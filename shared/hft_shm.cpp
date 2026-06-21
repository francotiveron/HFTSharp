#include "hft_shm.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static const char* SHM_NAME = "/hft_demo";

HftSharedMemory hft_shm_init()
{
    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0600);
    if (fd == -1) return nullptr;

    if (ftruncate(fd, sizeof(HftSharedRegion)) == -1) {
        close(fd);
        return nullptr;
    }

    void* ptr = mmap(nullptr, sizeof(HftSharedRegion),
                     PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    if (ptr == MAP_FAILED) return nullptr;

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
