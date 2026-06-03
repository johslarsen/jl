#include <benchmark/benchmark.h>
#include <jl.h>

static void BM_ns_duration_cast_to_timespec(benchmark::State& state) {
  auto now = std::chrono::system_clock::now().time_since_epoch();
  timespec ts{};
  for (auto _ : state) {
    benchmark::DoNotOptimize(ts = jl::as_timespec(now--));
  }
  state.counters["ts"] = double(ts.tv_nsec);
}
BENCHMARK(BM_ns_duration_cast_to_timespec);

static void BM_ns_divmod_to_timespec(benchmark::State& state) {
  auto now = std::chrono::system_clock::now().time_since_epoch();
  timespec ts{};
  for (auto _ : state) {
    auto ns = (now--).count();
    benchmark::DoNotOptimize(ts = timespec{.tv_sec = ns / 1'000'000'000, .tv_nsec = ns % 1'000'000'000});
  }
  state.counters["ts"] = double(ts.tv_nsec);
}
BENCHMARK(BM_ns_divmod_to_timespec);
BENCHMARK_MAIN();  // NOLINT
