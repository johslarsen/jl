#include <benchmark/benchmark.h>
#include <jl.h>

#include <deque>

template <typename Index>
void BM_RingBufferBytewiseAdvance(benchmark::State& state) {
  jl::RingBuffer<4 << 10, Index> buf;
  while (state.KeepRunning()) {
    buf.commit_written(buf.peek_back(1));
    buf.commit_read(buf.peek_front(1));
  }
}
BENCHMARK_TEMPLATE(BM_RingBufferBytewiseAdvance, uint32_t);
BENCHMARK_TEMPLATE(BM_RingBufferBytewiseAdvance, std::atomic<uint32_t>);

template <size_t Capacity, typename Index>
void BM_RingBufferCopyIntoAndCopyBack(benchmark::State& state) {
  std::vector<uint8_t> frame(state.range(0));
  jl::RingBuffer<Capacity, Index> buf;
  while (state.KeepRunning()) {
    while (true) {
      if (buf.push_back(frame) == 0) break;
    }
    while (true) {
      if (buf.fill_from_front(frame) == 0) break;
    }
  }
}
BENCHMARK_TEMPLATE(BM_RingBufferCopyIntoAndCopyBack, 4 << 10, uint32_t)->Arg(1 << 10)->Arg((1 << 10) - 1);
BENCHMARK_TEMPLATE(BM_RingBufferCopyIntoAndCopyBack, 32 << 10, uint32_t)->Arg(1 << 10)->Arg((1 << 10) - 1);
BENCHMARK_TEMPLATE(BM_RingBufferCopyIntoAndCopyBack, 256 << 10, uint32_t)->Arg(1 << 10)->Arg((1 << 10) - 1);
BENCHMARK_TEMPLATE(BM_RingBufferCopyIntoAndCopyBack, 2 << 20, uint32_t)->Arg(1 << 10)->Arg((1 << 10) - 1);
BENCHMARK_TEMPLATE(BM_RingBufferCopyIntoAndCopyBack, 16 << 20, uint32_t)->Arg(1 << 10)->Arg((1 << 10) - 1);
BENCHMARK_TEMPLATE(BM_RingBufferCopyIntoAndCopyBack, 128 << 20, uint32_t)->Arg(1 << 10)->Arg((1 << 10) - 1);
BENCHMARK_TEMPLATE(BM_RingBufferCopyIntoAndCopyBack, 4 << 10, std::atomic<uint32_t>)->Arg(1 << 10)->Arg((1 << 10) - 1);
BENCHMARK_TEMPLATE(BM_RingBufferCopyIntoAndCopyBack, 32 << 10, std::atomic<uint32_t>)->Arg(1 << 10)->Arg((1 << 10) - 1);
BENCHMARK_TEMPLATE(BM_RingBufferCopyIntoAndCopyBack, 256 << 10, std::atomic<uint32_t>)->Arg(1 << 10)->Arg((1 << 10) - 1);
BENCHMARK_TEMPLATE(BM_RingBufferCopyIntoAndCopyBack, 2 << 20, std::atomic<uint32_t>)->Arg(1 << 10)->Arg((1 << 10) - 1);
BENCHMARK_TEMPLATE(BM_RingBufferCopyIntoAndCopyBack, 16 << 20, std::atomic<uint32_t>)->Arg(1 << 10)->Arg((1 << 10) - 1);
BENCHMARK_TEMPLATE(BM_RingBufferCopyIntoAndCopyBack, 128 << 20, std::atomic<uint32_t>)->Arg(1 << 10)->Arg((1 << 10) - 1);

void BM_DequeCopyIntoAndCopyBack(benchmark::State& state) {
  std::vector<uint8_t> frame(1 << 10);
  size_t capacity = state.range(0);
  std::deque<uint8_t> buf;
  while (state.KeepRunning()) {
    while (buf.size() < capacity) {
      buf.insert(buf.end(), frame.begin(), frame.end());
    }
    while (!buf.empty()) {
      frame.clear();
      frame.insert(frame.begin(), buf.begin(), buf.begin() + (1 << 10));
      buf.erase(buf.begin(), buf.begin() + (1 << 10));
    }
  }
}
BENCHMARK(BM_DequeCopyIntoAndCopyBack)->Range(4 << 10, 128 << 20);
;

void BM_VectorCopyIntoAndCopyBack(benchmark::State& state) {
  std::vector<uint8_t> frame(1 << 10);
  std::vector<uint8_t> buf;
  size_t capacity = state.range(0);
  buf.reserve(capacity);
  while (state.KeepRunning()) {
    while (buf.size() < capacity) {
      buf.insert(buf.end(), frame.begin(), frame.end());
    }
    while (!buf.empty()) {
      frame.clear();
      frame.insert(frame.begin(), buf.begin(), buf.begin() + (1 << 10));
      buf.erase(buf.begin(), buf.begin() + frame.size());
      buf.resize(buf.size() - frame.size());
    }
  }
}
BENCHMARK(BM_VectorCopyIntoAndCopyBack)->Range(4 << 10, 10 << 20);

BENCHMARK_MAIN();
