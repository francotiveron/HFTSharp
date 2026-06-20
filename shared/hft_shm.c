#include "hft_shm.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <stdatomic.h>

static const char* SHM_NAME = "/hft_demo";

HftSharedMemory hft_shm_init(void)
{
    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0600);
    if (fd == -1) return NULL;

    if (ftruncate(fd, sizeof(HftSharedRegion)) == -1) {
        close(fd);
        return NULL;
    }

    void* ptr = mmap(NULL, sizeof(HftSharedRegion),
                     PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    if (ptr == MAP_FAILED) return NULL;

    mlock(ptr, sizeof(HftSharedRegion));

    return ptr;
}

void hft_shm_cleanup(HftSharedMemory shm)
{
    if (shm) {
        munmap(shm, sizeof(HftSharedRegion));
        shm_unlink(SHM_NAME);
    }
}

bool hft_ring_try_write(HftSharedMemory shm, const HftExecutionEvent* event)
{
    if (!shm || !event) return false;

    HftSharedRegion* region = (HftSharedRegion*)shm;
    HftExecutionRing* ring  = &region->execution_ring;

    uint64_t head = atomic_load_explicit((_Atomic uint64_t*)&ring->write_head, memory_order_relaxed);
    uint64_t tail = atomic_load_explicit((_Atomic uint64_t*)&ring->read_tail,  memory_order_acquire);

    /* Ring full if head is a full lap ahead of tail */
    if (head - tail >= HFT_RING_CAPACITY) return false;

    ring->events[head % HFT_RING_CAPACITY] = *event;

    atomic_store_explicit((_Atomic uint64_t*)&ring->write_head, head + 1, memory_order_release);
    return true;
}
