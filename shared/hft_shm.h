#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HFT_RING_CAPACITY 1024       /* must be power of 2 */
#define HFT_CACHE_LINE    64

/*
 * Written by the F# process (commander), read by the C++ process (executor) on every tick.
 * All fields must be accessed atomically (_Atomic on C side, Interlocked/Volatile on F# side).
 * Padded to one cache line to avoid false sharing.
 */
typedef struct {
    double   bid_threshold;     /* 8  - buy if market price <= this */
    double   ask_threshold;     /* 8  - sell if market price >= this */
    double   max_position;      /* 8  - absolute position limit      */
    int64_t  strategy_version;  /* 8  - incremented by F# on update  */
    int32_t  trading_enabled;   /* 4  - kill switch: 0=halt, 1=trade */
    uint8_t  _pad[28];          /* pad to 64 bytes                   */
} __attribute__((aligned(HFT_CACHE_LINE))) HftStrategyParams; /* sizeof == 64 */

/*
 * A single execution event written by the C++ executor into the ring.
 * Read by the F# commander for monitoring and risk checks.
 */
typedef struct {
    int64_t  timestamp_ns;  /* 8  - CLOCK_REALTIME_COARSE nanoseconds */
    int64_t  order_id;      /* 8  - monotonic order counter           */
    double   price;         /* 8  - executed price                    */
    double   quantity;      /* 8  - executed quantity                 */
    int32_t  side;          /* 4  - 1=buy, -1=sell                    */
    int32_t  event_type;    /* 4  - 0=new, 1=fill, 2=reject           */
} HftExecutionEvent;        /* sizeof == 40 */

/*
 * Single-producer / single-consumer ring buffer.
 * C++ executor writes (write_head), F# commander reads (read_tail).
 * Each counter on its own cache line to prevent false sharing.
 */
typedef struct {
    uint64_t         write_head;                    /* 8  - written by C++, read by F# */
    uint8_t          _pad1[56];                     /* pad to cache line               */
    uint64_t         read_tail;                     /* 8  - written by F#, read by C++ */
    uint8_t          _pad2[56];                     /* pad to cache line               */
    HftExecutionEvent events[HFT_RING_CAPACITY];    /* ring slots                      */
} __attribute__((aligned(HFT_CACHE_LINE))) HftExecutionRing;

/*
 * Full shared memory region mapped by both processes.
 * F# owns writes to pars; C++ owns writes to execution_ring.
 */
typedef struct {
    HftStrategyParams pars;
    HftExecutionRing  execution_ring;
} HftSharedRegion;

/* Opaque handle returned by hft_shm_init */
typedef void* HftSharedMemory;

/* Initialise shared memory — first caller creates, subsequent callers attach */
HftSharedMemory hft_shm_init(void);
void            hft_shm_cleanup(HftSharedMemory shm);

/*
 * Ring write — called by the C++ executor via normal linkage (not P/Invoke).
 * F# reads the ring and writes pars directly through the mapped pointer
 * using Volatile.Read / Volatile.Write — no P/Invoke required.
 */
bool hft_ring_try_write(HftSharedMemory shm, const HftExecutionEvent* event);

#ifdef __cplusplus
}
#endif
