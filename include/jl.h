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

[[nodiscard]] inline std::string str_or_empty(const char *str) {
  return {str == nullptr ? "" : str};
}

[[nodiscard]] inline std::system_error make_system_error(std::errc err, const std::string &message) noexcept {
  return {std::make_error_code(err), message};
}

[[nodiscard]] inline std::system_error errno_as_error(const std::string &message) noexcept {
  return make_system_error(static_cast<std::errc>(errno), message);
}

/// @returns n usually or 0 for EAGAIN
/// @throws std::system_error on other errors
template <typename T>
T check_rw_error(T n, const std::string &message) {
  if (n < 0) {
    if (errno == EAGAIN) return 0;
    throw errno_as_error(message);
  }
  return n;
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
  [[nodiscard]] int fd() const noexcept { return _fd; }

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

  template <typename C>
    requires std::constructible_from<std::span<const typename C::value_type>, C>
  size_t write(const C &data) {  // NOLINT(*const)
    constexpr size_t size = sizeof(typename C::value_type);
    return check_rw_error(::write(_fd, data.data(), size * data.size()), "write failed") / size;
  }
  size_t write(std::string_view data) {  // NOLINT(*const)
    return check_rw_error(::write(_fd, data.data(), data.size()), "write failed");
  }

  template <typename T>
  std::span<T> read(std::span<T> buffer) {  // NOLINT(*const)
    auto n = check_rw_error(::read(_fd, buffer.data(), sizeof(T) * buffer.size()), "read failed");
    return buffer.subspan(0, n / sizeof(T));
  }
  template <typename C>
    requires std::constructible_from<std::span<const typename C::value_type>, C>
  std::span<typename C::value_type> read(C &data) {  // NOLINT(*const)
    return read(std::span<typename C::value_type>(data));
  }
  std::string_view read(std::string &buffer) {  // NOLINT(*const)
    auto n = check_rw_error(::read(_fd, buffer.data(), buffer.size()), "read failed");
    return {buffer.begin(), buffer.begin() + n};
  }
};

/// A named file descriptor that is closed and removed upon destruction.
class tmpfd : public unique_fd {
  std::filesystem::path _path;

 public:
  explicit tmpfd(const std::string &prefix = "/tmp/jl_tmpfile_", const std::string &suffix = "")
      : tmpfd(prefix + "XXXXXX" + suffix, static_cast<int>(suffix.length())) {}

  ~tmpfd() {
    if (fd() >= 0) std::filesystem::remove(_path);
  }

  [[nodiscard]] const std::filesystem::path &path() const noexcept { return _path; }

  tmpfd(const tmpfd &) = delete;
  tmpfd &operator=(const tmpfd &) = delete;
  tmpfd(tmpfd &&) noexcept = default;
  tmpfd &operator=(tmpfd &&) noexcept = default;

 private:
  tmpfd(std::string path, int suffixlen)
      : unique_fd(mkstemps(path.data(), suffixlen)), _path(path) {}
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

/// A circular (aka. ring) buffer with support for copy-free read/write of
/// contiguous elements anywhere in the buffer, even across the wrap-around
/// threshold. Given an atomic Index type, one writer and one reader can safely
/// use this to share data across threads. Even so, it is not thread-safe to
/// use this if there are multiple readers or writers.
template <typename T, size_t Capacity, typename Index = uint32_t>
  requires std::unsigned_integral<Index> || std::unsigned_integral<typename Index::value_type>
class CircularBuffer {
  static_assert(std::has_single_bit(Capacity),
                "CircularBuffer capacity must be a power-of-2 for performance, and so it divides the integer overflow evenly");
  static_assert((sizeof(T) * Capacity) % (4 << 10) == 0,
                "CircularBuffer byte capacity must be page aligned");
  static_assert(std::bit_width(Capacity) < CHAR_BIT * sizeof(Index) - 1,
                "CircularBuffer capacity is too large for the Index type");

  jl::unique_mmap<T> _data;
  Index _read = 0;
  Index _write = 0;

 public:
  CircularBuffer() : _data(Capacity * 2, PROT_NONE, MAP_ANONYMOUS | MAP_PRIVATE) {
    jl::tmpfd fd;
    off_t len = Capacity * sizeof(T);
    ftruncate(*fd, len);

    // _data is a continues virtual memory span twice as big as the Capacity
    // where the first and second half is mapped to the same shared buffer.
    // This gives a circular buffer that supports continuous memory spans even
    // when those bytes span across the wrap-around threshold, because
    // &_data[0] and &_data[Capacity] are both valid addresses that essentially
    // refer to the same physical address.
    //
    // This concept/trick originates from https://github.com/willemt/cbuffer
    if (::mmap(_data->data(), len, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_SHARED, *fd, 0) == MAP_FAILED) {
      throw errno_as_error("CircularBuffer mmap data failed");
    }
    if (::mmap(_data->data() + Capacity, len, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_SHARED, *fd, 0) == MAP_FAILED) {
      throw errno_as_error("CircularBuffer mmap shadow failed");
    }
  }

  /// @returns a span where you can write new data into the buffer where. Its
  /// size is limited to the amount of free space available.
  [[nodiscard]] std::span<T> peek_back(size_t max) noexcept {
    return {&_data[_write % Capacity], std::min(max, Capacity - size())};
  }

  /// "Give back" the part at the beginning of the span from peek_back() where
  /// you wrote data.
  void commit_written(std::span<T> &&written) noexcept {
    assert(written.data() == &_data[_write % Capacity]);
    assert(size() + written.size() <= Capacity);
    _write += written.size();
  }

  /// @returns a span where you can read available data from the buffer. Its
  /// size is limited by the amount of available data.
  [[nodiscard]] std::span<const T> peek_front(size_t max) const noexcept {
    return {&_data[_read % Capacity], std::min(max, size())};
  }
  [[nodiscard]] std::span<T> peek_front(size_t max) noexcept {
    return {&_data[_read % Capacity], std::min(max, size())};
  }

  /// "Give back" the part at the beginning of the span from peek_front() that
  /// you read.
  void commit_read(std::span<const T> &&read) noexcept {
    assert(read.data() == &_data[_read % Capacity]);
    assert(read.size() <= size());
    _read += read.size();
  }

  /// @returns the amount of data available to be read. In a threaded
  /// environment where there is exactly one reader and one writer this is
  /// respectively the lower and the upper bound for the current size.
  [[nodiscard]] size_t size() const noexcept { return _write - _read; }
  [[nodiscard]] bool empty() const noexcept { return size() == 0; }
  [[nodiscard]] size_t capacity() const noexcept { return Capacity; }

  /// Writes elements from data into the buffer.
  /// @returns the number of elements copied, and appended to the buffer.
  size_t push_back(const std::span<T> data) noexcept {
    auto writeable = peek_back(data.size());
    std::copy(data.begin(), data.begin() + writeable.size(), writeable.begin());
    commit_written(std::move(writeable));
    return writeable.size();
  }

  /// Read elements from the buffer into data.
  /// @returns the number of elements copied and erased from the buffer.
  size_t fill_from_front(std::span<T> data) noexcept {
    auto readable = peek_front(data.size());
    std::copy(readable.begin(), readable.end(), data.begin());
    commit_read(std::move(readable));
    return readable.size();
  }

  /// @returns elements read and copied from the buffer.
  [[nodiscard]] std::vector<T> pop_front(size_t max) {
    auto readable = peek_front(max);
    std::vector<T> output(readable.size());
    fill_from_front(output);
    return output;
  }
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

}  // namespace jl
