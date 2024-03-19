#include <benchmark/benchmark.h>
#include <jl.h>

std::vector<ssize_t> log2_size{1, 20, 30, 40};

template <int flags>
void BM_mmap_empty_file(benchmark::State& state) {
  auto fd = jl::tmpfd().unlink();
  auto map_size = 1L << state.range(0);
  size_t bytes_mapped = 0;
  for (auto _ : state) {
    jl::unique_mmap<char> map(map_size, PROT_READ, flags, fd.fd());
    benchmark::DoNotOptimize(bytes_mapped += map->size());
  }
  state.counters["Throughput"] = benchmark::Counter(static_cast<double>(bytes_mapped), benchmark::Counter::kIsRate);
}
BENCHMARK_TEMPLATE(BM_mmap_empty_file, MAP_SHARED)->ArgName("log2_size")->ArgsProduct({log2_size});
BENCHMARK_TEMPLATE(BM_mmap_empty_file, MAP_SHARED | MAP_POPULATE)->ArgName("log2_size")->ArgsProduct({log2_size});

template <int flags>
void BM_mmap_sparse_file(benchmark::State& state) {
  auto fd = jl::tmpfd().unlink();
  auto map_size = 1L << state.range(0);
  jl::unwrap(jl::truncate(fd.fd(), map_size));
  size_t bytes_mapped = 0;
  for (auto _ : state) {
    jl::unique_mmap<char> map(map_size, PROT_READ, flags, fd.fd());
    benchmark::DoNotOptimize(bytes_mapped += map->size());
  }
  state.counters["Throughput"] = benchmark::Counter(static_cast<double>(bytes_mapped), benchmark::Counter::kIsRate);
}
BENCHMARK_TEMPLATE(BM_mmap_sparse_file, MAP_SHARED)->ArgName("log2_size")->ArgsProduct({log2_size});
BENCHMARK_TEMPLATE(BM_mmap_sparse_file, MAP_SHARED | MAP_POPULATE)->ArgName("log2_size")->ArgsProduct({log2_size});

BENCHMARK_MAIN();
