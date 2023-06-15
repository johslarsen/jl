#include <benchmark/benchmark.h>
#include <jl.h>

std::string_view whitespace = " \f\n\r\t\v";

std::string none(1 << 10, 'x');
std::string last = []() {std::string s = none; s.back() = ' '; return s; }();
std::string first = []() {std::string s = none; s.front() = ' '; return s; }();
std::string mid = []() {std::string s = none; s[s.size()/2] = ' '; return s; }();

void BM_FindChar(benchmark::State& state, std::string_view str) {
  auto pos = std::string::npos;
  for (auto _ : state) {
    benchmark::DoNotOptimize(pos = str.find(' '));
  }
}
BENCHMARK_CAPTURE(BM_FindChar, None, none);
BENCHMARK_CAPTURE(BM_FindChar, First, first);
BENCHMARK_CAPTURE(BM_FindChar, Mid, mid);
BENCHMARK_CAPTURE(BM_FindChar, Last, last);

void BM_FindIfIsSpace(benchmark::State& state, std::string_view str) {
  const auto* pos = str.end();
  for (auto _ : state) {
    benchmark::DoNotOptimize(pos = std::find_if(str.begin(), str.end(), [](char c) { return std::isspace(c) != 0; }));
  }
}
BENCHMARK_CAPTURE(BM_FindIfIsSpace, None, none);
BENCHMARK_CAPTURE(BM_FindIfIsSpace, Find, first);
BENCHMARK_CAPTURE(BM_FindIfIsSpace, Mid, mid);
BENCHMARK_CAPTURE(BM_FindIfIsSpace, Last, last);

static inline size_t IsSpaceLoop(std::string_view str) {
  auto size = str.size();
  for (size_t i = 0; i < size; ++i) {
    if (std::isspace(str[i]) != 0) return i;
  }
  return std::string::npos;
}
void BM_IsSpaceLoop(benchmark::State& state, std::string_view str) {
  auto pos = std::string::npos;
  for (auto _ : state) {
    benchmark::DoNotOptimize(pos = IsSpaceLoop(str));
  }
}
BENCHMARK_CAPTURE(BM_IsSpaceLoop, None, none);
BENCHMARK_CAPTURE(BM_IsSpaceLoop, First, first);
BENCHMARK_CAPTURE(BM_IsSpaceLoop, Mid, mid);
BENCHMARK_CAPTURE(BM_IsSpaceLoop, Last, last);

void BM_FindOneOf(benchmark::State& state, std::string_view str) {
  auto pos = std::string::npos;
  for (auto _ : state) {
    benchmark::DoNotOptimize(pos = str.find_first_of(whitespace));
  }
}
BENCHMARK_CAPTURE(BM_FindOneOf, None, none);
BENCHMARK_CAPTURE(BM_FindOneOf, First, first);
BENCHMARK_CAPTURE(BM_FindOneOf, Mid, mid);
BENCHMARK_CAPTURE(BM_FindOneOf, Last, last);

BENCHMARK_MAIN();
