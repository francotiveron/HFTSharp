#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HFT_RING_CAPACITY 1024
#define HFT_CACHE_LINE    64

/* Written by F# (commander), read by C++ (executor) on every tick.
 * Padded to one cache line to avoid false sharing with the ring. */
typedef struct {
    double  bid_threshold;    /* buy if price <= this  */
    double  ask_threshold;    /* sell if price >= this */
    double  max_position;     /* absolute position limit */
    int64_t strategy_version; /* incremented by F# on each update */
    int32_t trading_enabled;  /* kill switch: 0=halt, 1=trade */
    uint8_t _pad[28];
} __attribute__((aligned(HFT_CACHE_LINE))) HftStrategyParams;

/* Written by C++ (executor), read by F# (commander). */
typedef struct {
    int64_t timestamp_ns;
    int64_t order_id;
    double  price;
    double  quantity;
    int32_t side;       /* 1=buy, -1=sell */
    int32_t event_type; /* 0=new, 1=fill, 2=reject */
} HftExecutionEvent;

/* SPSC ring — C++ writes (write_head), F# reads (read_tail).
 * Counters on separate cache lines to prevent false sharing. */
typedef struct {
    uint64_t write_head;
    uint8_t _pad1[56];
    uint64_t read_tail;
    uint8_t _pad2[56];
    HftExecutionEvent events[HFT_RING_CAPACITY];
} __attribute__((aligned(HFT_CACHE_LINE))) HftExecutionRing;

typedef struct {
    HftStrategyParams pars;
    HftExecutionRing execution_ring;
} HftSharedRegion;

typedef void* HftSharedMemory;

HftSharedMemory hft_shm_init(void);
void hft_shm_cleanup(HftSharedMemory shm);


#ifdef __cplusplus
}
#endif
