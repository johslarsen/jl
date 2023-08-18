#include <benchmark/benchmark.h>
#include <jl.h>

constexpr std::string_view whitespace = " \f\n\r\t\v";

const std::string none(1 << 10, 'x');
const std::string last = []() {std::string s = none; s.back() = ' '; return s; }();
const std::string first = []() {std::string s = none; s.front() = ' '; return s; }();
const std::string mid = []() {std::string s = none; s[s.size()/2] = ' '; return s; }();

void BM_FindChar(benchmark::State& state, std::string_view str) {
  auto pos = std::string::npos;
  for (auto _ : state) {
    benchmark::DoNotOptimize(pos = str.find(' '));  // uses hand optimized memchr under the hood
  }
  state.counters["Pos"] = static_cast<double>(pos);
}
BENCHMARK_CAPTURE(BM_FindChar, None, none);
BENCHMARK_CAPTURE(BM_FindChar, First, first);
BENCHMARK_CAPTURE(BM_FindChar, Mid, mid);
BENCHMARK_CAPTURE(BM_FindChar, Last, last);

void BM_FindCharLoop(benchmark::State& state, std::string_view str) {
  auto pos = std::string::npos;
  for (auto _ : state) {
    benchmark::DoNotOptimize(pos = [&str]() {
      for (size_t i = 0; i < str.size(); i++) {
        if (str[i] == ' ') return i;
      }
      return std::string::npos;
    }());
  }
  state.counters["Pos"] = static_cast<double>(pos);
}
BENCHMARK_CAPTURE(BM_FindCharLoop, None, none);
BENCHMARK_CAPTURE(BM_FindCharLoop, First, first);
BENCHMARK_CAPTURE(BM_FindCharLoop, Mid, mid);
BENCHMARK_CAPTURE(BM_FindCharLoop, Last, last);

void BM_FindCharAlgorithm(benchmark::State& state, std::string_view str) {
  const char* pos = str.end();
  for (auto _ : state) {
    benchmark::DoNotOptimize(pos = std::find(str.begin(), str.end(), ' '));
  }
  state.counters["Pos"] = static_cast<double>(pos - str.begin());
}
BENCHMARK_CAPTURE(BM_FindCharAlgorithm, None, none);
BENCHMARK_CAPTURE(BM_FindCharAlgorithm, First, first);
BENCHMARK_CAPTURE(BM_FindCharAlgorithm, Mid, mid);
BENCHMARK_CAPTURE(BM_FindCharAlgorithm, Last, last);

void BM_FindIfIsSpace(benchmark::State& state, std::string_view str) {
  const auto* pos = str.end();
  for (auto _ : state) {
    benchmark::DoNotOptimize(pos = std::find_if(str.begin(), str.end(), [](char c) { return std::isspace(c) != 0; }));
  }
  state.counters["Pos"] = static_cast<double>(pos - str.begin());
}
BENCHMARK_CAPTURE(BM_FindIfIsSpace, None, none);
BENCHMARK_CAPTURE(BM_FindIfIsSpace, First, first);
BENCHMARK_CAPTURE(BM_FindIfIsSpace, Mid, mid);
BENCHMARK_CAPTURE(BM_FindIfIsSpace, Last, last);

void BM_IsSpaceLoop(benchmark::State& state, std::string_view str) {
  auto pos = std::string::npos;
  for (auto _ : state) {
    benchmark::DoNotOptimize(pos = [&str]() {
      for (size_t i = 0; i < str.size(); i++) {
        if (std::isspace(str[i]) != 0) return i;
      }
      return std::string::npos;
    }());
  }
  state.counters["Pos"] = static_cast<double>(pos);
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
  state.counters["Pos"] = static_cast<double>(pos);
}
BENCHMARK_CAPTURE(BM_FindOneOf, None, none);
BENCHMARK_CAPTURE(BM_FindOneOf, First, first);
BENCHMARK_CAPTURE(BM_FindOneOf, Mid, mid);
BENCHMARK_CAPTURE(BM_FindOneOf, Last, last);

template <jl::fixed_string Needles>
struct DepthFirstMatcher {
  static size_t FindOneOf(std::string_view str) {
    for (auto n : Needles.chars) {
      if (auto pos = str.find(n); pos != std::string::npos) return pos;
    }
    return std::string::npos;
  }
};

void BM_FindOneOfDepthFirst_FirstNeedle(benchmark::State& state, std::string_view str) {
  auto pos = std::string::npos;
  for (auto _ : state) {
    benchmark::DoNotOptimize(pos = DepthFirstMatcher<" \t\n\r\f\v">::FindOneOf(str));
  }
  state.counters["Pos"] = static_cast<double>(pos);
}
BENCHMARK_CAPTURE(BM_FindOneOfDepthFirst_FirstNeedle, None, none);
BENCHMARK_CAPTURE(BM_FindOneOfDepthFirst_FirstNeedle, First, first);
BENCHMARK_CAPTURE(BM_FindOneOfDepthFirst_FirstNeedle, Mid, mid);
BENCHMARK_CAPTURE(BM_FindOneOfDepthFirst_FirstNeedle, Last, last);

void BM_FindOneOfDepthFirst_LastNeedle(benchmark::State& state, std::string_view str) {
  auto pos = std::string::npos;
  for (auto _ : state) {
    benchmark::DoNotOptimize(pos = DepthFirstMatcher<"\t\n\r\f\v ">::FindOneOf(str));
  }
  state.counters["Pos"] = static_cast<double>(pos);
}
BENCHMARK_CAPTURE(BM_FindOneOfDepthFirst_LastNeedle, None, none);
BENCHMARK_CAPTURE(BM_FindOneOfDepthFirst_LastNeedle, First, first);
BENCHMARK_CAPTURE(BM_FindOneOfDepthFirst_LastNeedle, Mid, mid);
BENCHMARK_CAPTURE(BM_FindOneOfDepthFirst_LastNeedle, Last, last);

BENCHMARK_MAIN();  // NOLINT
