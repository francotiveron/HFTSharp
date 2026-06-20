#include <iostream>
#include <atomic>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <ctime>
#include <csignal>
#include <cstdio>

extern "C" {
#include "../shared/hft_shm.h"
}

/* ------------------------------------------------------------------ */
/* Config                                                               */
/* ------------------------------------------------------------------ */
static constexpr int    TICK_MS        = 10;    /* executor tick interval      */
static constexpr double PRICE_START    = 100.0; /* simulated mid-price start   */
static constexpr double PRICE_DRIFT    = 0.05;  /* max random move per tick    */

/* ------------------------------------------------------------------ */
/* Graceful shutdown                                                    */
/* ------------------------------------------------------------------ */
static std::atomic<bool> g_running{true};

static void on_signal(int) { g_running.store(false, std::memory_order_relaxed); }

/* ------------------------------------------------------------------ */
/* Atomic helpers for reading shared params                             */
/*                                                                      */
/* HftStrategyParams fields are plain C types, not _Atomic, because    */
/* the struct is shared with F# via P/Invoke and _Atomic would change  */
/* its ABI on some compilers. We use std::atomic_ref (C++20) to read   */
/* them correctly without modifying the struct layout.                  */
/* ------------------------------------------------------------------ */
static double  read_double (const double&  field) {
    return std::atomic_ref<const double> (field).load(std::memory_order_acquire);
}
static int32_t read_int32  (const int32_t& field) {
    return std::atomic_ref<const int32_t>(field).load(std::memory_order_acquire);
}
static int64_t read_int64  (const int64_t& field) {
    return std::atomic_ref<const int64_t>(field).load(std::memory_order_acquire);
}

/* ------------------------------------------------------------------ */
/* Timestamp helper                                                     */
/* ------------------------------------------------------------------ */
static int64_t now_ns()
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1'000'000'000LL + ts.tv_nsec;
}

/* ------------------------------------------------------------------ */
/* Main executor loop                                                   */
/* ------------------------------------------------------------------ */
int main()
{
    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);
    std::srand(static_cast<unsigned>(std::time(nullptr)));

    /* --- attach shared memory --- */
    HftSharedMemory shm = hft_shm_init();
    if (!shm) {
        std::fprintf(stderr, "[executor] ERROR: failed to attach shared memory\n");
        return 1;
    }
    std::puts("[executor] shared memory attached — waiting for F# commander...");

    HftSharedRegion*  region = static_cast<HftSharedRegion*>(shm);
    HftStrategyParams& params = region->params;

    /* --- wait until F# has written a valid strategy version --- */
    while (g_running.load(std::memory_order_relaxed)) {
        if (read_int64(params.strategy_version) > 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::puts("[executor] strategy params received — starting tick loop");

    double   mid_price = PRICE_START;
    double   position  = 0.0;
    int64_t  order_id  = 0;

    while (g_running.load(std::memory_order_relaxed))
    {
        /* --- simulate random price walk --- */
        double move = ((std::rand() / (double)RAND_MAX) * 2.0 - 1.0) * PRICE_DRIFT;
        mid_price  += move;

        /* --- snapshot params atomically --- */
        int32_t enabled      = read_int32 (params.trading_enabled);
        double  bid_thr      = read_double(params.bid_threshold);
        double  ask_thr      = read_double(params.ask_threshold);
        double  max_pos      = read_double(params.max_position);

        if (enabled) {
            HftExecutionEvent ev{};
            ev.timestamp_ns = now_ns();
            ev.order_id     = ++order_id;
            ev.price        = mid_price;
            bool fire       = false;

            if (mid_price <= bid_thr && position < max_pos) {
                /* price at or below bid threshold — buy */
                ev.quantity  =  1.0;
                ev.side      =  1;
                ev.event_type = 1;   /* fill */
                position     +=  1.0;
                fire          = true;
            } else if (mid_price >= ask_thr && position > -max_pos) {
                /* price at or above ask threshold — sell */
                ev.quantity  =  1.0;
                ev.side      = -1;
                ev.event_type = 1;   /* fill */
                position     -=  1.0;
                fire          = true;
            }

            if (fire) {
                if (!hft_ring_try_write(shm, &ev)) {
                    std::fprintf(stderr, "[executor] WARNING: ring full, dropping event %lld\n",
                                 (long long)ev.order_id);
                } else {
                    std::printf("[executor] fill #%lld  side=%+d  price=%.4f  pos=%.0f\n",
                                (long long)ev.order_id, ev.side, ev.price, position);
                }
            }
        } else {
            std::puts("[executor] trading HALTED by kill switch");
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(TICK_MS));
    }

    std::puts("[executor] shutting down");
    hft_shm_cleanup(shm);
    return 0;
}
