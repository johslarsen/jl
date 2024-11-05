#include <benchmark/benchmark.h>
#include <jl_posix.h>
#include <sys/sendfile.h>

#include <fstream>

auto* configure_arguments(auto* b) {
  return b;
}
#define JL_BENCHMARK_WITHOUT_STRIDE(...) BENCHMARK_TEMPLATE(__VA_ARGS__)         \
                                             ->ArgNames({"size", "chunk"})       \
                                             ->ArgsProduct({{1 << 20}, {1, 64}}) \
                                             ->ArgsProduct({{1 << 30}, {4096, 1 << 15, 1 << 20}})
#define JL_BENCHMARK_WITH_ARGS(...)            \
  JL_BENCHMARK_WITHOUT_STRIDE(__VA_ARGS__, 1); \
  JL_BENCHMARK_WITHOUT_STRIDE(__VA_ARGS__, 1024)

static inline std::pair<jl::tmpfd, jl::unique_fd> open_read_write(off_t size) {
  auto read = jl::tmpfd();
  jl::unwrap(jl::truncate(read->fd(), size));

  // it is more realistic and fair if the data is not just zero pages, because
  // those are cheap for the OS to copy into user-space buffers (e.g. read).
  // also sparse files on tmpfs is strange, and this causes lots of unnecessary
  // page faulting when zeros is actually read back from the file:
  // https://unix.stackexchange.com/a/470363
  for (off_t pos = 0; pos < size; pos += 512) {
    pwrite(read->fd(), &pos, sizeof(pos), pos);
  }

  return {std::move(read), jl::unique_fd(open("/dev/null", O_WRONLY | O_CLOEXEC))};
}

static inline std::tuple<ssize_t, ssize_t> args(benchmark::State& state) {
  ssize_t len = state.range(0);
  ssize_t block_size = state.range(1);
  return {len, block_size};
}

template <size_t stride>
void BM_sendfile(benchmark::State& state) {
  auto [len, block_size] = args(state);
  auto [read, write] = open_read_write(len);

  size_t bytes_read = 0;
  int fd = read->fd();
  for (auto _ : state) {
    for (off_t pos = 0; pos < len; pos += stride * block_size) {
      off_t pos_copy = pos;
      auto size = jl::check_rw_error(sendfile(write.fd(), fd, &pos_copy, block_size), "sendfile failed");
      bytes_read += size;
    }
  }
  state.counters["Throughput"] = benchmark::Counter(static_cast<double>(bytes_read), benchmark::Counter::kIsRate);
}
JL_BENCHMARK_WITH_ARGS(BM_sendfile);

template <size_t stride>
void BM_mmap_ref(benchmark::State& state) {
  auto [len, block_size] = args(state);
  auto [read, write] = open_read_write(len);
  jl::fd_mmap<char> map(std::move(read).unlink());
  std::vector<char> buffer(block_size);

  size_t bytes_read = 0;
  for (auto _ : state) {
    for (off_t pos = 0; pos < len; pos += stride * block_size) {
      bytes_read += jl::write(*write, map->subspan(pos, block_size));
    }
  }
  state.counters["Throughput"] = benchmark::Counter(static_cast<double>(bytes_read), benchmark::Counter::kIsRate);
}
JL_BENCHMARK_WITH_ARGS(BM_mmap_ref);

template <size_t stride>
void BM_mmap_copy(benchmark::State& state) {
  auto [len, block_size] = args(state);
  auto [read, write] = open_read_write(len);
  jl::fd_mmap<char> map(std::move(read).unlink());
  std::vector<char> buffer(block_size);

  size_t bytes_read = 0;
  for (auto _ : state) {
    for (off_t pos = 0; pos < len; pos += stride * block_size) {
      auto base = map->begin() + pos;
      std::copy(base, base + block_size, buffer.begin());
      bytes_read += jl::write(*write, buffer);
    }
  }
  state.counters["Throughput"] = benchmark::Counter(static_cast<double>(bytes_read), benchmark::Counter::kIsRate);
}
JL_BENCHMARK_WITH_ARGS(BM_mmap_copy);

