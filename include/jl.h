#pragma once
#include <sys/mman.h>
#include <unistd.h>

#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <system_error>

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
    if (_fd >= 0) {
      ::close(_fd);
      _fd = -1;
    }
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
      : _map(mmap(nullptr, count, prot, flags, fd, offset, errmsg)) {
  }

  /// More advanced constructor for e.g. MAP_FIXED when you need the addr
  /// parameter. The size/offset parameters are in counts of T, not bytes.
  /// @throws std::system_error with errno and errmsg if it fails.
  unique_mmap(void *addr, size_t count, int prot = PROT_NONE, int flags = MAP_SHARED, int fd = -1, off_t offset = 0, const std::string &errmsg = "mmap failed")
      : _map(mmap(addr, count, prot, flags, fd, offset, errmsg)) {
  }

  ~unique_mmap() noexcept {
    if (!_map.empty()) {
      ::munmap(_map.data(), _map.size() * sizeof(T));
      _map = std::span<T>{};
    }
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

 private:
  static std::span<T> mmap(void *addr, size_t count, int prot, int flags, int fd, off_t offset, const std::string &errmsg) {
    void *pa = ::mmap(addr, count * sizeof(T), prot, flags, fd, offset * sizeof(T));
    if (pa == MAP_FAILED) throw errno_as_error(errmsg);     // NOLINT(*cstyle-cast,*int-to-ptr)
    return std::span<T>(reinterpret_cast<T *>(pa), count);  // NOLINT(*reinterpret-cast)
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

/// @throws std::runtime_error if there is no environment variable with this name.
[[nodiscard]] inline std::string reqenv(const char *name) {
  const char *value = std::getenv(name);  // NOLINT(*mt-unsafe)
  if (value == nullptr) throw std::runtime_error(std::string("Missing ") + name + " environment value");
  return value;
}

}  // namespace jl
