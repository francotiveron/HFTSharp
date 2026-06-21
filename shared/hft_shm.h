#pragma once

#include <stdint.h>
#include <atomic>

#define HFT_RING_CAPACITY 1024u
#define HFT_CACHE_LINE    64

typedef struct {
    double  bid_threshold;
    double  ask_threshold;
    double  max_position;
    std::atomic<int32_t>  strategy_version;
    int32_t trading_enabled;
    uint8_t _pad[32];
} __attribute__((aligned(HFT_CACHE_LINE))) HftStrategyParams;

typedef struct {
    int64_t timestamp_ns;
    int64_t order_id;
    double  price;
    double  quantity;
    int32_t side;
    int32_t event_type;
} HftExecutionEvent;

typedef struct {
    std::atomic<uint32_t> write_head;
    uint8_t _pad1[60];
    std::atomic<uint32_t> read_tail;
    uint8_t _pad2[60];
    HftExecutionEvent events[HFT_RING_CAPACITY];
} __attribute__((aligned(HFT_CACHE_LINE))) HftExecutionRing;

typedef struct {
    HftStrategyParams pars;
    HftExecutionRing execution_ring;
} HftSharedRegion;

typedef void* HftSharedMemory;

extern "C" {
HftSharedMemory hft_shm_init();
void hft_shm_cleanup(HftSharedMemory shm);
}
