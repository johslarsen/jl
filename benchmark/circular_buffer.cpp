#include <benchmark/benchmark.h>
#include <jl_posix.h>

#include <deque>
#include <thread>

template <typename Index>
void BM_CircularBufferBytewiseAdvance(benchmark::State& state) {
  jl::CircularBuffer<char, 4 << 10, Index> buf;
  double bytes_read = 0;
  for (auto _ : state) {
    buf.commit_written(buf.peek_back(1));
    bytes_read += buf.commit_read(buf.peek_front(1));
  }
  state.counters["Throughput"] = benchmark::Counter(bytes_read, benchmark::Counter::kIsRate);
}
BENCHMARK_TEMPLATE(BM_CircularBufferBytewiseAdvance, uint32_t);
BENCHMARK_TEMPLATE(BM_CircularBufferBytewiseAdvance, std::atomic<uint32_t>);

void BM_ParallelCircularBufferRWFullData(benchmark::State& state) {
  jl::CircularBuffer<uint8_t, 4 << 10, std::atomic<uint32_t>> buf;
  size_t chunk_size = state.range(0);
  std::jthread writer([&](const std::stop_token& token) {
    while (!token.stop_requested()) {
      auto writeable = buf.peek_back(chunk_size);
      std::ranges::fill(writeable, 1);
      buf.commit_written(std::move(writeable));
    }
  });

  size_t bytes_read = 0;
  for (auto _ : state) {
    const auto readable = buf.peek_front(chunk_size);
    for (const auto& one : readable) benchmark::DoNotOptimize(bytes_read += one);
    buf.commit_read(std::move(readable));
  }
  state.counters["Throughput"] = benchmark::Counter(static_cast<double>(bytes_read), benchmark::Counter::kIsRate);
  writer.request_stop();
}
BENCHMARK(BM_ParallelCircularBufferRWFullData)->ArgName("BurstSize")->ArgsProduct({{1, 16, 17, 256, 1023, 1024, 4096}});

void BM_ParallelCircularBufferRWDataEndpoints(benchmark::State& state) {
  jl::CircularBuffer<uint8_t, 4 << 10, std::atomic<uint32_t>> buf;
  size_t chunk_size = state.range(0);
  std::jthread writer([&](const std::stop_token& token) {
    while (!token.stop_requested()) {
      auto writeable = buf.peek_back(chunk_size);
      if (!writeable.empty()) benchmark::DoNotOptimize(writeable.front() += writeable.back());
      buf.commit_written(std::move(writeable));
    }
  });

  size_t bytes_read = 0;
  for (auto _ : state) {
    const auto readable = buf.peek_front(chunk_size);
    if (!readable.empty()) benchmark::DoNotOptimize(readable.front() += readable.back());
    bytes_read += buf.commit_read(std::move(readable));
  }
  state.counters["Throughput"] = benchmark::Counter(static_cast<double>(bytes_read), benchmark::Counter::kIsRate);
  writer.request_stop();
}
BENCHMARK(BM_ParallelCircularBufferRWDataEndpoints)->ArgName("BurstSize")->ArgsProduct({{1, 16, 17, 256, 1023, 1024, 4096}});

static const std::vector<int64_t> burst_sizes = {/*1, 2, 16, 256,*/ 1 << 10,
                                                 /*4 << 10*/};

