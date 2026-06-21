#include <cstdio>
#include <csignal>
#include <random>
#include <immintrin.h>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <time.h>

#include "../shared/hft_shm.h"

#ifdef HFT_VERBOSE
#define HFT_LOG(fmt, ...) std::printf(fmt, ##__VA_ARGS__)
#define HFT_WARN(msg) std::fputs(msg "\n", stderr)
#else
#define HFT_LOG(fmt, ...) ((void)0)
#define HFT_WARN(msg) ((void)0)
#endif

static constexpr int EXECUTOR_CPU = 2;
static constexpr int RT_PRIORITY = 80;
static constexpr int64_t TICK_NS = 10'000'000LL;
static constexpr double PRICE_START = 100.0;
static constexpr double PRICE_DRIFT = 0.05;

static bool running = true;
static void on_signal(int) { running = false; }

// Pin to a dedicated core — eliminates cache misses from thread migration.
// In production also set isolcpus= in kernel boot params.
static bool pin_to_cpu(int cpu)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == 0;
}

// SCHED_FIFO: only kernel threads and IRQs can preempt us. Requires root or CAP_SYS_NICE.
static bool set_realtime(int priority)
{
    struct sched_param sp{};
    sp.sched_priority = priority;
    return sched_setscheduler(0, SCHED_FIFO, &sp) == 0;
}

static inline int64_t mono_ns()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1'000'000'000LL + ts.tv_nsec;
}

static inline int64_t wall_ns()
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1'000'000'000LL + ts.tv_nsec;
}

// _mm_pause signals a spin-wait loop: reduces power and avoids memory order machine clears.
static inline void spin_until(int64_t deadline_ns)
{
    while (mono_ns() < deadline_ns)
        _mm_pause();
}

int main()
{
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    std::mt19937 rng{std::random_device{}()};
    std::uniform_real_distribution<double> dist{-PRICE_DRIFT, PRICE_DRIFT};

    pthread_setname_np(pthread_self(), "hft-executor");

    if (pin_to_cpu(EXECUTOR_CPU))
        std::printf("[executor] pinned to CPU %d\n", EXECUTOR_CPU);
    else
        std::puts("[executor] WARNING: CPU pinning failed");

    if (set_realtime(RT_PRIORITY))
        std::printf("[executor] SCHED_FIFO priority %d\n", RT_PRIORITY);
    else
        std::puts("[executor] WARNING: SCHED_FIFO failed — continuing at normal priority");

    HftSharedMemory shm = hft_shm_init();
    if (!shm) {
        std::fprintf(stderr, "[executor] ERROR: failed to attach shared memory\n");
        return 1;
    }
    std::puts("[executor] shared memory attached — waiting for F# commander...");

    // Huge pages reduce TLB pressure on repeated ring accesses.
    madvise(shm, sizeof(HftSharedRegion), MADV_HUGEPAGE);

    HftSharedRegion* region = static_cast<HftSharedRegion*>(shm);
    HftStrategyParams& pars = region->pars;
    HftExecutionRing* ring = &region->execution_ring;

    while (running) {
        int32_t v = pars.strategy_version.load(std::memory_order_acquire);
        if (v > 0 && (v & 1) == 0) break;   // even and non-zero → write complete
        _mm_pause();
    }
    std::puts("[executor] params received — starting tick loop");

    double mid_price = PRICE_START;
    double position = 0.0;
    int64_t order_id = 0;
    uint32_t write_head = 0;
    int64_t next_tick = mono_ns();

    int32_t cached_version = 0;
    int32_t enabled = 0;
    double bid_thr = 0.0, ask_thr = 0.0, max_pos = 0.0;

    auto reload_params = [&]() {
        int32_t ver;

        do {
            do {
                ver = pars.strategy_version.load(std::memory_order_acquire);
                if (ver & 1) _mm_pause();
            } while (ver & 1);

            enabled = pars.trading_enabled;
            bid_thr = pars.bid_threshold;
            ask_thr = pars.ask_threshold;
            max_pos = pars.max_position;
        } while (pars.strategy_version.load(std::memory_order_acquire) != ver);
        cached_version = ver;
    };

    while (running)
    {
        spin_until(next_tick);
        next_tick += TICK_NS;
        mid_price += dist(rng);
        if (pars.strategy_version.load(std::memory_order_acquire) != cached_version) reload_params();

        if (enabled) [[likely]]
        {
            int32_t side = 0;
            if (mid_price <= bid_thr && position < max_pos) side = +1;
            else if (mid_price >= ask_thr && position > -max_pos) side = -1;

            if (side) [[unlikely]]
            {
                uint32_t tail = ring->read_tail.load(std::memory_order_acquire);
                if (write_head - tail >= HFT_RING_CAPACITY) HFT_WARN("[executor] WARNING: ring full, dropping");
                else {
                    position += side;
                    HftExecutionEvent ev{};
                    ev.timestamp_ns = wall_ns();
                    ev.order_id = ++order_id;
                    ev.price = mid_price;
                    ev.quantity = 1.0;
                    ev.side = side;
                    ev.event_type = 1;
                    __builtin_prefetch(&ring->events[(write_head + 1) & (HFT_RING_CAPACITY - 1)], 1, 0);
                    ring->events[write_head & (HFT_RING_CAPACITY - 1)] = ev;
                    ring->write_head.store(++write_head, std::memory_order_release);
                    HFT_LOG("[executor] fill #%lld  side=%+d  price=%.4f  pos=%.0f\n", (long long)ev.order_id, ev.side, ev.price, position);
                }
            }
        }
        else HFT_LOG("[executor] HALTED\n");
    }

    HFT_LOG("[executor] shutting down");
    hft_shm_cleanup(shm);
    return 0;
}
