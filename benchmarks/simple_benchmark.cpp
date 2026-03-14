#include <benchmark/benchmark.h>

#include <iostream>

static void simple_benchmark(benchmark::State& state)
{
    bool printed = false;

    for (auto _ : state)
    {
        benchmark::DoNotOptimize(_);
        if (!printed)
        {
            std::cout << "simple benchmark stdout\n";
            printed = true;
        }
    }
}

BENCHMARK(simple_benchmark);

