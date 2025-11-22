#include <benchmark/benchmark.h>
#include <jl.h>

#include <thread>

constexpr size_t Capacity = 0x7fff'ffff'ffff'ffff;

template <class T>
void BM_Singlethreaded(benchmark::State& state) {
  jl::RingIndex<T, Capacity> fifo;
  size_t steps = 0;
  for (auto _ : state) {  // simulate a FIFO queue
    auto [write, available] = fifo.write_free(1);
    if (available > 0) {
      benchmark::DoNotOptimize(steps = write % Capacity);
      fifo.store_write(write + 1);
    }
    auto [read, filled] = fifo.read_filled(1);
    if (filled > 0) {
      benchmark::DoNotOptimize(steps = read % Capacity);
      fifo.store_read(read + 1);
    }
  }
  state.counters["Throughput"] = benchmark::Counter(static_cast<double>(steps), benchmark::Counter::kIsRate);
}
BENCHMARK_TEMPLATE(BM_Singlethreaded, uint64_t);
BENCHMARK_TEMPLATE(BM_Singlethreaded, std::atomic<uint64_t>);

void BM_Multithreaded(benchmark::State& state) {
  jl::RingIndex<std::atomic<uint64_t>, Capacity> fifo;

  std::jthread consumer([&fifo] {
    while (true) {
      auto [read, available] = fifo.read_filled(1);
      if (available) {
        if (read + available == Capacity) break;  // signal for us to stop
        fifo.store_read(read + 1);
      }
    }
  });

  size_t steps = 0;
  for (auto _ : state) {  // simulate a FIFO queue
    auto [write, available] = fifo.write_free(1);
    if (available > 0) {
      benchmark::DoNotOptimize(steps = write % Capacity);
      fifo.store_write(write + 1);
    }
  }
  state.PauseTiming();
  state.counters["Throughput"] = benchmark::Counter(static_cast<double>(steps), benchmark::Counter::kIsRate);
  fifo.store_write(Capacity);
}
BENCHMARK(BM_Multithreaded);

void BM_MultithreadedEagerConsumer(benchmark::State& state) {
  jl::RingIndex<std::atomic<uint64_t>, Capacity> fifo;

  std::jthread consumer([&fifo] {
    while (true) {
      auto [read, available] = fifo.read_filled();
      fifo.store_read(read + available);
      if (read + available == Capacity) break;  // signal for us to stop
    }
  });

  size_t steps = 0;
  for (auto _ : state) {  // simulate a FIFO queue
    auto [write, available] = fifo.write_free(1);
    if (available > 0) {
      benchmark::DoNotOptimize(steps = write % Capacity);
      fifo.store_write(write + 1);
    }
  }
  state.PauseTiming();
  state.counters["Throughput"] = benchmark::Counter(static_cast<double>(steps), benchmark::Counter::kIsRate);
  fifo.store_write(Capacity);
}
BENCHMARK(BM_MultithreadedEagerConsumer);

void BM_MultithreadedEagerProducer(benchmark::State& state) {
  jl::RingIndex<std::atomic<uint64_t>, Capacity> fifo;

  std::jthread consumer([&fifo] {
    while (true) {
      auto [read, available] = fifo.read_filled(1);
      fifo.store_read(read + 1);
      if (read + available == Capacity) break;  // signal for us to stop
    }
  });

  size_t steps = 0;
  for (auto _ : state) {  // simulate a FIFO queue
    auto [write, available] = fifo.write_free(256);
    if (available > 0) {
      benchmark::DoNotOptimize(steps = write % Capacity);
      fifo.store_write(write + 256);
    }
  }
  state.PauseTiming();
  state.counters["Throughput"] = benchmark::Counter(static_cast<double>(steps) / 256, benchmark::Counter::kIsRate);
  fifo.store_write(Capacity);
}
BENCHMARK(BM_MultithreadedEagerProducer);

BENCHMARK_MAIN();  // NOLINT
