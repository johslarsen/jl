#pragma once
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <charconv>
#include <concepts>
#include <cstring>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
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

template <typename T>
[[nodiscard]] std::vector<std::span<T>> sliced(std::span<T> buffer, size_t size) {
  assert(buffer.size() % size == 0);
  std::vector<std::span<T>> slices(buffer.size() / size);
  for (size_t i = 0; i < slices.size(); ++i) {
    slices[i] = {&buffer[i * size], size};
  }
  return slices;
}

class tmpfd;

/// An owned and managed file descriptor.
class unique_fd {
 protected:
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
  unique_fd(tmpfd &&) = delete;  // block implicit slicing from this subclass
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

  template <typename T>
  std::span<T> readall(std::span<T> buffer) {
    for (size_t count = 0, offset = 0; offset < buffer.size(); offset += count) {
      count = read(buffer.subspan(offset)).size();
      if (count == 0) return buffer.subspan(0, offset);
    }
    return buffer;
  }

  [[nodiscard]] struct stat stat() const {
    struct stat buf {};
    if (fstat(_fd, &buf) != 0) throw errno_as_error("fstat failed");
    return buf;
  }

  void truncate(off_t length) {  // NOLINT(*const)
    if (ftruncate(_fd, length) != 0) throw errno_as_error("ftruncate failed");
  }
};

/// A named file descriptor that is closed and removed upon destruction.
class tmpfd : public unique_fd {
  std::filesystem::path _path;

 public:
  explicit tmpfd(const std::string &prefix = "/tmp/jl_tmpfile_", const std::string &suffix = "")
      : tmpfd(prefix + "XXXXXX" + suffix, static_cast<int>(suffix.length())) {}

  ~tmpfd() {
    _fd = release_unlinked();
  }

  [[nodiscard]] const std::filesystem::path &path() const noexcept { return _path; }

  tmpfd(const tmpfd &) = delete;
  tmpfd &operator=(const tmpfd &) = delete;
  tmpfd(tmpfd &&) noexcept = default;
  tmpfd &operator=(tmpfd &&) noexcept = default;

  /// Explicitly convert this into an unlinked but still open unique_fd
  unique_fd unlink() && {
    return unique_fd(release_unlinked());
  }

 private:
  tmpfd(std::string path, int suffixlen)
      : unique_fd(mkstemps(path.data(), suffixlen)), _path(path) {}

  int release_unlinked() {
    if (_fd >= 0) std::filesystem::remove(_path);

    int fd = _fd;
    _fd = -1;
    return fd;
  }
};

[[nodiscard]] inline std::string uri_host(const std::string &host) {
  return host.find(':') == std::string::npos ? host : "[" + host + "]";
}

/// An owned addrinfo wrapper that also remembers the hostname you looked up.
class unique_addr {
  std::string _host;
  std::string _port;

  struct addrinfo_deleter {
    void operator()(addrinfo *p) {
      if (p != nullptr) freeaddrinfo(p);
    }
  };
  std::unique_ptr<addrinfo, addrinfo_deleter> _addr;

 public:
  unique_addr(std::string host, std::string port, int family = 0, addrinfo hints = {}) : _host(std::move(host)), _port(std::move(port)) {
    hints.ai_family = family;
    if (host.empty()) hints.ai_flags |= AI_PASSIVE;

    addrinfo *result = nullptr;
    if (int status = ::getaddrinfo(_host.empty() ? nullptr : _host.c_str(), _port.c_str(), &hints, &result); status != 0) {
      throw std::runtime_error("getaddrinfo(" + string() + ") failed: " + gai_strerror(status));
    }
    _addr.reset(result);
  }

  [[nodiscard]] const addrinfo *get() const { return _addr.get(); }
  [[nodiscard]] std::string string() const { return uri_host(_host) + ":" + _port; }

  unique_addr(const unique_addr &) = delete;
  unique_addr &operator=(const unique_addr &) = delete;
  unique_addr(unique_addr &&) noexcept = default;
  unique_addr &operator=(unique_addr &&) noexcept = default;
  ~unique_addr() = default;
};

/// A converter from the IPv4/6 type-erased sockaddr stuctures
struct host_port {
  std::string host;
  uint16_t port = 0;

