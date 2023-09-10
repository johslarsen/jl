#include <benchmark/benchmark.h>
#include <jl.h>
#include <sys/sendfile.h>

#include <fstream>

auto* configure_arguments(auto* b) {
  return b;
}
#define JL_BENCHMARK_WITH_ARGS(...) BENCHMARK(__VA_ARGS__)                                   \
                                        ->ArgNames({"size", "chunk", "stride"})              \
                                        ->ArgsProduct({{1 << 20}, {1, 64, 1024}, {1, 1024}}) \
                                        ->ArgsProduct({{1 << 30}, {32768, 1 << 20}, {1, 1024, 1 << 20}})

static inline std::pair<jl::tmpfd, jl::unique_fd> open_read_write(off_t size) {
  auto read = jl::tmpfd();
  jl::truncate(read->fd(), size);
  return {std::move(read), jl::unique_fd(open("/dev/null", O_WRONLY | O_CLOEXEC))};
}

static inline std::tuple<ssize_t, ssize_t, ssize_t> args(benchmark::State& state) {
  ssize_t len = state.range(0);
  ssize_t block_size = state.range(1);
  ssize_t stride = state.range(2);
  return {len, block_size, stride};
}

void BM_mmap_ref(benchmark::State& state) {
  auto [len, block_size, stride] = args(state);
  auto [read, write] = open_read_write(len);
  jl::fd_mmap<char> map(std::move(read).unlink());
  std::vector<char> buffer(block_size);

  size_t bytes_read = 0;
  for (auto _ : state) {
    for (off_t pos = 0; pos < len; pos += stride * block_size) {
      benchmark::DoNotOptimize(bytes_read += jl::write(*write, map->subspan(pos, block_size)));
    }
  }
  state.counters["B/s"] = benchmark::Counter(static_cast<double>(bytes_read), benchmark::Counter::kIsRate);
}
JL_BENCHMARK_WITH_ARGS(BM_mmap_ref);

void BM_mmap_copy(benchmark::State& state) {
  auto [len, block_size, stride] = args(state);
  auto [read, write] = open_read_write(len);
  jl::fd_mmap<char> map(std::move(read).unlink());
  std::vector<char> buffer(block_size);

  size_t bytes_read = 0;
  for (off_t pos = 0; pos <= len; pos += block_size) {
    std::copy(map->begin() + pos, map->begin() + pos + block_size, buffer.begin());
    benchmark::DoNotOptimize(bytes_read += jl::write(*write, buffer));
  }
  for (auto _ : state) {
    for (off_t pos = 0; pos < len; pos += stride * block_size) {
      std::copy(map->begin() + pos, map->begin() + pos + block_size, buffer.begin());
      benchmark::DoNotOptimize(bytes_read += jl::write(*write, buffer));
    }
  }
  state.counters["Throughput"] = benchmark::Counter(static_cast<double>(bytes_read), benchmark::Counter::kIsRate);
}
JL_BENCHMARK_WITH_ARGS(BM_mmap_copy);

void BM_sendfile(benchmark::State& state) {
  auto [len, block_size, stride] = args(state);
  auto [read, write] = open_read_write(len);

  size_t bytes_read = 0;
  int fd = read->fd();
  for (auto _ : state) {
    for (off_t pos = 0; pos < len; pos += stride * block_size) {
      off_t pos_copy = pos;
      benchmark::DoNotOptimize(bytes_read += jl::check_rw_error(sendfile(write.fd(), fd, &pos_copy, block_size), "sendfile failed"));
    }
  }
  state.counters["Throughput"] = benchmark::Counter(static_cast<double>(bytes_read), benchmark::Counter::kIsRate);
}
JL_BENCHMARK_WITH_ARGS(BM_sendfile);

void BM_read(benchmark::State& state) {
  auto [len, block_size, stride] = args(state);
  auto [read, write] = open_read_write(len);
  std::vector<char> buffer(block_size);

  size_t bytes_read = 0;
  int fd = read->fd();
  for (auto _ : state) {
    for (off_t pos = 0; pos < len; pos += stride * block_size) {
      lseek(fd, pos, SEEK_SET);
      size_t size = jl::check_rw_error(::read(fd, buffer.data(), block_size), "read failed");
      benchmark::DoNotOptimize(bytes_read += jl::write(*write, {buffer.data(), size}));
    }
  }
  state.counters["Throughput"] = benchmark::Counter(static_cast<double>(bytes_read), benchmark::Counter::kIsRate);
}
JL_BENCHMARK_WITH_ARGS(BM_read);

void BM_pread(benchmark::State& state) {
  auto [len, block_size, stride] = args(state);
  auto [read, write] = open_read_write(len);
  std::vector<char> buffer(block_size);

  size_t bytes_read = 0;
  int fd = read->fd();
  for (auto _ : state) {
    for (off_t pos = 0; pos < len; pos += stride * block_size) {
      size_t size = jl::check_rw_error(pread(fd, buffer.data(), block_size, pos), "pread failed");
      benchmark::DoNotOptimize(bytes_read += jl::write(*write, {buffer.data(), size}));
    }
  }
  state.counters["Throughput"] = benchmark::Counter(static_cast<double>(bytes_read), benchmark::Counter::kIsRate);
}
JL_BENCHMARK_WITH_ARGS(BM_pread);

void BM_fread(benchmark::State& state) {
  auto [len, block_size, stride] = args(state);
  auto [read, write] = open_read_write(len);
  std::vector<char> buffer(block_size);

  FILE* fp = fopen(read.path().c_str(), "re");
  if (fp == nullptr) throw jl::errno_as_error("fopen failed");

  size_t bytes_read = 0;
  for (auto _ : state) {
    rewind(fp);
    for (ssize_t pos = 0; pos < len; pos += stride * block_size) {
      benchmark::DoNotOptimize(bytes_read += fread(buffer.data(), 1, block_size, fp));
    }
  }
  state.counters["Throughput"] = benchmark::Counter(static_cast<double>(bytes_read), benchmark::Counter::kIsRate);
}
JL_BENCHMARK_WITH_ARGS(BM_fread);

void BM_ifstream_read(benchmark::State& state) {
  auto [len, block_size, stride] = args(state);
  auto [read, write] = open_read_write(len);
  std::vector<char> buffer(block_size);

  size_t bytes_read = 0;
  std::ifstream file(read.path());
  for (auto _ : state) {
    file.seekg(0, std::ios::beg);
    for (off_t pos = 0; pos < len; pos += stride * block_size) {
      file.read(buffer.data(), block_size);
      benchmark::DoNotOptimize(bytes_read += block_size);
    }
  }
  state.counters["Throughput"] = benchmark::Counter(static_cast<double>(bytes_read), benchmark::Counter::kIsRate);
}
JL_BENCHMARK_WITH_ARGS(BM_ifstream_read);
BENCHMARK_MAIN();
