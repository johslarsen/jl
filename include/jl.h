#pragma once
#include <sys/mman.h>
#include <unistd.h>

#include <atomic>
#include <charconv>
#include <concepts>
#include <cstring>
#include <filesystem>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <system_error>
#include <vector>

/// Johs's <mail@johslarsen.net> Library. Use however you see fit.
namespace jl {

[[nodiscard]] inline std::system_error make_system_error(std::errc err, const std::string &message) noexcept {
  return {std::make_error_code(err), message};
}

[[nodiscard]] inline std::system_error errno_as_error(const std::string &message) noexcept {
  return make_system_error(static_cast<std::errc>(errno), message);
}

/// An owned and managed file descriptor.
class unique_fd {
  int _fd;

 public:
  /// @throws std::system_error with errno and errmsg if it fails.
  explicit unique_fd(int fd, const std::string &errmsg = "unique_fd(-1)") : _fd(fd) {
    if (_fd < 0) throw errno_as_error(errmsg);
  }

  ~unique_fd() noexcept {
    if (_fd >= 0) ::close(_fd);
  }

  [[nodiscard]] int operator*() const noexcept { return _fd; }

  unique_fd(const unique_fd &) = delete;
  unique_fd &operator=(const unique_fd &) = delete;
  unique_fd(unique_fd &&other) noexcept : _fd(other._fd) {
    other._fd = -1;
  }
  unique_fd &operator=(unique_fd &&other) noexcept {
    if (this != &other) {
      _fd = other._fd;
      other._fd = -1;
    }
    return *this;
  }
};

/// An owned and managed memory mapped span.
template <typename T>
class unique_mmap {
  std::span<T> _map;

 public:
  /// A common mmap. The count/offset parameters are in counts of T, not bytes.
  /// @throws std::system_error with errno and errmsg if it fails.
  explicit unique_mmap(size_t count, int prot = PROT_NONE, int flags = MAP_SHARED, int fd = -1, off_t offset = 0, const std::string &errmsg = "mmap failed")
      : unique_mmap(nullptr, count, prot, flags, fd, offset, errmsg) {
  }

  /// More advanced constructor for e.g. MAP_FIXED when you need the addr
  /// parameter. The size/offset parameters are in counts of T, not bytes.
  /// @throws std::system_error with errno and errmsg if it fails.
  unique_mmap(void *addr, size_t count, int prot = PROT_NONE, int flags = MAP_SHARED, int fd = -1, off_t offset = 0, const std::string &errmsg = "mmap failed")
      : _map([&] {
          void *pa = ::mmap(addr, count * sizeof(T), prot, flags, fd, offset * sizeof(T));
          if (pa == MAP_FAILED) throw errno_as_error(errmsg);     // NOLINT(*cstyle-cast,*int-to-ptr)
          return std::span<T>(reinterpret_cast<T *>(pa), count);  // NOLINT(*reinterpret-cast)
        }()) {}

  ~unique_mmap() noexcept {
    if (!_map.empty()) ::munmap(_map.data(), _map.size() * sizeof(T));
  }

  [[nodiscard]] T &operator[](size_t idx) noexcept { return _map[idx]; }
  [[nodiscard]] const T &operator[](size_t idx) const noexcept { return _map[idx]; }

  [[nodiscard]] std::span<T> &operator*() noexcept { return _map; }
  [[nodiscard]] const std::span<T> &operator*() const noexcept { return _map; }
  [[nodiscard]] std::span<T> *operator->() noexcept { return &_map; }
  [[nodiscard]] const std::span<T> *operator->() const noexcept { return &_map; }

  /// The count parameter is in counts of T not bytes.
  /// @throws std::system_error with errno and errmsg if it fails.
  void remap(size_t count, int flags = 0, void *addr = nullptr, const std::string &errmsg = "mremap failed") {
    void *pa = ::mremap(_map.data(), _map.size() * sizeof(T), count * sizeof(T), flags, addr);
    if (pa == MAP_FAILED) throw errno_as_error(errmsg);     // NOLINT(*cstyle-cast,*int-to-ptr)
    _map = std::span<T>(reinterpret_cast<T *>(pa), count);  // NOLINT(*reinterpret-cast)
  }

  unique_mmap(const unique_mmap &) = delete;
  unique_mmap &operator=(const unique_mmap &) = delete;
  unique_mmap(unique_mmap &&other) noexcept : _map(other._map) {
    other._map = std::span<T>{};
  }
  unique_mmap &operator=(unique_mmap &&other) noexcept {
    if (this != &other) {
      _map = other._map;
      other._map = std::span<T>{};
    }
    return *this;
  }
};

/// A named file descriptor that is closed and removed upon destruction.
class tmpfd {
  unique_fd _fd;
  std::filesystem::path _path;

 public:
  explicit tmpfd(const std::string &prefix = "/tmp/jl_tmpfile_", const std::string &suffix = "")
      : tmpfd(prefix + "XXXXXX" + suffix, static_cast<int>(suffix.length())) {}

  ~tmpfd() {
    if (*_fd >= 0) std::filesystem::remove(_path);
  }

  [[nodiscard]] const std::filesystem::path &path() const noexcept { return _path; }
  int operator*() const noexcept { return *_fd; }

  tmpfd(const tmpfd &) = delete;
  tmpfd &operator=(const tmpfd &) = delete;
  tmpfd(tmpfd &&) = default;
  tmpfd &operator=(tmpfd &&) = default;