  static host_port from(const sockaddr *addr) {
    std::array<char, INET6_ADDRSTRLEN> buf{};
    switch (addr->sa_family) {
      case AF_INET: {
        const auto *v4 = reinterpret_cast<const sockaddr_in *>(addr);  // NOLINT(*reinterpret-cast) from type-erased C-struct
        return {str_or_empty(inet_ntop(addr->sa_family, &v4->sin_addr, buf.data(), sizeof(buf))),
                ntohs(v4->sin_port)};
      }
      case AF_INET6: {
        const auto *v6 = reinterpret_cast<const sockaddr_in6 *>(addr);  // NOLINT(*reinterpret-cast) from type-erased C-struct
        return {str_or_empty(inet_ntop(addr->sa_family, &v6->sin6_addr, buf.data(), sizeof(buf))),
                ntohs(v6->sin6_port)};
      }
      default:
        return {};
    }
  }
  static host_port from(const addrinfo *ai) { return from(ai->ai_addr); }
  static host_port from(const unique_addr &addr) { return from(addr.get()); }

  [[nodiscard]] std::string string() const { return uri_host(host) + ":" + std::to_string(port); }
  bool operator==(const host_port &) const = default;
};

/// An owned socket descriptor that simplifies common network usage.
class unique_socket : public unique_fd {
 public:
  explicit unique_socket(int fd) : unique_fd(fd, "unique_socket(-1)") {}

  static std::pair<unique_socket, unique_socket> pipes(int domain = AF_UNIX, int type = SOCK_STREAM) {
    std::array<int, 2> sv{-1, -1};
    if (socketpair(domain, type, 0, sv.data()) < 0) {
      throw errno_as_error("socketpair failed");
    }
    return {unique_socket(sv[0]), unique_socket(sv[1])};
  }

  static unique_socket bound(
      const unique_addr &source = {"::", "0"},
      std::optional<int> domain = {},
      std::optional<int> type = {},
      std::optional<int> protocol = {},
      const std::function<void(unique_socket &)> &before_bind = [](auto &) {}) {
    for (const auto *p = source.get(); p != nullptr; p = p->ai_next) {
      if (int fd = ::socket(domain.value_or(p->ai_family), type.value_or(p->ai_socktype), protocol.value_or(p->ai_protocol)); fd >= 0) {
        unique_socket ufd(std::move(fd));
        before_bind(ufd);
        if (::bind(fd, p->ai_addr, p->ai_addrlen) == 0) return ufd;
      }
    }
    throw errno_as_error("socket/bind(" + source.string() + ") failed");
  }
  static unique_socket udp(
      const unique_addr &source = {"::", "0"},
      std::optional<int> domain = {},
      std::optional<int> protocol = IPPROTO_UDP,
      const std::function<void(unique_socket &)> &before_bind = [](auto &) {}) {
    return bound(source, domain, SOCK_DGRAM, protocol, before_bind);
  }
  static unique_socket tcp(
      const unique_addr &source = {"::", "0"},
      std::optional<int> domain = {},
      std::optional<int> protocol = IPPROTO_TCP,
      const std::function<void(unique_socket &)> &before_bind = [](auto &) {}) {
    return bound(source, domain, SOCK_STREAM, protocol, [&](auto &fd) {
      fd.setsockopt(SOL_SOCKET, SO_REUSEADDR, 1);
      before_bind(fd);
    });
  }

  void bind(const unique_addr &source = unique_addr("", "0")) {
    for (const auto *p = source.get(); p != nullptr; p = p->ai_next) {
      if (::bind(fd(), p->ai_addr, p->ai_addrlen) == 0) return;
    }
    throw errno_as_error("bind(" + source.string() + ") failed");
  }

  void connect(const unique_addr &source) {
    for (const auto *p = source.get(); p != nullptr; p = p->ai_next) {
      if (::connect(fd(), p->ai_addr, p->ai_addrlen) == 0) return;
    }
    throw errno_as_error("connect(" + source.string() + ") failed");
  }

