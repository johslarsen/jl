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
  jl::curl::multi curlm;
  for (long i = 0; i < state.range(0); ++i) {
    curlm.send(jl::curl::easy().request("file:///dev/null", jl::curl::discard_body));
  }

  for (auto _ : state) {
    curlm.action();
    for (std::optional<std::pair<CURLcode, jl::curl::easy>> r; (r = curlm.pop_response());) {
      curlm.send(std::move(r->second));
    }
  }
}
BENCHMARK(BM_CURLMEmptyFileRequests)->Range(1, 4096);

BENCHMARK_MAIN();  // NOLINT
