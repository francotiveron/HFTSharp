#include <cstdio>
#include <csignal>
#include <atomic>
#include <random>
#include <immintrin.h>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <time.h>

extern "C" {
#include "../shared/hft_shm.h"
}

static constexpr int EXECUTOR_CPU = 2;
static constexpr int RT_PRIORITY = 80;
static constexpr int64_t TICK_NS = 10'000'000LL;
static constexpr double PRICE_START = 100.0;
static constexpr double PRICE_DRIFT = 0.05;

static std::atomic<bool> g_running{true};
static void on_signal(int) { g_running.store(false, std::memory_order_relaxed); }

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

// std::atomic_ref imposes acquire semantics on plain C struct fields without changing their layout.
static inline double read_double(const double& f) { return std::atomic_ref<const double>(f).load(std::memory_order_acquire); }
static inline int32_t read_int32(const int32_t& f) { return std::atomic_ref<const int32_t>(f).load(std::memory_order_acquire); }
static inline int64_t read_int64(const int64_t& f) { return std::atomic_ref<const int64_t>(f).load(std::memory_order_acquire); }

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

    while (g_running.load(std::memory_order_relaxed)) {
        if (read_int64(pars.strategy_version) > 0) break;
        _mm_pause();
    }
    std::puts("[executor] params received — starting tick loop");

    double mid_price = PRICE_START;
    double position = 0.0;
    int64_t order_id = 0;
    int64_t next_tick = mono_ns();

    while (g_running.load(std::memory_order_relaxed))
    {
        spin_until(next_tick);
        next_tick += TICK_NS;

        mid_price += dist(rng);

        int32_t enabled = read_int32(pars.trading_enabled);
        double bid_thr = read_double(pars.bid_threshold);
        double ask_thr = read_double(pars.ask_threshold);
        double max_pos = read_double(pars.max_position);

        if (enabled) [[likely]]
        {
            HftExecutionEvent ev{};
            ev.timestamp_ns = wall_ns();
            ev.order_id = ++order_id;
            ev.price = mid_price;
            bool fire = false;

            if (mid_price <= bid_thr && position < max_pos) {
                ev.quantity = 1.0; ev.side = 1; ev.event_type = 1;
                position += 1.0; fire = true;
            } else if (mid_price >= ask_thr && position > -max_pos) {
                ev.quantity = 1.0; ev.side = -1; ev.event_type = 1;
                position -= 1.0; fire = true;
            }

            if (fire) [[unlikely]]
            {
                uint64_t head = std::atomic_ref<uint64_t>(ring->write_head).load(std::memory_order_relaxed);
                uint64_t tail = std::atomic_ref<uint64_t>(ring->read_tail).load(std::memory_order_acquire);

                if (head - tail >= HFT_RING_CAPACITY) {
                    std::fprintf(stderr, "[executor] WARNING: ring full, dropping #%lld\n",
                                 (long long)ev.order_id);
                } else {
                    // Prefetch next slot to hide DRAM latency on a cold cache miss.
                    __builtin_prefetch(&ring->events[(head + 1) % HFT_RING_CAPACITY], 1, 0);
                    ring->events[head % HFT_RING_CAPACITY] = ev;
                    std::atomic_ref<uint64_t>(ring->write_head).store(head + 1, std::memory_order_release);
                    std::printf("[executor] fill #%lld  side=%+d  price=%.4f  pos=%.0f\n",
                                (long long)ev.order_id, ev.side, ev.price, position);
                }
            }
        }
        else
        {
            std::puts("[executor] HALTED");
        }
    }

    std::puts("[executor] shutting down");
    hft_shm_cleanup(shm);
    return 0;
}