  void listen(int backlog) {
    check_rw_error(::listen(fd(), backlog), "listen failed");
  }
  std::optional<std::pair<unique_socket, host_port>> accept(int flags = 0) {
    sockaddr_in6 addr_buf{};
    auto *addr = reinterpret_cast<sockaddr *>(&addr_buf);  // NOLINT(*reinterpret-cast) to type-erased C-struct
    socklen_t addr_len = sizeof(addr_buf);

    auto client = check_rw_error(::accept4(fd(), addr, &addr_len, flags), "accept failed");
    if (client < 0) return std::nullopt;
    return {{unique_socket(client), host_port::from(addr)}};
  }

  template <typename T>
  int try_setsockopt(int level, int option_name, const T &value) {
    return ::setsockopt(fd(), level, option_name, &value, sizeof(value));
  }
  template <typename T>
  void setsockopt(int level, int option_name, const T &value) {
    if (try_setsockopt(level, option_name, value) < 0) {
      throw errno_as_error("setsockopt(" + std::to_string(level) + ", " + std::to_string(option_name) + ") failed");
    }
  }

  template <typename C>
    requires std::constructible_from<std::span<const typename C::value_type>, C>
  size_t send(const C &data, int flags = 0) {  // NOLINT(*-function-const)
    constexpr size_t size = sizeof(typename C::value_type);
    return check_rw_error(::send(fd(), data.data(), size * data.size(), flags), "send failed") / size;
  }
  size_t send(std::string_view data, int flags = 0) {  // NOLINT(*-function-const)
    return check_rw_error(::send(fd(), data.data(), data.size(), flags), "send failed");
  }

  template <typename T>
  std::span<T> recv(std::span<T> buffer, int flags = 0) {  // NOLINT(*-function-const)
    auto n = check_rw_error(::recv(fd(), buffer.data(), sizeof(T) * buffer.size(), flags), "recv failed");
    return buffer.subspan(0, n / sizeof(T));
  }
  template <typename C>
    requires std::constructible_from<std::span<const typename C::value_type>, C>
  std::span<typename C::value_type> recv(C &data, int flags = 0) {  // NOLINT(*-function-const)
    return recv(std::span<typename C::value_type>(data), flags);
  }
  std::string_view recv(std::string &buffer, int flags = 0) {  // NOLINT(*-function-const)
    auto n = check_rw_error(::recv(fd(), buffer.data(), buffer.size(), flags), "recv failed");
    return {buffer.begin(), buffer.begin() + n};
  }
};

/// An abstraction for managing the POSIX "span" structures required by
/// multi-message system calls like recvmmsg/sendmmsg.
template <typename T = char>
  requires std::is_trivially_copyable<T>::value
class mmsg_socket {
  unique_socket _fd;
  std::vector<mmsghdr> _msgs;
  std::vector<iovec> _iovecs;
  std::vector<std::span<T>> _received;  /// lazily initialized

 public:
  /// WARN: Buffers should be large enough to fit the expected messages
  /// otherwise those could be truncated (see `man recvmsg` for details).
  mmsg_socket(unique_socket fd, std::span<std::span<T>> buffers)
      : _fd(std::move(fd)), _msgs(buffers.size()), _iovecs(buffers.size()) {
    reset(buffers);
  }

  void reset(std::span<std::span<T>> buffers) {
    _msgs.resize(buffers.size());
    _iovecs.resize(buffers.size());
    for (size_t i = 0; i < buffers.size(); ++i) {
      _iovecs[i].iov_base = buffers[i].data();
      _iovecs[i].iov_len = sizeof(T) * buffers[i].size();
      _msgs[i].msg_hdr.msg_iov = &_iovecs[i];
      _msgs[i].msg_hdr.msg_iovlen = 1;
    }
  }

  /// WARN: new_count only updates the length field in the message header, so
  /// it assumes that the original message buffer is large enough for this.
  ///
  /// @returns the buffer for the message at idx.
  [[nodiscard]] std::span<T> buffer(size_t idx, std::optional<size_t> new_count = std::nullopt) {
    if (new_count) {
      _iovecs[idx].iov_len = sizeof(T) * *new_count;
    }
    // NOLINTNEXTLINE(*reinterpret-cast) is safe because iov_base originate from std::span<T>.data()
    T *base = reinterpret_cast<T *>(_iovecs[idx].iov_base);
    return {base, base + _iovecs[idx].iov_len / sizeof(T)};
  }
  /// WARN: Assumes that the message buffer is large enough to fit data.
  void write(size_t idx, std::span<const T> data) {
    auto buf = buffer(idx, data.size());
    std::copy(data.begin(), data.end(), buf.begin());
  }
  [[nodiscard]] mmsghdr &message(size_t idx) { return _msgs[idx]; }

