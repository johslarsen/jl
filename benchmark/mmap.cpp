#include <benchmark/benchmark.h>
#include <jl_posix.h>

static std::vector<ssize_t> log2_1_1M_1G_1P{1, 20, 30, 40};
static std::vector<ssize_t> log2_4K_1M_128M{12, 20, 27};

template <int flags>
static void BM_mmap_empty_file(benchmark::State& state) {
  auto fd = jl::unwrap(jl::tmpfd::unlinked());
  auto map_size = 1L << state.range(0);
  size_t bytes_mapped = 0;
  for (auto _ : state) {
    auto map = jl::unwrap(jl::unique_mmap<char>::map(map_size, PROT_READ, flags, fd.fd()));
    benchmark::DoNotOptimize(bytes_mapped += map->size());
  }
  state.counters["Throughput"] = benchmark::Counter(static_cast<double>(bytes_mapped), benchmark::Counter::kIsRate);
}
BENCHMARK_TEMPLATE(BM_mmap_empty_file, MAP_SHARED)->ArgName("log2_size")->ArgsProduct({log2_1_1M_1G_1P});
BENCHMARK_TEMPLATE(BM_mmap_empty_file, MAP_SHARED | MAP_POPULATE)->ArgName("log2_size")->ArgsProduct({log2_1_1M_1G_1P});

template <int flags>
static void BM_mmap_sparse_file(benchmark::State& state) {
  auto fd = jl::unwrap(jl::tmpfd::unlinked());
  auto map_size = 1L << state.range(0);
  jl::unwrap(jl::truncate(fd.fd(), map_size));
  size_t bytes_mapped = 0;
  for (auto _ : state) {
    auto map = jl::unwrap(jl::unique_mmap<char>::map(map_size, PROT_READ, flags, fd.fd()));
    benchmark::DoNotOptimize(bytes_mapped += map->size());
  }
  state.counters["Throughput"] = benchmark::Counter(static_cast<double>(bytes_mapped), benchmark::Counter::kIsRate);
}
BENCHMARK_TEMPLATE(BM_mmap_sparse_file, MAP_SHARED)->ArgName("log2_size")->ArgsProduct({log2_1_1M_1G_1P});
BENCHMARK_TEMPLATE(BM_mmap_sparse_file, MAP_SHARED | MAP_POPULATE)->ArgName("log2_size")->ArgsProduct({log2_1_1M_1G_1P});

template <int flags>
static void BM_fd_mmap_truncated(benchmark::State& state) {
  auto map_size = 1L << state.range(0);
  size_t bytes_mapped = 0;
  for (auto _ : state) {
    auto fd = jl::unwrap(jl::tmpfd::open());
    jl::unwrap(jl::truncate(fd->fd(), map_size));
    auto map = jl::unwrap(jl::fd_mmap<std::byte>::open(fd.path().c_str(), O_RDWR, flags, 0, map_size));
    for (ssize_t i = 0; i < map_size; i += 512) {
      std::memcpy(map->data() + i, &i, sizeof(i));
    }
    bytes_mapped += map->size();
  }
  state.counters["Throughput"] = benchmark::Counter(static_cast<double>(bytes_mapped), benchmark::Counter::kIsRate);
}
BENCHMARK_TEMPLATE(BM_fd_mmap_truncated, MAP_SHARED)->ArgName("log2_size")->ArgsProduct({log2_4K_1M_128M});
BENCHMARK_TEMPLATE(BM_fd_mmap_truncated, MAP_SHARED | MAP_POPULATE)->ArgName("log2_size")->ArgsProduct({log2_4K_1M_128M});

template <int flags>
static void BM_fd_mmap_allocated(benchmark::State& state) {
  auto map_size = 1L << state.range(0);
  size_t bytes_mapped = 0;
  for (auto _ : state) {
    auto fd = jl::unwrap(jl::tmpfd::open());
    auto map = jl::unwrap(jl::fd_mmap<std::byte>::allocated(fd.path().c_str(), map_size, O_RDWR, flags));
    for (ssize_t i = 0; i < map_size; i += 512) {
      std::memcpy(map->data() + i, &i, sizeof(i));
    }
    bytes_mapped += map->size();
  }
  state.counters["Throughput"] = benchmark::Counter(static_cast<double>(bytes_mapped), benchmark::Counter::kIsRate);
}
BENCHMARK_TEMPLATE(BM_fd_mmap_allocated, MAP_SHARED)->ArgName("log2_size")->ArgsProduct({log2_4K_1M_128M});
BENCHMARK_TEMPLATE(BM_fd_mmap_allocated, MAP_SHARED | MAP_POPULATE)->ArgName("log2_size")->ArgsProduct({log2_4K_1M_128M});

BENCHMARK_MAIN();