 private:
  tmpfd(std::string path, int suffixlen)
      : _fd(mkstemps(path.data(), suffixlen)), _path(path) {}
};

[[nodiscard]] inline std::optional<std::string> optenv(const char *name) noexcept {
  const char *value = std::getenv(name);  // NOLINT(*mt-unsafe)
  if (value == nullptr) return std::nullopt;
  return value;
}

template <typename T>
  requires std::integral<T> || std::floating_point<T>
[[nodiscard]] inline std::optional<T> env_as(const char *name) {
  auto value = optenv(name);
  if (!value) return std::nullopt;

  T parsed;
  char *end = value->data() + value->size();  // NOLINT(*pointer-arithmetic), how else?
  if (auto res = std::from_chars(value->data(), end, parsed); res.ec != std::errc()) {
    throw make_system_error(res.ec, "Failed to parse " + *value);
  }
  return parsed;
}

template <typename T>
  requires std::integral<T> || std::floating_point<T>
[[nodiscard]] inline T env_or(const char *name, T fallback) {
  return env_as<T>(name).value_or(fallback);
}
[[nodiscard]] inline std::string env_or(const char *name, const std::string &fallback) {
  return optenv(name).value_or(fallback);
}

/// @throws std::runtime_error if there is no environment variable with this name.
[[nodiscard]] inline std::string reqenv(const char *name) {
  const char *value = std::getenv(name);  // NOLINT(*mt-unsafe)
  if (value == nullptr) throw std::runtime_error(std::string("Missing ") + name + " environment value");
  return value;
}

/// A ring (aka. circular) buffer. Given an atomic Index type, one writer and
/// one reader can safely use this to share data across threads. Nevertheless,
/// It is not thread-safe to use this with multiple readers or writers.
template <size_t Capacity, typename Index = uint32_t>
  requires std::unsigned_integral<Index> || std::unsigned_integral<typename Index::value_type>
class RingBuffer {
  static_assert(std::has_single_bit(Capacity),
                "RingBuffer capacity must be a power-of-2 for performance, and so it divides the integer overflow evenly");
  static_assert(Capacity >= 4 << 10,
                "RingBuffer capacity must be page aligned");
  static_assert(std::bit_width(Capacity) < CHAR_BIT * sizeof(Index) - 1,
                "RingBuffer capacity is too large for the Index type");

  jl::unique_mmap<uint8_t> _data;
  Index _head = 0;
  Index _tail = 0;

 public:
  RingBuffer() : _data(Capacity * 2, PROT_NONE, MAP_ANONYMOUS | MAP_PRIVATE) {
    jl::tmpfd fd;
    ftruncate(*fd, Capacity);

    // _data is a continues virtual memory span twice as big as the Capacity
    // where the first and second half is mapped to the same shared buffer.
    // This gives a circular buffer that supports continuous memory spans even
    // when those bytes span across the wrap-around threshold, because
    // &_data[0] and &_data[Capacity] are both valid addresses that essentially
    // refer to the same physical address.
    //
    // This concept/trick originates from https://github.com/willemt/cbuffer
    if (::mmap(_data->data(), Capacity, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_SHARED, *fd, 0) == MAP_FAILED) {
      throw errno_as_error("RingBuffer mmap data failed");
    }
    if (::mmap(_data->data() + Capacity, Capacity, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_SHARED, *fd, 0) == MAP_FAILED) {
      throw errno_as_error("RingBuffer mmap mirror failed");
    }
  }

  /// @returns a span into the buffer at the end of the ring where you can
  /// write new data. Its size is limited by the free space available.
  [[nodiscard]] std::span<uint8_t> peek_back(size_t max) {
    return {&_data[_tail % Capacity], std::min(max, Capacity - size())};
  }

  /// "Give back" the part at the beginning of the span from peek_back() where
  /// you wrote data.
  void commit_written(std::span<uint8_t> &&written) {
    assert(written.data() == &_data[_tail % Capacity]);
    assert(size() + written.size() <= Capacity);
    _tail += written.size();
  }

  /// @returns a span into the buffer at the start of the ring where you can
  /// read available data. Its size is limited by the amount of available data.
  [[nodiscard]] std::span<const uint8_t> peek_front(size_t max) const {
    return {&_data[_head % Capacity], std::min(max, size())};
  }

  /// "Give back" the part at the beginning of the span from peek_front() that
  /// you read.
  void commit_read(std::span<const uint8_t> &&read) {
    assert(read.data() == &_data[_head % Capacity]);
    assert(read.size() <= size());
    _head += read.size();
  }

  /// @returns the amount of data available to be read. In a threaded
  /// environment it is respectively an upper/lower bound for a writer/reader.
  [[nodiscard]] size_t size() const { return _tail - _head; }
  [[nodiscard]] bool empty() const { return size() == 0; }
  [[nodiscard]] size_t capacity() const { return Capacity; }

  /// Copies bytes from data to the end of the ring.
  /// @returns the amount of data copied.
  size_t push_back(const std::span<uint8_t> data) {
    auto writeable = peek_back(data.size());
    memmove(writeable.data(), data.data(), writeable.size());
    commit_written(std::move(writeable));
    return writeable.size();
  }

  /// Copies bytes from the front of the ring into data.
  /// @returns the amount of data copied.
  size_t fill_from_front(std::span<uint8_t> data) {
    auto readable = peek_front(data.size());
    memmove(data.data(), readable.data(), readable.size());
    commit_read(std::move(readable));
    return readable.size();
  }

  /// @returns a copy of up to max available bytes from the front of the ring.
  [[nodiscard]] std::vector<uint8_t> pop_front(size_t max) {
    auto readable = peek_front(max);
    std::vector<uint8_t> output(readable.size());
    fill_from_front(output);
    return output;
  }
};

}  // namespace jl