  /// Sends message buffers off through off + count.
  /// @returns the number of messages sent.
  size_t sendmmsg(off_t off = 0, std::optional<size_t> count = std::nullopt, int flags = MSG_WAITFORONE) {
    // assert(off + count <= _msgs.size());
    return check_rw_error(::sendmmsg(*_fd, &_msgs[off], count.value_or(_msgs.size() - off), flags), "sendmmsg failed");
  }

  /// Receives message into buffers off through off + count. Returned spans are
  /// valid until further operations on those same message slots.
  std::span<std::span<T>> recvmmsg(off_t off = 0, std::optional<size_t> count = std::nullopt, int flags = MSG_WAITFORONE) {
    int msgs = check_rw_error(::recvmmsg(*_fd, &_msgs[off], count.value_or(_msgs.size() - off), flags, nullptr), "recvmmsg failed");
    if (static_cast<int>(_received.size()) < msgs) {
      _received.resize(_msgs.size());
    }
    for (int i = 0; i < msgs; ++i) {
      // NOLINTNEXTLINE(*reinterpret-cast) is safe because iov_base originate from std::span<T>.data()
      T *base = reinterpret_cast<T *>(_iovecs[i].iov_base);
      _received[i] = {base, base + _msgs[i].msg_len / sizeof(T)};
    }
    return {_received.begin(), _received.begin() + msgs};
  }

  [[nodiscard]] unique_socket &fd() noexcept { return _fd; }

  mmsg_socket(const mmsg_socket &) = delete;
  mmsg_socket &operator=(const mmsg_socket &) = delete;
  mmsg_socket(mmsg_socket &&) noexcept = default;
  mmsg_socket &operator=(mmsg_socket &&) noexcept = default;
  ~mmsg_socket() = default;
};

/// An abstraction for doing
template <typename T = char>
  requires std::is_trivially_copyable<T>::value
class mmsg_buffer : public mmsg_socket<T> {
  std::vector<T> _buffer;

 public:
  /// WARN: The mtu (Maximum Transfer Unit) size is an upper limited to the
  /// size of messages that are sent or received. Writing messages larger than
  /// that would overflow its buffer, and received messages could be truncated
  /// to fit in the buffers (see `man recvmsg` for details).
  mmsg_buffer(unique_socket fd, size_t msgs, size_t mtu = 1500)
      : mmsg_socket<T>(std::move(fd), {}), _buffer(msgs * mtu) {
    auto slices = sliced<T>(_buffer, mtu);
    mmsg_socket<T>::reset(slices);
  }

  void reset(std::span<std::span<T>> buffers) = delete;

  mmsg_buffer(const mmsg_buffer &) = delete;
  mmsg_buffer &operator=(const mmsg_buffer &) = delete;
  mmsg_buffer(mmsg_buffer &&) noexcept = default;
  mmsg_buffer &operator=(mmsg_buffer &&) noexcept = default;
  ~mmsg_buffer() = default;
};

/// An owned and managed memory mapped span.
template <typename T>
  requires std::is_trivially_copyable<T>::value
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
          return std::span<T>(reinterpret_cast<T *>(pa), count);  // NOLINT(*reinterpret-cast), mmap returns page-aligned address
        }()) {}

  static unique_mmap<T> anon(size_t count, int prot = PROT_NONE, const std::string &name = "unique_mmap", int flags = MAP_ANONYMOUS | MAP_PRIVATE, const std::string &errmsg = "anon mmap failed") {
    unique_mmap<T> map(count, prot, flags, -1, 0, errmsg);
    prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, &map[0], count * sizeof(T), name.c_str());  // best effort, so okay if it fails silently
    return map;
  }

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
    _map = std::span<T>(reinterpret_cast<T *>(pa), count);  // NOLINT(*reinterpret-cast), mremap returns page-aligned address
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

/// An owned and managed file descriptor and mapping to its contents
template <typename T>
  requires std::is_trivially_copyable<T>::value
