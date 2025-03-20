#include <benchmark/benchmark.h>
#include <jl.h>

void BM_CRC32_CCITT(benchmark::State& state) {
  size_t bytes_checksummed = 0;
  uint32_t crc = 0;
  for (auto _ : state) {
    bytes_checksummed += sizeof(bytes_checksummed);
    crc ^= jl::crc16_ccitt::compute(std::as_bytes(std::span(&bytes_checksummed, 1)));
  }
  state.counters["Throughput"] = benchmark::Counter(static_cast<double>(bytes_checksummed), benchmark::Counter::kIsRate);
  state.counters["CRC"] = crc;
}
BENCHMARK(BM_CRC32_CCITT);

BENCHMARK_MAIN();  // NOLINT
