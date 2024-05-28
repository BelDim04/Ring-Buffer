#include "common.hpp"

template <typename SharedState, size_t BatchSize>
struct Writer
{
    Writer(SharedState &sharedState_p)
        : sharedState(sharedState_p)
    {
        static_assert(BatchSize < RING_BUFFER_SIZE);
    }

    void push(typename SharedState::ValueType element, bool forceNotify = false)
    {
        size_t after(SharedState::next(nextWrite));
        if (after == localRead)
        {
            while (after == sharedState.read.load(std::memory_order_acquire)) {}
            localRead = sharedState.read.load(std::memory_order_relaxed);
        }

        sharedState.data[nextWrite] = element;
        nextWrite = after;
        if constexpr (BatchSize > 1)
        {
            if (++curBatch >= BatchSize || forceNotify) curBatch = 0;
            else return;
        }
        sharedState.write.store(nextWrite, std::memory_order_release);
    }

    SharedState &sharedState;
    size_t localRead{0};
    size_t nextWrite{0};
    size_t curBatch{0};
};

int main(int argc, char **argv)
{
    verify(argc == 3);
    size_t testingSize(std::atoi(argv[1]));
    auto nsec(std::atoi(argv[2]));
    timespec tv{.tv_sec = nsec / 1000000000, .tv_nsec = nsec % 1000000000};
    assign_cpus({0, 1});

    auto *ss = SharedState::load_state(/*do_create=*/true);
    Writer<SharedState,
#ifdef BATCH_SIZE
    BATCH_SIZE
#else
    1
#endif
    > writer(*ss);
    StatsHolder statsHolder(testingSize);
    timespec get_ready_tv{.tv_sec = 3, .tv_nsec = 0};
    pselect(0, NULL, NULL, NULL, &get_ready_tv, NULL);

    log_printf("  Start writing total %zu objects, one per %uns\n", testingSize, static_cast<uint32_t>(tv.tv_nsec));
    for (size_t i = 0; i < testingSize; ++i)
    {
        bool fn(i + 1 == testingSize);
        statsHolder.add(std::chrono::high_resolution_clock::now().time_since_epoch().count());
        writer.push(i, fn);
        pselect(0, NULL, NULL, NULL, &tv, NULL);
    }
    log_printf("  Writing finished\n");

    SharedState::unload_state(ss, /*do_unlink=*/true);
    statsHolder.save("writer.txt");
}