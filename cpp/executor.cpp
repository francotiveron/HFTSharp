#include <cstdio>
#include <csignal>
#include <atomic>
#include <random>        /* mt19937, uniform_real_distribution            */
#include <immintrin.h>   /* _mm_pause                                     */
#include <pthread.h>     /* pthread_setaffinity_np, pthread_setname_np    */
#include <sched.h>       /* sched_setscheduler, SCHED_FIFO                */
#include <sys/mman.h>    /* madvise, MADV_HUGEPAGE                        */
#include <time.h>

extern "C" {
#include "../shared/hft_shm.h"
}

/* ------------------------------------------------------------------ */
/* Tuning knobs                                                         */
/* ------------------------------------------------------------------ */
static constexpr int     EXECUTOR_CPU  = 2;           /* core to pin to              */
static constexpr int     RT_PRIORITY   = 80;          /* SCHED_FIFO priority (1-99)  */
static constexpr int64_t TICK_NS       = 10'000'000LL;/* simulated tick: 10 ms       */
static constexpr double  PRICE_START   = 100.0;
static constexpr double  PRICE_DRIFT   = 0.05;

/* ------------------------------------------------------------------ */
/* Graceful shutdown                                                    */
/* ------------------------------------------------------------------ */
static std::atomic<bool> g_running{true};
static void on_signal(int) { g_running.store(false, std::memory_order_relaxed); }

/* ------------------------------------------------------------------ */
/* CPU pinning                                                          */
/*                                                                      */
/* Pins this thread to a single core. Eliminates latency spikes caused  */
/* by the OS migrating the thread to a cold cache on another core.      */
/* In production, dedicate an isolated core (isolcpus= kernel param).   */
/* ------------------------------------------------------------------ */
static bool pin_to_cpu(int cpu)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == 0;
}

/* ------------------------------------------------------------------ */
/* Real-time scheduling                                                 */
/*                                                                      */
/* SCHED_FIFO prevents the OS from preempting this thread in favour of  */
/* lower-priority work. With priority 80, only kernel threads and IRQs  */
/* can interrupt us. Requires root or CAP_SYS_NICE.                     */
/* ------------------------------------------------------------------ */
static bool set_realtime(int priority)
{
    struct sched_param sp{};
    sp.sched_priority = priority;
    return sched_setscheduler(0, SCHED_FIFO, &sp) == 0;
}

/* ------------------------------------------------------------------ */
/* Clocks                                                               */
/*                                                                      */
/* mono_ns(): CLOCK_MONOTONIC — no NTP jumps, used for spin-wait timing */
/* wall_ns(): CLOCK_REALTIME  — wall clock, used for event timestamps   */
/* ------------------------------------------------------------------ */
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

/* ------------------------------------------------------------------ */
/* Spin-wait                                                            */
/*                                                                      */
/* Busy-spins until deadline_ns (monotonic). _mm_pause tells the CPU   */
/* this is a spin-wait loop: reduces power, improves hyperthreaded      */
/* sibling throughput, and avoids memory order machine clears.          */
/*                                                                      */
/* sleep_for(10ms) can overshoot by 100-500 us depending on kernel      */
/* scheduling; a spin-wait wakes within nanoseconds.                   */
/* ------------------------------------------------------------------ */
static inline void spin_until(int64_t deadline_ns)
{
    while (mono_ns() < deadline_ns)
        _mm_pause();
}

/* ------------------------------------------------------------------ */
/* Atomic param reads via std::atomic_ref (C++20)                      */
/*                                                                      */
/* HftStrategyParams fields are plain C types (not _Atomic) so the ABI */
/* is compatible with F#. std::atomic_ref imposes acquire semantics on  */
/* any aligned storage without touching the struct layout.              */
/* ------------------------------------------------------------------ */
static inline double  read_double (const double&  f) { return std::atomic_ref<const double> (f).load(std::memory_order_acquire); }
static inline int32_t read_int32  (const int32_t& f) { return std::atomic_ref<const int32_t>(f).load(std::memory_order_acquire); }
static inline int64_t read_int64  (const int64_t& f) { return std::atomic_ref<const int64_t>(f).load(std::memory_order_acquire); }

