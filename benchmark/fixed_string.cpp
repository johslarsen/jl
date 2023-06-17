#include <benchmark/benchmark.h>
#include <jl.h>

#define digits_32 "01234567890123456789012345678912"
constexpr std::string_view rev_32 = "21987654321098765432109876543210";

template <jl::fixed_string Key>
struct charwise {
  static size_t sum_after_xor(std::string_view plaintext) {
    size_t sum = 0;
    for (size_t i = 0; i < Key.chars.size(); ++i) {
      sum += plaintext[i] ^ Key.chars[i];
    }
    return sum;
  }
};

void BM_FixedStringXorSum(benchmark::State& state, std::string_view str) {
  size_t sum = -1;
  for (auto _ : state) {
    benchmark::DoNotOptimize(sum = charwise<digits_32>::sum_after_xor(str));
  }
  state.counters["Sum"] = static_cast<double>(sum);
}
BENCHMARK_CAPTURE(BM_FixedStringXorSum, c32, rev_32);

size_t sum_after_xor(std::string_view key, std::string_view plaintext) {
  size_t sum = 0;
  for (size_t i = 0; i < key.size(); ++i) {
    sum += plaintext[i] ^ key[i];
  }
  return sum;
}

void BM_StringViewXorSum(benchmark::State& state, std::string_view str, std::string_view key) {
  size_t sum = -1;
  for (auto _ : state) {
    benchmark::DoNotOptimize(sum = sum_after_xor(key, str));
  }
  state.counters["Sum"] = static_cast<double>(sum);
}
BENCHMARK_CAPTURE(BM_StringViewXorSum, c32, rev_32, digits_32);

BENCHMARK_MAIN();
