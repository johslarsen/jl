#include <benchmark/benchmark.h>
#include <jl.h>

#include <random>

template <typename Engine>
static void BM_random_number_engine(benchmark::State& state) {
  Engine gen(std::random_device{}());

  size_t bytes_generated = 0;
  size_t checksum = 0;
  for (auto _ : state) {
    checksum ^= gen();
    bytes_generated += sizeof(typename Engine::result_type);
  }
  state.counters["Throughput"] = benchmark::Counter(static_cast<double>(bytes_generated), benchmark::Counter::kIsRate);
  state.counters["Final"] = gen();
}
BENCHMARK_TEMPLATE(BM_random_number_engine, std::mt19937);
BENCHMARK_TEMPLATE(BM_random_number_engine, std::mt19937_64);

static void BM_urandom_into(benchmark::State& state) {
  std::array<std::byte, 1024> buffer{};
  std::mt19937_64 gen(std::random_device{}());

  size_t bytes_generated = 0;
  for (auto _ : state) {
    jl::urandom_into<int64_t>(std::span(buffer), gen);
    bytes_generated += buffer.size();
  }
  state.counters["Throughput"] = benchmark::Counter(static_cast<double>(bytes_generated), benchmark::Counter::kIsRate);

  double final = 0;
  std::memcpy(&final, buffer.data(), sizeof(final));
  state.counters["Final"] = final;
}
BENCHMARK(BM_urandom_into);

BENCHMARK_MAIN();  // NOLINT