/* ------------------------------------------------------------------ */
/* Main executor loop                                                   */
/* ------------------------------------------------------------------ */
int main()
{
    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);

    std::mt19937                          rng{std::random_device{}()};
    std::uniform_real_distribution<double> dist{-PRICE_DRIFT, PRICE_DRIFT};

    /* name this thread for profiler / perf visibility */
    pthread_setname_np(pthread_self(), "hft-executor");

    /* --- CPU pinning --- */
    if (pin_to_cpu(EXECUTOR_CPU))
        std::printf("[executor] pinned to CPU %d\n", EXECUTOR_CPU);
    else
        std::puts("[executor] WARNING: CPU pinning failed — "
                  "try a lower EXECUTOR_CPU index or run as root");

    /* --- Real-time scheduling --- */
    if (set_realtime(RT_PRIORITY))
        std::printf("[executor] SCHED_FIFO priority %d set\n", RT_PRIORITY);
    else
        std::puts("[executor] WARNING: SCHED_FIFO failed — "
                  "requires root or CAP_SYS_NICE; continuing at normal priority");

    /* --- Attach shared memory --- */
    HftSharedMemory shm = hft_shm_init();
    if (!shm) {
        std::fprintf(stderr, "[executor] ERROR: failed to attach shared memory\n");
        return 1;
    }
    std::puts("[executor] shared memory attached — waiting for F# commander...");

    /* Advise kernel to use 2 MB huge pages for this region.
     * Reduces TLB pressure: 1 huge-page entry vs ~40 normal 4 KB entries
     * for the same data — meaningful when the hot loop touches the ring
     * on every tick. mlock() is already applied inside hft_shm_init. */
    madvise(shm, sizeof(HftSharedRegion), MADV_HUGEPAGE);

    HftSharedRegion*   region = static_cast<HftSharedRegion*>(shm);
    HftStrategyParams& params = region->params;
    HftExecutionRing*  ring   = &region->execution_ring;

    /* --- Spin-wait for F# to write a valid strategy version ---
     * _mm_pause rather than sleep: react within nanoseconds the
     * moment the commander sets strategy_version > 0. */
    while (g_running.load(std::memory_order_relaxed)) {
        if (read_int64(params.strategy_version) > 0) break;
        _mm_pause();
    }
    std::puts("[executor] strategy params received — starting tick loop");

    double  mid_price = PRICE_START;
    double  position  = 0.0;
    int64_t order_id  = 0;
    int64_t next_tick = mono_ns();

    /* ---- Hot loop ---- */
    while (g_running.load(std::memory_order_relaxed))
    {
        /* Spin-wait until next tick boundary.
         * Deterministic inter-tick spacing; sleep_for overshoots by
         * hundreds of microseconds on a non-RT kernel. */
        spin_until(next_tick);
        next_tick += TICK_NS;

        /* --- Simulate random price walk --- */
        mid_price += dist(rng);

        /* --- Snapshot params with acquire semantics ---
         * Sees all param writes that F# released before incrementing
         * strategy_version. No torn reads on aligned doubles on x86-64,
         * but acquire is required for cross-core memory ordering. */
        int32_t enabled = read_int32 (params.trading_enabled);
        double  bid_thr = read_double(params.bid_threshold);
        double  ask_thr = read_double(params.ask_threshold);
        double  max_pos = read_double(params.max_position);

        if (enabled) [[likely]]
        {
            HftExecutionEvent ev{};
            ev.timestamp_ns = wall_ns();
            ev.order_id     = ++order_id;
            ev.price        = mid_price;
            bool fire       = false;

            if (mid_price <= bid_thr && position < max_pos) {
                ev.quantity   =  1.0;
                ev.side       =  1;   /* buy  */
                ev.event_type =  1;   /* fill */
                position      +=  1.0;
                fire           = true;
            } else if (mid_price >= ask_thr && position > -max_pos) {
                ev.quantity   =  1.0;
                ev.side       = -1;   /* sell */
                ev.event_type =  1;   /* fill */
                position      -=  1.0;
                fire           = true;
            }

            if (fire) [[unlikely]]
            {
                /* Prefetch the NEXT ring slot into L1 before we need it.
                 * Hides the ~100 ns DRAM latency on a cold cache miss when
                 * the ring wraps or hasn't been touched recently.
                 * write=1, locality=0: write prefetch, no temporal reuse. */
                __builtin_prefetch(
                    &ring->events[(order_id) % HFT_RING_CAPACITY],
                    /*write=*/1, /*locality=*/0);

                if (!hft_ring_try_write(shm, &ev))
                    /* Ring full — in production use a lock-free logger;
                     * printf acquires a mutex and must never be on the
                     * hot path in a real system. */
                    std::fprintf(stderr,
                        "[executor] WARNING: ring full, dropping event %lld\n",
                        (long long)ev.order_id);
                else
                    std::printf("[executor] fill #%lld  side=%+d  price=%.4f  pos=%.0f\n",
                        (long long)ev.order_id, ev.side, ev.price, position);
            }
        }
        else
        {
            std::puts("[executor] trading HALTED by kill switch");
        }
    }

    std::puts("[executor] shutting down");
    hft_shm_cleanup(shm);
    return 0;
}
