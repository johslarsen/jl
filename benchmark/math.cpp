#include <benchmark/benchmark.h>
#include <gtest/gtest.h>
#include <jl.h>

static void BM_ConstantDivision(benchmark::State& state) {
  size_t n = 0;
  for (auto _ : state) {
    for (size_t i = 1; i != 0; i <<= 1) {
      benchmark::DoNotOptimize(n = i / 1'000'000);
    }
  }
}
BENCHMARK(BM_ConstantDivision);

static void BM_RuntimeDivision(benchmark::State& state) {
  size_t n = 0;
  volatile size_t divisor = 1'000'000;
  for (auto _ : state) {
    for (size_t i = 1; i != 0; i <<= 1) {
      benchmark::DoNotOptimize(n = i / divisor);
    }
  }
}
BENCHMARK(BM_RuntimeDivision);

static auto doubles = jl::rands<256>(std::uniform_real_distribution(-100 * M_PI, 100 * M_PI), std::mt19937_64(42));

static void BM_normalize_minus_rounded(benchmark::State& state) {
  double sum = 0.0;
  for (auto _ : state) {
    benchmark::DoNotOptimize(sum = std::ranges::fold_left(doubles, 0.0, [](double s, double x) {
                               return s + x - std::round(x);
                             }));
  }
  state.counters["Sum"] = sum;
}
BENCHMARK(BM_normalize_minus_rounded);

static void BM_normalize_fmod_2pi(benchmark::State& state) {
  double sum = 0.0;
  for (auto _ : state) {
    benchmark::DoNotOptimize(sum = std::ranges::fold_left(doubles, 0.0, [](double s, double rad) {
                               double rem = std::fmod(rad, 2 * M_PI);
                               return s + rem - (rem > M_PI ? 2 * M_PI : 0);
                             }));
  }
  state.counters["Sum"] = sum;
}
BENCHMARK(BM_normalize_fmod_2pi);

static void BM_normalize_remainder_2pi(benchmark::State& state) {
  double sum = 0.0;
  for (auto _ : state) {
    benchmark::DoNotOptimize(sum = std::ranges::fold_left(doubles, 0.0, [](double s, double rad) {
                               return s + std::remainder(rad, 2 * M_PI);
                             }));
  }
  state.counters["Sum"] = sum;
}
BENCHMARK(BM_normalize_remainder_2pi);

static void BM_normalize_2pi_via_rounding(benchmark::State& state) {
  double sum = 0.0;
  for (auto _ : state) {
    benchmark::DoNotOptimize(sum = std::ranges::fold_left(doubles, 0.0, [](double s, double rad) {
                               double turns = rad / (2 * M_PI);
                               return s + 2 * M_PI * (turns - std::round(turns));
                             }));
  }
  state.counters["Sum"] = sum;
}
BENCHMARK(BM_normalize_2pi_via_rounding);

BENCHMARK_MAIN();  // NOLINT
