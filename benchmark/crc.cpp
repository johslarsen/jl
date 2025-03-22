#include <benchmark/benchmark.h>
#include <jl.h>

struct crc8_autosar : jl::crc<uint8_t, 0x2f, 0xff, false, 0xff> {};
struct crc8_bluetooth : jl::crc<uint8_t, 0xa7, 0x00, true, 0x00> {};
struct crc16_gsm : jl::crc<uint16_t, 0x1021, 0x0000, false, 0xffff> {};
struct crc32_cksum : jl::crc<uint32_t, 0x04c1'1db7, 0x0, false, 0xffff'ffff> {};
struct crc64_we : jl::crc<uint64_t, 0x42f0'e1eb'a9ea'3693, 0xffff'ffff'ffff'ffff, false, 0xffff'ffff'ffff'ffff> {};
struct crc64_nvme : jl::crc<uint64_t, 0xad93'd235'94c9'35a9, 0x0, true, 0x0> {};

template <typename CRC>
static void BM_CRC_uint64_t(benchmark::State& state) {
  uint64_t bytes_checksummed = 0;
  uint32_t crc = 0;
  for (auto _ : state) {
    bytes_checksummed += sizeof(bytes_checksummed);
    crc ^= CRC::compute(std::as_bytes(std::span(&bytes_checksummed, 1)));
  }
  state.counters["Throughput"] = benchmark::Counter(static_cast<double>(bytes_checksummed), benchmark::Counter::kIsRate);
  state.counters["CRC"] = crc;
}
BENCHMARK_TEMPLATE(BM_CRC_uint64_t, crc8_autosar);
BENCHMARK_TEMPLATE(BM_CRC_uint64_t, crc8_bluetooth);
BENCHMARK_TEMPLATE(BM_CRC_uint64_t, jl::crc16_ccitt);
BENCHMARK_TEMPLATE(BM_CRC_uint64_t, crc16_gsm);
BENCHMARK_TEMPLATE(BM_CRC_uint64_t, crc32_cksum);
BENCHMARK_TEMPLATE(BM_CRC_uint64_t, jl::crc32c);
BENCHMARK_TEMPLATE(BM_CRC_uint64_t, crc64_we);
BENCHMARK_TEMPLATE(BM_CRC_uint64_t, crc64_nvme);

template <typename CRC>
static void BM_CRC_str(benchmark::State& state) {
  const size_t nbyte = state.range(0);
  std::string str(nbyte, 0);

  uint64_t bytes_checksummed = 0;
  if (nbyte < sizeof(bytes_checksummed)) throw std::runtime_error("too small");
  uint32_t crc = 0;
  for (auto _ : state) {
    bytes_checksummed += nbyte;
    std::memcpy(str.data(), &bytes_checksummed, sizeof(bytes_checksummed));

    crc ^= CRC::compute(str);
  }
  state.counters["Throughput"] = benchmark::Counter(static_cast<double>(bytes_checksummed), benchmark::Counter::kIsRate);
  state.counters["CRC"] = crc;
}
static const std::vector<int64_t> str_sizes{128,1024};

BENCHMARK_TEMPLATE(BM_CRC_str, crc8_autosar)->ArgsProduct({str_sizes});
BENCHMARK_TEMPLATE(BM_CRC_str, crc8_bluetooth)->ArgsProduct({str_sizes});
BENCHMARK_TEMPLATE(BM_CRC_str, jl::crc16_ccitt)->ArgsProduct({str_sizes});
BENCHMARK_TEMPLATE(BM_CRC_str, crc16_gsm)->ArgsProduct({str_sizes});
BENCHMARK_TEMPLATE(BM_CRC_str, crc32_cksum)->ArgsProduct({str_sizes});
BENCHMARK_TEMPLATE(BM_CRC_str, jl::crc32c)->ArgsProduct({str_sizes});
BENCHMARK_TEMPLATE(BM_CRC_str, crc64_we)->ArgsProduct({str_sizes});
BENCHMARK_TEMPLATE(BM_CRC_str, crc64_nvme)->ArgsProduct({str_sizes});

BENCHMARK_MAIN();  // NOLINT
