#include <benchmark/benchmark.h>
#include <jl.h>

#include <ranges>

void BM_chunk_manually(benchmark::State& state) {
  std::vector<char> data(state.range(0));
  size_t n = state.range(1);
  size_t bytes_processed = 0;
  for (auto _ : state) {
    for (size_t i = 0; i < data.size(); i += n) {
      bytes_processed += std::span(data.data() + i, std::min(n, data.size() - i)).size();
    }
  }
  state.counters["Throughput"] = benchmark::Counter(static_cast<double>(bytes_processed), benchmark::Counter::kIsRate);
}
BENCHMARK(BM_chunk_manually)->ArgNames({"Size", "Chunk"})->ArgsProduct({{(1 << 30) - 1}, {1023, 1024, 1 << 20}});

void BM_remove_prefix(benchmark::State& state) {
  std::vector<char> data(state.range(0));
  size_t n = state.range(1);
  size_t bytes_processed = 0;
  for (auto _ : state) {
    std::span span(data);
    while (!span.empty()) {
      auto chunk = span.subspan(0, std::min(n, span.size()));
      bytes_processed += chunk.size();
      span = span.subspan(chunk.size());
    }
  }
  state.counters["Throughput"] = benchmark::Counter(static_cast<double>(bytes_processed), benchmark::Counter::kIsRate);
}
BENCHMARK(BM_remove_prefix)->ArgNames({"Size", "Chunk"})->ArgsProduct({{(1 << 30) - 1}, {1023, 1024, 1 << 20}});

void BM_chunked_span_iterate(benchmark::State& state) {
  std::vector<char> data(state.range(0));
  size_t n = state.range(1);
  size_t bytes_processed = 0;
  for (auto _ : state) {
    for (const auto& chunk : jl::chunked(std::span(data), n)) {
      bytes_processed += chunk.size();
    }
  }
  state.counters["Throughput"] = benchmark::Counter(static_cast<double>(bytes_processed), benchmark::Counter::kIsRate);
}
BENCHMARK(BM_chunked_span_iterate)->ArgNames({"Size", "Chunk"})->ArgsProduct({{(1 << 30) - 1}, {1023, 1024, 1 << 20}});

void BM_chunked_span_copy(benchmark::State& state) {
  std::vector<char> data(state.range(0));
  size_t n = state.range(1);
  size_t bytes_processed = 0;
  for (auto _ : state) {
    for (const auto& chunk : jl::chunked(std::span(data), n)) {
      bytes_processed += std::copy(chunk.begin(), chunk.end(), data.begin()) - data.begin();
    }
  }
  state.counters["Throughput"] = benchmark::Counter(static_cast<double>(bytes_processed), benchmark::Counter::kIsRate);
}
BENCHMARK(BM_chunked_span_copy)->ArgNames({"Size", "Chunk"})->ArgsProduct({{(1 << 30) - 1}, {1023, 1024, 1 << 20}});

void BM_chunked_view_iterate(benchmark::State& state) {
  std::vector<char> data(state.range(0));
  size_t n = state.range(1);
  size_t bytes_processed = 0;
  for (auto _ : state) {
    for (const auto& chunk : data | std::views::chunk(n)) {
      bytes_processed += chunk.size();
    }
  }
  state.counters["Throughput"] = benchmark::Counter(static_cast<double>(bytes_processed), benchmark::Counter::kIsRate);
}
BENCHMARK(BM_chunked_view_iterate)->ArgNames({"Size", "Chunk"})->ArgsProduct({{(1 << 30) - 1}, {1023, 1024, 1 << 20}});

void BM_chunked_view_copy(benchmark::State& state) {
  std::vector<char> data(state.range(0));
  size_t n = state.range(1);
  size_t bytes_processed = 0;
  for (auto _ : state) {
    for (const auto& chunk : data | std::views::chunk(n)) {
      bytes_processed += std::copy(chunk.begin(), chunk.end(), data.begin()) - data.begin();
    }
  }
  state.counters["Throughput"] = benchmark::Counter(static_cast<double>(bytes_processed), benchmark::Counter::kIsRate);
}
BENCHMARK(BM_chunked_view_copy)->ArgNames({"Size", "Chunk"})->ArgsProduct({{(1 << 30) - 1}, {1023, 1024, 1 << 20}});

BENCHMARK_MAIN();
