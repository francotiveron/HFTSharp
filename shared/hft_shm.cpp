#include "hft_shm.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static const char* SHM_NAME = "/hft_demo";

// Executor calls this: unlinks any stale segment, creates fresh (OS-zeroed).
HftSharedMemory hft_shm_init()
{
    shm_unlink(SHM_NAME);
    int fd = shm_open(SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd == -1) return nullptr;

    if (ftruncate(fd, sizeof(HftSharedRegion)) == -1) {
        close(fd);
        shm_unlink(SHM_NAME);
        return nullptr;
    }

    void* ptr = mmap(nullptr, sizeof(HftSharedRegion),
                     PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    if (ptr == MAP_FAILED) { shm_unlink(SHM_NAME); return nullptr; }

    mlock(ptr, sizeof(HftSharedRegion));
    return ptr;
}

// Commander calls this: attaches to the segment the executor already created.
HftSharedMemory hft_shm_attach()
{
    int fd = shm_open(SHM_NAME, O_RDWR, 0600);
    if (fd == -1) return nullptr;

    void* ptr = mmap(nullptr, sizeof(HftSharedRegion),
                     PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    if (ptr == MAP_FAILED) return nullptr;

    mlock(ptr, sizeof(HftSharedRegion));
    return ptr;
}

// Executor calls this on exit: munmap + unlink.
void hft_shm_cleanup(HftSharedMemory shm)
{
    if (shm) {
        munmap(shm, sizeof(HftSharedRegion));
        shm_unlink(SHM_NAME);
    }
}