template <size_t stride>
void BM_pread(benchmark::State& state) {
  auto [len, block_size] = args(state);
  auto [read, write] = open_read_write(len);
  std::vector<char> buffer(block_size);

  size_t bytes_read = 0;
  int fd = read->fd();
  for (auto _ : state) {
    for (off_t pos = 0; pos < len; pos += stride * block_size) {
      size_t size = jl::check_rw_error(pread(fd, buffer.data(), block_size, pos), "pread failed");
      bytes_read += jl::write(*write, {buffer.data(), size});
    }
  }
  state.counters["Throughput"] = benchmark::Counter(static_cast<double>(bytes_read), benchmark::Counter::kIsRate);
}
JL_BENCHMARK_WITH_ARGS(BM_pread);

template <size_t stride>
void BM_read(benchmark::State& state) {
  auto [len, block_size] = args(state);
  auto [read, write] = open_read_write(len);
  std::vector<char> buffer(block_size);

  size_t bytes_read = 0;
  int fd = read->fd();
  for (auto _ : state) {
    if constexpr (stride == 0) lseek(fd, 0, SEEK_SET);
    for (off_t pos = 0; pos < len; pos += stride * block_size) {
      if constexpr (stride != 0) lseek(fd, pos, SEEK_SET);
      size_t size = jl::check_rw_error(::read(fd, buffer.data(), block_size), "read failed");
      bytes_read += jl::write(*write, {buffer.data(), size});
    }
  }
  state.counters["Throughput"] = benchmark::Counter(static_cast<double>(bytes_read), benchmark::Counter::kIsRate);
}
JL_BENCHMARK_WITH_ARGS(BM_read);

template <size_t stride>
void BM_fread(benchmark::State& state) {
  auto [len, block_size] = args(state);
  auto [read, _] = open_read_write(len);
  std::vector<char> buffer(block_size);

  using unique_file = std::unique_ptr<FILE, decltype([](FILE* fp) { fclose(fp); })>;

  unique_file fp(fopen(read.path().c_str(), "re"));
  if (fp == nullptr) throw jl::errno_as_error("fopen failed");
  unique_file null(fopen("/dev/null", "w"));
  if (null == nullptr) throw jl::errno_as_error("fopen failed");

  size_t bytes_read = 0;
  for (auto _ : state) {
    if constexpr (stride == 1) rewind(fp.get());
    for (ssize_t pos = 0; pos < len; pos += stride * block_size) {
      if constexpr (stride != 1) fseek(fp.get(), pos, SEEK_SET);
      bytes_read += fread(buffer.data(), 1, block_size, fp.get());
      fwrite(buffer.data(), 1, block_size, null.get());
    }
  }
  state.counters["Throughput"] = benchmark::Counter(static_cast<double>(bytes_read), benchmark::Counter::kIsRate);
}
JL_BENCHMARK_WITH_ARGS(BM_fread);

template <size_t stride>
void BM_ifstream_read(benchmark::State& state) {
  auto [len, block_size] = args(state);
  auto [read, _] = open_read_write(len);
  std::vector<char> buffer(block_size);

  size_t bytes_read = 0;
  std::ifstream file(read.path());
  std::ofstream null("/dev/null");
  for (auto _ : state) {
    if constexpr (stride == 1) file.seekg(0, std::ios::beg);
    for (off_t pos = 0; pos < len; pos += stride * block_size) {
      if constexpr (stride != 1) file.seekg(pos, std::ios::beg);
      file.read(buffer.data(), block_size);
      null.write(buffer.data(), block_size);
      bytes_read += block_size;
    }
  }
  state.counters["Throughput"] = benchmark::Counter(static_cast<double>(bytes_read), benchmark::Counter::kIsRate);
}
JL_BENCHMARK_WITH_ARGS(BM_ifstream_read);
BENCHMARK_MAIN();
