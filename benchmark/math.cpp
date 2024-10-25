#include <benchmark/benchmark.h>

void BM_ConstantDivision(benchmark::State& state) {
  size_t n = 0;
  for (auto _ : state) {
    for (size_t i = 1; i != 0; i <<= 1) {
      benchmark::DoNotOptimize(n = i / 1'000'000);
    }
  }
}
BENCHMARK(BM_ConstantDivision);

void BM_RuntimeDivision(benchmark::State& state) {
  size_t n = 0;
  volatile size_t divisor = 1'000'000;
  for (auto _ : state) {
    for (size_t i = 1; i != 0; i <<= 1) {
      benchmark::DoNotOptimize(n = i / divisor);
    }
  }
}
BENCHMARK(BM_RuntimeDivision);

BENCHMARK_MAIN(); // NOLINT