class fd_mmap {
  off_t _offset;  // need to keep track of this to properly map size after truncation
  unique_fd _fd;
  unique_mmap<T> _mmap;  // destroy before _fd, so fd is unmapped before being closed

  /// Nominally this reflects the _mmap span. However, files can be empty, and
  /// memory maps cannot. If that is the case (e.g. a newly created file) we
  /// prefer not to fail, so internally we still map a non-empty portion of the
  /// file into _mmap, and instead put a empty span here to return to the user.
  std::span<T> _map;

 public:
  /// A mmap over fd. The count/offset parameters are in counts of T, not bytes.
  /// @throws std::system_error with errno and errmsg if it fails.
  explicit fd_mmap(unique_fd fd, int prot = PROT_READ, int flags = MAP_SHARED, off_t offset = 0, std::optional<size_t> count = std::nullopt, const std::string &errmsg = "fd_mmap failed")  // NOLINT(*-member-init) false positive: https://github.com/llvm/llvm-project/issues/37250
      : fd_mmap(with_count(std::move(fd), count), prot, flags, offset, count, errmsg) {}

  [[nodiscard]] int fd() const noexcept { return _fd.fd(); }

  [[nodiscard]] T &operator[](size_t idx) noexcept { return _map[idx]; }
  [[nodiscard]] const T &operator[](size_t idx) const noexcept { return _map[idx]; }

  [[nodiscard]] std::span<T> &operator*() noexcept { return _map; }
  [[nodiscard]] const std::span<T> &operator*() const noexcept { _map; }
  [[nodiscard]] std::span<T> *operator->() noexcept { return &_map; }
  [[nodiscard]] const std::span<T> *operator->() const noexcept { return &_map; }

  /// The count parameter is in counts of T not bytes. New mapping is relative
  /// to offset from construction.
  /// @throws std::system_error with errno and errmsg if it fails.
  void remap(size_t count, int mremap_flags = 0) {
    _mmap.remap(count, mremap_flags);
    _map = *_mmap;
  }

  /// Truncate the file to this length. Length is in counts of T not bytes.
  /// Length is relative to start of file, but remapping is relative to offset
  /// from construction.
  /// @throws std::system_error with errno and errmsg if it fails.
  void truncate(size_t length, int mremap_flags = 0) {
    _fd.truncate(length * sizeof(T));
    _mmap.remap(beyond_offset(length), mremap_flags);
    _map = _offset < static_cast<off_t>(length) ? *_mmap : _mmap->subspan(0, 0);
  }

 private:
  static std::pair<unique_fd, size_t> with_count(unique_fd fd, std::optional<size_t> specified_count) {
    auto count = specified_count.value_or(fd.stat().st_size / sizeof(T));
    return {std::move(fd), count};
  }
  fd_mmap(std::pair<unique_fd, size_t> fd_count, int prot, int flags, off_t offset, std::optional<size_t> specified_count, const std::string &errmsg)
      : _offset(offset),
        _fd(std::move(fd_count.first)),
        _mmap(specified_count.value_or(beyond_offset(fd_count.second)), prot, flags, _fd.fd(), offset, errmsg),
        _map(specified_count || offset < static_cast<off_t>(fd_count.second) ? *_mmap : _mmap->subspan(0, 0)) {}

  [[nodiscard]] size_t beyond_offset(size_t count) {
    if (static_cast<off_t>(count) <= _offset) return 1;  // 0-sized mmaps are not allowed, so always map something
    return count - _offset;
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
  explicit CircularBuffer(const std::string &mmap_name = "CircularBuffer")
      : _data(unique_mmap<T>::anon(Capacity * 2, PROT_NONE, mmap_name)) {
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
    int prot = PROT_READ | PROT_WRITE;
    int flags = MAP_FIXED | MAP_SHARED;
    if (::mmap(_data->data(), len, prot, flags, *fd, 0) == MAP_FAILED) {  // NOLINT(*cstyle-cast,*int-to-ptr)
      throw errno_as_error("CircularBuffer mmap data failed");
    }
    if (::mmap(_data->data() + Capacity, len, prot, flags, *fd, 0) == MAP_FAILED) {  // NOLINT(*cstyle-cast,*int-to-ptr)
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