template <size_t Capacity, typename Index>
void BM_CircularBufferFillThenEmpty(benchmark::State& state) {
  std::vector<uint8_t> frame(state.range(0));
  jl::CircularBuffer<uint8_t, Capacity, Index> buf;
  size_t bytes_read = 0;
  for (auto _ : state) {
    while (true) {
      if (buf.push_back(frame) == 0) break;
    }
    for (size_t n = 0; (n = buf.fill_from_front(frame)) > 0;) {
      benchmark::DoNotOptimize(frame[0] += frame[n - 1]);
      bytes_read += n;
    }
  }
  state.counters["Throughput"] = benchmark::Counter(static_cast<double>(bytes_read), benchmark::Counter::kIsRate);
}
BENCHMARK_TEMPLATE(BM_CircularBufferFillThenEmpty, 4 << 10, uint32_t)->ArgName("BurstSize")->ArgsProduct({burst_sizes});
BENCHMARK_TEMPLATE(BM_CircularBufferFillThenEmpty, 32 << 10, uint32_t)->ArgName("BurstSize")->ArgsProduct({burst_sizes});
BENCHMARK_TEMPLATE(BM_CircularBufferFillThenEmpty, 256 << 10, uint32_t)->ArgName("BurstSize")->ArgsProduct({burst_sizes});
BENCHMARK_TEMPLATE(BM_CircularBufferFillThenEmpty, 2 << 20, uint32_t)->ArgName("BurstSize")->ArgsProduct({burst_sizes});
BENCHMARK_TEMPLATE(BM_CircularBufferFillThenEmpty, 16 << 20, uint32_t)->ArgName("BurstSize")->ArgsProduct({burst_sizes});
BENCHMARK_TEMPLATE(BM_CircularBufferFillThenEmpty, 128 << 20, uint32_t)->ArgName("BurstSize")->ArgsProduct({burst_sizes});
BENCHMARK_TEMPLATE(BM_CircularBufferFillThenEmpty, 4 << 10, std::atomic<uint32_t>)->ArgName("BurstSize")->ArgsProduct({burst_sizes});
BENCHMARK_TEMPLATE(BM_CircularBufferFillThenEmpty, 32 << 10, std::atomic<uint32_t>)->ArgName("BurstSize")->ArgsProduct({burst_sizes});
BENCHMARK_TEMPLATE(BM_CircularBufferFillThenEmpty, 256 << 10, std::atomic<uint32_t>)->ArgName("BurstSize")->ArgsProduct({burst_sizes});
BENCHMARK_TEMPLATE(BM_CircularBufferFillThenEmpty, 2 << 20, std::atomic<uint32_t>)->ArgName("BurstSize")->ArgsProduct({burst_sizes});
BENCHMARK_TEMPLATE(BM_CircularBufferFillThenEmpty, 16 << 20, std::atomic<uint32_t>)->ArgName("BurstSize")->ArgsProduct({burst_sizes});
BENCHMARK_TEMPLATE(BM_CircularBufferFillThenEmpty, 128 << 20, std::atomic<uint32_t>)->ArgName("BurstSize")->ArgsProduct({burst_sizes});

template <typename Container>
void BM_ContainerFillThenEmptyWith1KiBursts(benchmark::State& state) {
  ssize_t chunk = state.range(0);
  size_t capacity = state.range(1);
  std::vector<uint8_t> frame(chunk);
  Container buf;
  size_t bytes_read = 0;
  for (auto _ : state) {
    while (buf.size() < capacity) {
      buf.insert(buf.end(), frame.begin(), frame.end());
    }
    bytes_read += buf.size();
    while (!buf.empty()) {
      frame.clear();
      frame.insert(frame.begin(), buf.begin(), buf.begin() + chunk);
      benchmark::DoNotOptimize(frame.front() += frame.back());

      buf.erase(buf.begin(), buf.begin() + chunk);
    }
  }
  state.counters["Throughput"] = benchmark::Counter(static_cast<double>(bytes_read), benchmark::Counter::kIsRate);
}
BENCHMARK_TEMPLATE(BM_ContainerFillThenEmptyWith1KiBursts, std::deque<uint8_t>)
    ->ArgNames({"BurstSize", "Capacity"})
    ->ArgsProduct({burst_sizes, {4 << 10, 32 << 10, 256 << 10, 2 << 20, 16 << 20, 128 << 20}});
BENCHMARK_TEMPLATE(BM_ContainerFillThenEmptyWith1KiBursts, std::vector<uint8_t>)
    ->ArgNames({"BurstSize", "Capacity"})
    ->ArgsProduct({burst_sizes, {4 << 10,
                                 32 << 10,
                                 256 << 10,
                                 2 << 20,
                                 /* rest is too slow */}});

BENCHMARK_MAIN();  // NOLINT
