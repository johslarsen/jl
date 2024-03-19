#include <benchmark/benchmark.h>
#include <jl.h>

#include <iostream>
#include <latch>
#include <thread>

auto* configure_arguments(auto* b) {
  return b;
}

std::vector<ssize_t> block_sizes{1, 1024, 4096, 1 << 14, 1 << 16};  // any larger and it blocks!

void BM_readwrite(benchmark::State& state) {
  size_t block_size = state.range(0);
  auto [in, out] = jl::unique_fd::pipes();
  jl::unique_fd devnull(open("/dev/null", O_WRONLY | O_CLOEXEC));

  std::vector<char> buffer(block_size);
  std::span block = buffer;

  size_t bytes_copied = 0;
  for (auto _ : state) {
    auto written = jl::write(*out, block);
    auto read = jl::read(*in, block.subspan(0, written));
    benchmark::DoNotOptimize(bytes_copied += jl::write(*devnull, read));
  }
  state.counters["B/s"] = benchmark::Counter(static_cast<double>(bytes_copied), benchmark::Counter::kIsRate);
}
BENCHMARK(BM_readwrite)->ArgsProduct({{block_sizes}});

void BM_splice(benchmark::State& state) {
  ssize_t block_size = state.range(0);
  auto zero = jl::tmpfd().unlink();
  jl::truncate(*zero, block_size);
  auto [in, out] = jl::unique_fd::pipes();
  jl::unique_fd devnull(open("/dev/null", O_WRONLY | O_CLOEXEC));

  size_t bytes_copied = 0;
  for (auto _ : state) {
    auto written = jl::unwrap(jl::spliceall({*zero, 0}, {*out}, block_size));
    benchmark::DoNotOptimize(bytes_copied += jl::unwrap(jl::spliceall({*in}, {*devnull}, written)));
  }
  state.counters["B/s"] = benchmark::Counter(static_cast<double>(bytes_copied), benchmark::Counter::kIsRate);
}
BENCHMARK(BM_splice)->ArgsProduct({{block_sizes}});

void BM_sendfile(benchmark::State& state) {
  ssize_t block_size = state.range(0);
  auto zero = jl::tmpfd();
  jl::truncate(zero->fd(), block_size);
  jl::unique_fd devnull(open("/dev/null", O_WRONLY | O_CLOEXEC));

  size_t bytes_copied = 0;
  for (auto _ : state) {
    // NOTE: this only copies once, so not really a fair comparison to the other alternatives
    benchmark::DoNotOptimize(bytes_copied += jl::unwrap(jl::sendfileall(*devnull, {zero->fd(), 0}, block_size)));
  }
  state.counters["B/s"] = benchmark::Counter(static_cast<double>(bytes_copied), benchmark::Counter::kIsRate);
}
BENCHMARK(BM_sendfile)->ArgsProduct({{block_sizes}})->ArgsProduct({{1 << 20, 1 << 30, 16UL << 30}});
BENCHMARK_MAIN();
