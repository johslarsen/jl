#include <benchmark/benchmark.h>
#include <jl.h>

#include <deque>

template <typename Index>
void BM_CircularBufferBytewiseAdvance(benchmark::State& state) {
  jl::CircularBuffer<char, 4 << 10, Index> buf;
  while (state.KeepRunning()) {
    buf.commit_written(buf.peek_back(1));
    buf.commit_read(buf.peek_front(1));
  }
}
BENCHMARK_TEMPLATE(BM_CircularBufferBytewiseAdvance, uint32_t);
BENCHMARK_TEMPLATE(BM_CircularBufferBytewiseAdvance, std::atomic<uint32_t>);

template <size_t Capacity, typename Index>
void BM_CircularBufferCopyIntoAndCopyBack(benchmark::State& state) {
  std::vector<char> frame(state.range(0));
  jl::CircularBuffer<char, Capacity, Index> buf;
  while (state.KeepRunning()) {
    while (true) {
      if (buf.push_back(frame) == 0) break;
    }
    while (true) {
      if (buf.fill_from_front(frame) == 0) break;
    }
  }
}
BENCHMARK_TEMPLATE(BM_CircularBufferCopyIntoAndCopyBack, 4 << 10, uint32_t)->Arg(1 << 10)->Arg((1 << 10) - 1);
BENCHMARK_TEMPLATE(BM_CircularBufferCopyIntoAndCopyBack, 32 << 10, uint32_t)->Arg(1 << 10)->Arg((1 << 10) - 1);
BENCHMARK_TEMPLATE(BM_CircularBufferCopyIntoAndCopyBack, 256 << 10, uint32_t)->Arg(1 << 10)->Arg((1 << 10) - 1);
BENCHMARK_TEMPLATE(BM_CircularBufferCopyIntoAndCopyBack, 2 << 20, uint32_t)->Arg(1 << 10)->Arg((1 << 10) - 1);
BENCHMARK_TEMPLATE(BM_CircularBufferCopyIntoAndCopyBack, 16 << 20, uint32_t)->Arg(1 << 10)->Arg((1 << 10) - 1);
BENCHMARK_TEMPLATE(BM_CircularBufferCopyIntoAndCopyBack, 128 << 20, uint32_t)->Arg(1 << 10)->Arg((1 << 10) - 1);
BENCHMARK_TEMPLATE(BM_CircularBufferCopyIntoAndCopyBack, 4 << 10, std::atomic<uint32_t>)->Arg(1 << 10)->Arg((1 << 10) - 1);
BENCHMARK_TEMPLATE(BM_CircularBufferCopyIntoAndCopyBack, 32 << 10, std::atomic<uint32_t>)->Arg(1 << 10)->Arg((1 << 10) - 1);
BENCHMARK_TEMPLATE(BM_CircularBufferCopyIntoAndCopyBack, 256 << 10, std::atomic<uint32_t>)->Arg(1 << 10)->Arg((1 << 10) - 1);
BENCHMARK_TEMPLATE(BM_CircularBufferCopyIntoAndCopyBack, 2 << 20, std::atomic<uint32_t>)->Arg(1 << 10)->Arg((1 << 10) - 1);
BENCHMARK_TEMPLATE(BM_CircularBufferCopyIntoAndCopyBack, 16 << 20, std::atomic<uint32_t>)->Arg(1 << 10)->Arg((1 << 10) - 1);
BENCHMARK_TEMPLATE(BM_CircularBufferCopyIntoAndCopyBack, 128 << 20, std::atomic<uint32_t>)->Arg(1 << 10)->Arg((1 << 10) - 1);

template<typename Container>
void BM_ContainerCopyIntoAndCopyBack(benchmark::State& state) {
  ssize_t chunk = 1 << 10;
  std::vector<char> frame(chunk);
  size_t capacity = state.range(0);
  Container buf;
  while (state.KeepRunning()) {
    while (buf.size() < capacity) {
      buf.insert(buf.end(), frame.begin(), frame.end());
    }
    while (!buf.empty()) {
      frame.clear();
      frame.insert(frame.begin(), buf.begin(), buf.begin() + chunk);
      buf.erase(buf.begin(), buf.begin() + chunk);
    }
  }
}
BENCHMARK_TEMPLATE(BM_ContainerCopyIntoAndCopyBack, std::deque<char>)->Range(4 << 10, 128 << 20);
BENCHMARK_TEMPLATE(BM_ContainerCopyIntoAndCopyBack, std::vector<char>)->Range(4 << 10, 10 << 20);
;

BENCHMARK_MAIN();
