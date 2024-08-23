#include <benchmark/benchmark.h>
#include <jl_curl.h>

void BM_CURLMReleaseAndReaddHandles(benchmark::State& state) {
  std::vector<std::pair<jl::curl::easy, CURL*>> curls(state.range(0));
  jl::curl::curlm curlm;
  for (auto _ : state) {
    for (auto& [curl, ptr] : curls) {
      ptr = *curlm.add(std::move(curl));
    }
    for (auto& [curl, ptr] : curls) {
      curl = curlm.release(ptr);
    }
  }
}
BENCHMARK(BM_CURLMReleaseAndReaddHandles)->Range(1, 4096);

void BM_CURLMEmptyFileRequests(benchmark::State& state) {
  std::vector<std::future<std::pair<CURLcode, jl::curl::easy>>> results(state.range(0));
  jl::curl::multi curlm;
  for (auto& result : results) {
    result = curlm.start(jl::curl::easy().request("file:///dev/null", jl::curl::discard_body));
  }

  for (auto _ : state) {
    curlm.action();
    for (auto& result : results) {
      auto [_, curl] = result.get();
      result = curlm.start(std::move(curl));
    }
  }
}
BENCHMARK(BM_CURLMEmptyFileRequests)->Range(1, 4096);

BENCHMARK_MAIN();  // NOLINT
