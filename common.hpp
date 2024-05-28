#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifndef RING_BUFFER_SIZE
#define RING_BUFFER_SIZE 1024
#endif

const char *log_prefix(const char *func, int line)
{
    thread_local static char prefix[100];
    sprintf(prefix, "%*s():%-3d ", 15, func, line);
    return prefix;
}

#define log_printf_impl(fmt, ...)                                                     \
    {                                                                                 \
        dprintf(2, "%s: " fmt "%s", log_prefix(__FUNCTION__, __LINE__), __VA_ARGS__); \
    }
#define log_printf(...) log_printf_impl(__VA_ARGS__, "")

#define fail_with_strerror(code, msg)                      \
    {                                                      \
        char err_buf[1024];                                \
        strerror_r(code, err_buf, sizeof(err_buf));        \
        log_printf(msg " (From err code: %s)\n", err_buf); \
        exit(EXIT_FAILURE);                                \
    }

#define verify(stmt) \
    if (!(stmt))     \
    fail_with_strerror(errno, "'" #stmt "' failed.")

void assign_cpus(std::vector<size_t> cpus)
{
    cpu_set_t cpuset;
    pthread_t thread(pthread_self());

    CPU_ZERO(&cpuset);
    for (size_t c : cpus)
        CPU_SET(c, &cpuset);

    int s = pthread_setaffinity_np(thread, sizeof(cpuset), &cpuset);
    verify(s == 0);
    log_printf("  Cpu assigned\n");
}

template <typename T, size_t BufferSize>
struct SharedStateGeneralized
{
    static constexpr char shmName[] = "__my_ring_buffer__";
    using ValueType = T;

    static SharedStateGeneralized *load_state(bool do_create = false)
    {
        int fd = shm_open(shmName, O_RDWR | (do_create ? O_CREAT : 0), 0644);
        verify(fd >= 0);
        verify(ftruncate(fd, sizeof(SharedStateGeneralized)) == 0);
        SharedStateGeneralized *state = static_cast<SharedStateGeneralized *>(mmap(
            /* desired addr, addr = */ NULL,
            /* length = */ sizeof(SharedStateGeneralized),
            /* access attributes, prot = */ PROT_READ | PROT_WRITE,
            /* flags = */ MAP_SHARED,
            /* fd = */ fd,
            /* offset in file, offset = */ 0));
        close(fd);

        verify(state != MAP_FAILED);

        if (do_create) new (state) SharedStateGeneralized();

        log_printf("  Shared state loaded\n");
        return state;
    }

    static void unload_state(SharedStateGeneralized *state, bool do_unlink = false)
    {
        if (do_unlink) state->~SharedStateGeneralized();
        verify(munmap(state, sizeof(SharedStateGeneralized)) == 0);
        if (do_unlink) verify(shm_unlink(shmName) == 0);

        log_printf("  State unloaded\n");
    }

    static size_t next(size_t ind)
    {
        return (ind + 1) % BufferSize;
    }

private:
    SharedStateGeneralized() noexcept = default;

public:
    alignas(64) std::atomic<size_t> read{0};
    alignas(64) std::atomic<size_t> write{0};
    T data[BufferSize];
};

using SharedState = SharedStateGeneralized < size_t, RING_BUFFER_SIZE>;

struct StatsHolder
{
    StatsHolder(size_t sz = 0)
    {
        stats.reserve(sz);
    }

    void add(uint64_t val)
    {
        stats.push_back(val);
    }

    void save(std::string filename)
    {
        std::ofstream statFile(filename, std::ios::trunc);
        if (statFile.is_open())
        {
            for (auto stat : stats)
            {
                statFile << stat << '\n';
            }
            log_printf("  Stats saved to file\n");
        }
        else log_printf("  Unable to open file\n");
    }

    std::vector<uint64_t> stats;
};
