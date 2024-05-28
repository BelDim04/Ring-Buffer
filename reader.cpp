#include "common.hpp"

template <typename SharedState, size_t BatchSize>
struct Reader
{
    Reader(SharedState &sharedState_p)
        : sharedState(sharedState_p)
    {
        static_assert(BatchSize < RING_BUFFER_SIZE);
    }

    void pop(typename SharedState::ValueType &element)
    {
        if (nextRead == localWrite)
        {
            while (nextRead == sharedState.write.load(std::memory_order_acquire)) {}
            localWrite = sharedState.write.load(std::memory_order_relaxed);
        }

        element = sharedState.data[nextRead];
        nextRead = SharedState::next(nextRead);
        if constexpr (BatchSize > 1)
        {
            if (++curBatch >= BatchSize) curBatch = 0;
            else return;
        }
        sharedState.read.store(nextRead, std::memory_order_release);
    }

    SharedState &sharedState;
    size_t localWrite{0};
    size_t nextRead{0};
    size_t curBatch{0};
};

int main(int argc, char **argv)
{
    verify(argc == 2);
    size_t testingSize(std::atoi(argv[1]));
    assign_cpus({2, 3});

    auto *ss = SharedState::load_state();
    Reader<SharedState,
#ifdef BATCH_SIZE
    BATCH_SIZE
#else
    1
#endif
    > reader(*ss);
    StatsHolder statsHolder(testingSize);

    log_printf("  Start reading total %zu objects\n", testingSize);
    for (size_t i = 0; i < testingSize; ++i)
    {
        size_t x;
        reader.pop(x);
        statsHolder.add(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    }
    log_printf("  Reading finished\n");

    SharedState::unload_state(ss);
    statsHolder.save("reader.txt");
}