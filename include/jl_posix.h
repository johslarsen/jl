#pragma once
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>

#include "jl.h"

/// Johs's <mail@johslarsen.net> Library. Use however you see fit.
namespace jl {

template <class T = void>
expected_or_errno<T*> ok_mmap(void* p) {
  if (p == MAP_FAILED) return std::unexpected(errno);
  return reinterpret_cast<T*>(p);
}

template <std::ranges::contiguous_range R, class T = std::ranges::range_value_t<R>>
  requires std::is_trivially_copyable_v<T>
inline constexpr iovec as_iovec(R& span) {
  return iovec{.iov_base = std::ranges::data(span), .iov_len = sizeof(T) * std::ranges::size(span)};
}

template <std::ranges::viewable_range R, std::ranges::contiguous_range T = std::ranges::range_value_t<R>>
  requires std::is_trivially_copyable_v<std::ranges::range_value_t<T>>
inline std::vector<iovec> as_iovecs(R&& spans) noexcept {
  return std::ranges::to<std::vector>(spans | std::views::transform(as_iovec<T>));
}

template <class T>
inline std::span<T> as_span(auto&& src) noexcept {
  if constexpr (std::same_as<std::remove_cvref_t<decltype(src)>, iovec>) {
    return {reinterpret_cast<T*>(src.iov_base), src.iov_len / sizeof(T)};
  } else {
    return std::span(src);
  }
}

template <class T>
inline std::vector<std::span<T>> as_spans(std::span<iovec> iovecs) noexcept {
  return std::ranges::to<std::vector>(iovecs | std::views::transform([](auto r) { return as_span<T>(r); }));
}

/// Copy the concatenation of list of input buffers (e.g. iovecs)
template <class T, class ListOfSpanable>
inline std::span<T> copy(ListOfSpanable&& source, std::span<T> dest) noexcept {
  std::span<T> copied;
  for (const auto& spanable : source) {
    if (copied.size() == dest.size()) break;

    auto part = as_span<const T>(spanable);
    auto last = part.begin() + std::min(part.size(), dest.size() - copied.size());
    auto copied_end = std::copy(part.begin(), last, dest.begin() + copied.size());
    copied = {dest.begin(), copied_end};
  }
  return copied;
}

/// A file descriptor associated with an explicit offset
struct ofd {
  int fd = -1;
  std::optional<off_t> offset = std::nullopt;
};

/// Copy up to len bytes from in to out (see `man 2 sendfile` for details).
/// NOTE: The system call requires that at most one of the file descriptors is a pipe.
std::expected<size_t, error> inline sendfileall(int fd_out, ofd in, size_t len) {
  return rw_loop(len, [fd_out, in_fd = in.fd, in_off = nullable(in.offset)](size_t remaining, off_t) {
           return ::sendfile(fd_out, in_fd, in_off, remaining);
         })
      .transform_error([in, fd_out](auto ec) { return error(ec, "sendfile({} -> {})", in.fd, fd_out); });
}

/// Copy up to len bytes from in to out (see `man 2 splice` for details).
/// NOTE: The system call requires that at least one of the file descriptors is a pipe.
std::expected<size_t, error> inline spliceall(ofd in, ofd out, size_t len, unsigned flags = 0) {  // NOLINT(*swappable-parameters), to mimic ::splice
  return rw_loop(len, [flags, in = in.fd, in_off = nullable(in.offset), out = out.fd, out_off = nullable(out.offset)](size_t remaining, off_t) {
           return ::splice(in, in_off, out, out_off, remaining, flags);
         })
      .transform_error([in, out](auto ec) { return error(ec, "splice({} -> {})", in.fd, out.fd); });
}

/// An owned and managed file descriptor.
class unique_fd {
 protected:
  int _fd;

 public:
  [[nodiscard]] static expected_or_errno<unique_fd> from(int fd) {
    return ok_or_errno(fd).transform([](int fd) { return unique_fd(fd); });
  }

  [[nodiscard]] static std::expected<unique_fd, error> open(const std::filesystem::path& path, int oflag) {
    return from(::open(path.c_str(), oflag))
        .transform_error([&](auto ec) { return error(ec, "open({}, 0x{:x})", path.c_str(), oflag); });
  }

  [[nodiscard]] static expected_or_errno<std::pair<unique_fd, unique_fd>> pipes(int flags = O_CLOEXEC) {
    std::array<int, 2> sv{-1, -1};
    return zero_or_errno(pipe2(sv.data(), flags))
        .transform([&sv] { return std::make_pair(unique_fd(sv[0]), unique_fd(sv[1])); });
  }

  [[nodiscard]] int operator*() const noexcept { return _fd; }
  [[nodiscard]] int fd() const noexcept { return _fd; }

  void reset(int fd = -1) noexcept {
    if (auto old = std::exchange(_fd, fd); old >= 0) ::close(old);
  }
  int release() noexcept {
    return std::exchange(_fd, -1);
  }
  ~unique_fd() noexcept { reset(); }
  unique_fd(const unique_fd&) = delete;
  unique_fd& operator=(const unique_fd&) = delete;
  unique_fd(unique_fd&& other) noexcept : _fd(other.release()) {}
  unique_fd& operator=(unique_fd&& other) noexcept {
    reset(other.release());
    return *this;
  }

 protected:
  explicit unique_fd(int fd) : _fd(fd) {}
};

template <class C>
  requires std::constructible_from<std::span<const typename C::value_type>, C>
[[nodiscard]] size_t write(int fd, const C& data) {
  constexpr size_t size = sizeof(typename C::value_type);
  return unwrap(ok_or_errno(::write(fd, data.data(), size * data.size()))) / size;
}
[[nodiscard]] size_t inline write(int fd, std::string_view data) {
  return write(fd, std::span(data));
}

template <class C, size_t Size = sizeof(typename C::value_type)>
  requires std::constructible_from<std::span<const typename C::value_type>, C>
[[nodiscard]] std::expected<size_t, error> writeall(int fd, const C& data) {
  return rw_loop(Size * data.size(), [fd, buf = data.data()](size_t remaining, off_t offset) {
           return ::write(fd, buf + offset / Size, remaining);
         })
      .transform([](size_t bytes_written) { return bytes_written / Size; })
      .transform_error([fd](auto ec) { return error(ec, "write({})", fd); });
}

template <class T>
[[nodiscard]] std::span<T> read(int fd, std::span<T> buffer) {
  auto n = unwrap(ok_or_errno(::read(fd, buffer.data(), sizeof(T) * buffer.size())));
  return buffer.subspan(0, n / sizeof(T));
}
template <class C>
  requires std::constructible_from<std::span<const typename C::value_type>, C>
[[nodiscard]] std::span<typename C::value_type> read(int fd, C& buffer) {
  return read(fd, std::span(buffer));
}
[[nodiscard]] std::string_view inline read(int fd, std::string& buffer) {
  auto result = read(fd, std::span(buffer));
  return {result.data(), result.size()};
}

template <class T, size_t Size = sizeof(T)>
[[nodiscard]] std::expected<std::span<T>, error> readall(int fd, std::span<T> buffer) {
  return rw_loop(Size * buffer.size(), [fd, buf = buffer.data()](size_t remaining, off_t offset) {
           return ::read(fd, buf + offset / Size, remaining);
         })
      .transform([buffer](size_t bytes_read) { return buffer.subspan(0, bytes_read / Size); })
      .transform_error([fd](auto ec) { return error(ec, "write({})", fd); });
}

[[nodiscard]] std::expected<struct stat, error> inline stat(int fd) {
  struct stat buf{};
  return zero_or_errno(fstat(fd, &buf))
      .transform([&] { return std::move(buf); })
      .transform_error([fd](auto ec) { return error(ec, "fstat({})", fd); });
}

[[nodiscard]] std::expected<void, error> inline truncate(int fd, off_t length) {
  return zero_or_errno(ftruncate(fd, length))
      .transform_error([=](auto ec) { return error(ec, "ftruncate({}, {})", fd, length); });
}

/// @returns nfd
inline expected_or_errno<int> poll(std::span<pollfd> fds, std::chrono::nanoseconds timeout = std::chrono::nanoseconds(0), std::optional<sigset_t> sigset = std::nullopt) {
  auto ts = as_timespec(timeout);
  return ok_or_errno(::ppoll(fds.data(), fds.size(), &ts, nullable(sigset)))
      .or_else(retryable_as<EAGAIN, EINTR>(0));  // i.e. as if timeout
}
/// @returns revents (0 on timeout/EAGAIN/EINTR)
inline expected_or_errno<int> poll(int fd, short events, std::chrono::nanoseconds timeout = std::chrono::nanoseconds{0}, std::optional<sigset_t> sigset = std::nullopt) {
  pollfd fds{.fd = fd, .events = events, .revents = 0};
  return poll(std::span{&fds, 1}, timeout, sigset)
      .transform([&fds](int nfd) { return nfd == 1 ? fds.revents : 0; });
}

/// A named file descriptor that is closed and removed upon destruction.
class tmpfd {
  unique_fd _fd;
  std::filesystem::path _path;

 public:
  [[nodiscard]] static std::expected<tmpfd, error> open(const std::string& prefix = "/tmp/jl_tmpfile_", const std::string& suffix = "") {
    auto path = std::format("{}XXXXXX{}", prefix, suffix);
    return unique_fd::from(mkstemps(path.data(), static_cast<int>(suffix.length())))
        .transform_error([&path](auto ec) { return error(ec, "mkstemps({})", path); })
        .transform([&path](unique_fd fd) { return tmpfd(std::move(fd), std::move(path)); });
  }
  [[nodiscard]] static std::expected<unique_fd, error> unlinked() {
    return open().transform([](tmpfd fd) { return std::move(fd).unlink(); });
  }

  [[nodiscard]] auto* operator->(this auto& self) noexcept { return &self._fd; }

  [[nodiscard]] const std::filesystem::path& path() const noexcept { return _path; }
  [[nodiscard]] std::string url() const noexcept { return std::format("file://{}", path().string()); }

  /// Tries to unlink the file, and clears path if this is successful to be idempotent.
  [[nodiscard]] std::error_code try_unlink() noexcept {
    if (!_path.empty()) {
      std::error_code error{};
      if (!std::filesystem::remove(_path, error)) return error;
    }
    _path.clear();
    return {};
  }
  /// Explicitly convert this into an unlinked but still open unique_fd,
  /// but silently ignores unlink failures that occurs.
  unique_fd unlink() && noexcept {
    if (try_unlink().value() > 0) _path.clear();
    return std::move(_fd);
  }

  ~tmpfd() noexcept {
    std::move(*this).unlink();
  }
  tmpfd(const tmpfd&) = delete;
  tmpfd& operator=(const tmpfd&) = delete;
  tmpfd(tmpfd&& other) noexcept : _fd(std::move(other._fd)),
                                  _path(std::exchange(other._path, {})) {}
  tmpfd& operator=(tmpfd&& other) noexcept {
    _fd = std::move(other._fd);
    std::swap(_path, other._path);  // delegate unlink of our old _path to the other
    return *this;
  }

 private:
  tmpfd(unique_fd fd, std::filesystem::path path) : _fd(std::move(fd)), _path(std::move(path)) {}
};

/// An owned addrinfo wrapper that also remembers the hostname you looked up.
class unique_addr {
  std::string _host;
  std::string _port;

  std::unique_ptr<addrinfo, deleter<freeaddrinfo>> _addr;

 public:
  unique_addr(std::string host, std::string port, int family = 0, addrinfo hints = {}) : _host(std::move(host)), _port(std::move(port)) {
    hints.ai_family = family;
    if (host.empty()) hints.ai_flags |= AI_PASSIVE;

    addrinfo* result = nullptr;
    if (int status = ::getaddrinfo(_host.empty() ? nullptr : _host.c_str(), _port.c_str(), &hints, &result); status != 0) {
      if (status == EAI_SYSTEM) throw error(errno, "getaddrinfo({})", string());
      throw std::runtime_error(std::format("getaddrinfo({}): {}", string(), gai_strerror(status)));
    }
    _addr.reset(result);
  }

  [[nodiscard]] auto* get(this auto& self) { return self._addr.get(); }
  [[nodiscard]] std::string string() const { return std::format("{}:{}", uri_host(_host), _port); }

  ~unique_addr() = default;
  unique_addr(const unique_addr&) = delete;
  unique_addr& operator=(const unique_addr&) = delete;
  unique_addr(unique_addr&&) noexcept = default;
  unique_addr& operator=(unique_addr&&) noexcept = default;
};

struct type_erased_sockaddr {
  sockaddr_storage _buffer{};
  socklen_t length = sizeof(_buffer);

  static type_erased_sockaddr from(int fd) {
    type_erased_sockaddr addr;
    if (getsockname(fd, addr.get(), &addr.length) != 0) throw error(errno, "getsockname({})", fd);
    return addr;
  }

  [[nodiscard]] const sockaddr* get() const { return reinterpret_cast<const sockaddr*>(&_buffer); }  // NOLINT(*reinterpret-cast) to type-erased C-struct
  [[nodiscard]] sockaddr* get() { return reinterpret_cast<sockaddr*>(&_buffer); }                    // NOLINT(*reinterpret-cast) to type-erased C-struct
};

/// A converter from the IPv4/6 type-erased sockaddr stuctures
struct host_port {
  std::string host;
  uint16_t port = 0;

  [[nodiscard]] static host_port from(const sockaddr* addr) {
    std::array<char, INET6_ADDRSTRLEN> buf{};
    switch (addr->sa_family) {
      case AF_INET: {
        const auto* v4 = reinterpret_cast<const sockaddr_in*>(addr);  // NOLINT(*reinterpret-cast) from type-erased C-struct
        return {.host = str_or_empty(inet_ntop(addr->sa_family, &v4->sin_addr, buf.data(), sizeof(buf))),
                .port = ntohs(v4->sin_port)};
      }
      case AF_INET6: {
        const auto* v6 = reinterpret_cast<const sockaddr_in6*>(addr);  // NOLINT(*reinterpret-cast) from type-erased C-struct
        return {.host = str_or_empty(inet_ntop(addr->sa_family, &v6->sin6_addr, buf.data(), sizeof(buf))),
                .port = ntohs(v6->sin6_port)};
      }
      default:
        return {};
    }
  }
  [[nodiscard]] static host_port from(const addrinfo* ai) { return from(ai->ai_addr); }
  [[nodiscard]] static host_port from(const unique_addr& addr) { return from(addr.get()); }
  [[nodiscard]] static host_port from(const type_erased_sockaddr& addr) { return from(addr.get()); }
  [[nodiscard]] static host_port from(int fd) { return from(type_erased_sockaddr::from(fd)); }

  [[nodiscard]] std::string string() const { return std::format("{}:{}", uri_host(host), port); }
  bool operator==(const host_port&) const = default;
};

template <class T>
[[nodiscard]] std::expected<void, error> setsockopt(int fd, int level, int option_name, const T& value) {
  return zero_or_errno(::setsockopt(fd, level, option_name, &value, sizeof(value)))
      .transform_error([=](auto ec) { return error(ec, "setsockopt({}, {}, {})", fd, level, option_name); });
}

[[nodiscard]] std::expected<void, error> inline linger(int fd, std::chrono::seconds timeout) {
  return setsockopt(fd, SOL_SOCKET, SO_LINGER,
                    ::linger{.l_onoff = 1, .l_linger = static_cast<int>(timeout.count())});
}

/// An owned socket descriptor that simplifies common network usage.
class unique_socket : public unique_fd {
 public:
  explicit unique_socket(int fd) : unique_fd(fd) {}

  [[nodiscard]] static expected_or_errno<std::pair<unique_socket, unique_socket>> pipes(int domain = AF_UNIX, int type = SOCK_STREAM) {
    std::array<int, 2> sv{-1, -1};
    return zero_or_errno(socketpair(domain, type, 0, sv.data()))
        .transform([&sv] { return std::make_pair(unique_socket(sv[0]), unique_socket(sv[1])); });
  }

  [[nodiscard]] static std::expected<unique_socket, error> bound(
      const unique_addr& source = {"::", "0"},
      std::optional<int> domain = {},
      std::optional<int> type = {},
      std::optional<int> protocol = {},
      const std::function<void(unique_socket&)>& before_bind = [](auto&) {}) {
    for (const auto* p = source.get(); p != nullptr; p = p->ai_next) {
      if (int fd = ::socket(domain.value_or(p->ai_family), type.value_or(p->ai_socktype), protocol.value_or(p->ai_protocol)); fd >= 0) {
        unique_socket ufd(std::move(fd));
        before_bind(ufd);
        if (::bind(fd, p->ai_addr, p->ai_addrlen) == 0) return ufd;
      }
    }
    return std::unexpected(error(errno, "socket/bind({})", source.string()));
  }
  [[nodiscard]] static std::expected<unique_socket, error> udp(
      const unique_addr& source = {"::", "0"},
      std::optional<int> domain = {},
      std::optional<int> protocol = IPPROTO_UDP,
      const std::function<void(unique_socket&)>& before_bind = [](auto&) {}) {
    return bound(source, domain, SOCK_DGRAM, protocol, before_bind);
  }
  [[nodiscard]] static std::expected<unique_socket, error> tcp(
      const unique_addr& source = {"::", "0"},
      std::optional<int> domain = {},
      std::optional<int> protocol = IPPROTO_TCP,
      const std::function<void(unique_socket&)>& before_bind = [](auto&) {}) {
    return bound(source, domain, SOCK_STREAM, protocol, [&](auto& fd) {
      unwrap(setsockopt(*fd, SOL_SOCKET, SO_REUSEADDR, 1));
      before_bind(fd);
    });
  }

  std::vector<error> linger(std::chrono::seconds timeout) && {
    std::vector<error> errors;
    if (auto status = jl::linger(fd(), timeout); !status) errors.push_back(status.error());
    if (int fd = release(); close(fd) != 0) errors.emplace_back(error(errno, "close({})", fd));  // NOLINT(*-emplace)
    return errors;
  }
  std::vector<error> terminate() && {
    return std::move(*this).linger(std::chrono::seconds(0));
  }
};

template <class C>
  requires std::constructible_from<std::span<const typename C::value_type>, C>
[[nodiscard]] size_t send(int fd, const C& data, int flags = 0) {
  constexpr size_t size = sizeof(typename C::value_type);
  return unwrap(ok_or_errno(::send(fd, data.data(), size * data.size(), flags))) / size;
}
[[nodiscard]] size_t inline send(int fd, std::string_view data, int flags = 0) {
  return send(fd, std::span(data), flags);
}

template <class T>
[[nodiscard]] std::span<T> recv(int fd, std::span<T> buffer, int flags = 0) {
  auto n = unwrap(ok_or_errno(::recv(fd, buffer.data(), sizeof(T) * buffer.size(), flags)));
  return buffer.subspan(0, n / sizeof(T));
}
template <class C>
  requires std::constructible_from<std::span<const typename C::value_type>, C>
[[nodiscard]] std::span<typename C::value_type> recv(int fd, C& data, int flags = 0) {
  return recv(fd, std::span(data), flags);
}
[[nodiscard]] std::string_view inline recv(int fd, std::string& buffer, int flags = 0) {
  auto result = recv(fd, std::span(buffer), flags);
  return {result.data(), result.size()};
}
void inline bind(int fd, const unique_addr& source = unique_addr("", "0")) {
  for (const auto* p = source.get(); p != nullptr; p = p->ai_next) {
    if (::bind(fd, p->ai_addr, p->ai_addrlen) == 0) return;
  }
  throw error(errno, "bind({})", source.string());
}

void inline connect(int fd, const type_erased_sockaddr& addr) {
  if (::connect(fd, addr.get(), addr.length) != 0) {
    throw error(errno, "connect({})", host_port::from(addr.get()).string());
  }
}

void inline connect(int fd, const unique_addr& source) {
  for (const auto* p = source.get(); p != nullptr; p = p->ai_next) {
    if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) return;
  }
  throw error(errno, "connect({})", source.string());
}

void inline listen(int fd, int backlog) {
  std::ignore = unwrap(ok_or_errno(::listen(fd, backlog))
                           .transform_error([fd](auto ec) { return error(ec, "listen({})", fd); }));
}
[[nodiscard]] inline expected_or_errno<std::pair<unique_socket, host_port>> accept(int fd, int flags = 0) {
  type_erased_sockaddr addr;
  return ok_or_errno(::accept4(fd, addr.get(), &addr.length, flags))
      .transform([&addr](int fd) { return std::make_pair(unique_socket(fd), host_port::from(addr.get())); });
}

/// An abstraction for managing the POSIX "span" structures required by
/// multi-message system calls like recvmmsg/sendmmsg.
template <class T = char>
  requires std::is_trivially_copyable_v<T>
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

  /// WARN: new_count only updates the length field in the message header, so
  /// it assumes that the original message buffer is large enough for this.
  ///
  /// @returns the buffer for the message at idx.
  [[nodiscard]] std::span<T> buffer(size_t idx, std::optional<size_t> new_count = std::nullopt) {
    if (new_count) {
      _iovecs[idx].iov_len = sizeof(T) * *new_count;
    }
    // NOLINTNEXTLINE(*reinterpret-cast) is safe because iov_base originate from std::span<T>.data()
    T* base = reinterpret_cast<T*>(_iovecs[idx].iov_base);
    return {base, base + _iovecs[idx].iov_len / sizeof(T)};
  }
  /// WARN: Assumes that the message buffer is large enough to fit data.
  void write(size_t idx, std::span<const T> data) {
    auto buf = buffer(idx, data.size());
    std::copy(data.begin(), data.end(), buf.begin());
  }
  [[nodiscard]] mmsghdr& message(size_t idx) { return _msgs[idx]; }

  /// Sends message buffers off through off + count.
  /// @returns the number of messages sent.
  [[nodiscard]] size_t sendmmsg(off_t off = 0, std::optional<size_t> count = std::nullopt, int flags = MSG_WAITFORONE) {
    return unwrap(ok_or_errno(::sendmmsg(*_fd, &_msgs[off], count.value_or(_msgs.size() - off), flags)));
  }

  /// Receives message into buffers off through off + count. Returned spans are
  /// valid until further operations on those same message slots
  [[nodiscard]] std::span<std::span<T>> recvmmsg(off_t off = 0, std::optional<size_t> count = std::nullopt, int flags = MSG_WAITFORONE) {
    int msgs = unwrap(ok_or_errno(::recvmmsg(*_fd, &_msgs[off], count.value_or(_msgs.size() - off), flags, nullptr)));
    _received.resize(msgs);
    auto available_as_span = [](mmsghdr& msg) {
      // NOLINTNEXTLINE(*reinterpret-cast) is safe because iov_base originate from std::span<T>.data()
      T* base = reinterpret_cast<T*>(msg.msg_hdr.msg_iov->iov_base);
      return std::span{base, base + msg.msg_len / sizeof(T)};
    };
    std::ranges::copy(_msgs | std::views::take(msgs) | std::views::transform(available_as_span), _received.begin());
    return _received;
  }

  [[nodiscard]] unique_socket& fd() noexcept { return _fd; }

  template <std::ranges::sized_range RR, std::ranges::contiguous_range R = std::ranges::range_value_t<RR>>
    requires std::same_as<T, std::ranges::range_value_t<R>>
  void reset(RR&& buffers) {
    _iovecs.resize(std::ranges::size(buffers));
    std::ranges::copy(buffers | std::views::transform([](auto&& s) { return as_iovec(s); }), _iovecs.begin());

    _msgs.resize(std::ranges::size(buffers));
    auto mmsghdr_to = [](iovec& iovec) {
      mmsghdr msg{};
      msg.msg_hdr.msg_iov = &iovec;
      msg.msg_hdr.msg_iovlen = 1;
      return msg;
    };
    std::ranges::copy(_iovecs | std::views::transform(mmsghdr_to), _msgs.begin());
  }

  ~mmsg_socket() = default;
  mmsg_socket(const mmsg_socket&) = delete;
  mmsg_socket& operator=(const mmsg_socket&) = delete;
  mmsg_socket(mmsg_socket&&) noexcept = default;
  mmsg_socket& operator=(mmsg_socket&&) noexcept = default;
};

/// Same as mmsg_socket, but with a self-managed buffer
template <class T = char>
  requires std::is_trivially_copyable_v<T>
class mmsg_buffer : public mmsg_socket<T> {
  std::vector<T> _buffer;

 public:
  /// WARN: The mtu (Maximum Transfer Unit) size is an upper limited to the
  /// size of messages that are sent or received. Writing messages larger than
  /// that would overflow its buffer, and received messages could be truncated
  /// to fit in the buffers (see `man recvmsg` for details).
  mmsg_buffer(unique_socket fd, size_t msgs, size_t mtu = 1500)
      : mmsg_socket<T>(std::move(fd), {}), _buffer(msgs * mtu) {
    mmsg_socket<T>::reset(chunked(std::span(_buffer), mtu));
  }

  void reset(std::span<std::span<T>> buffers) = delete;

  ~mmsg_buffer() = default;
  mmsg_buffer(const mmsg_buffer&) = delete;
  mmsg_buffer& operator=(const mmsg_buffer&) = delete;
  mmsg_buffer(mmsg_buffer&&) noexcept = default;
  mmsg_buffer& operator=(mmsg_buffer&&) noexcept = default;
};

/// An owned and managed memory mapped span.
template <class T>
  requires std::is_trivially_copyable_v<T>
class unique_mmap {
  std::span<T> _map;

 public:
  /// A common mmap. The count/offset parameters are in counts of T, not bytes.
  [[nodiscard]] static expected_or_errno<unique_mmap<T>> map(size_t count, int prot = PROT_NONE, int flags = MAP_SHARED, int fd = -1, off_t offset = 0) {
    return map(nullptr, count, prot, flags, fd, offset);
  }

  /// More advanced constructor for e.g. MAP_FIXED when you need the addr
  /// parameter. The size/offset parameters are in counts of T, not bytes.
  [[nodiscard]] static expected_or_errno<unique_mmap<T>> map(void* addr, size_t count, int prot = PROT_NONE, int flags = MAP_SHARED, int fd = -1, off_t offset = 0) {
    return ok_mmap<T>(mmap(addr, count * sizeof(T), prot, flags, fd, offset * sizeof(T)))
        .transform([count](T* addr) { return unique_mmap<T>(std::span{addr, count}); });
  }

  static std::expected<unique_mmap<T>, error> anon(size_t count, int prot = PROT_NONE, const std::string& name = "unique_mmap", int flags = MAP_ANONYMOUS | MAP_PRIVATE) {
    return map(count, prot, flags, -1, 0)
        .transform([count, &name](unique_mmap<T> map) {
#ifdef PR_SET_VMA
          std::ignore = prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, &map[0], count * sizeof(T), name.c_str());  // best effort, so okay if it fails silently
#endif
          return map;
        })
        .transform_error([count, &name](auto ec) { return error(ec, "unique_mmap({} {}B)", name, count * sizeof(T)); });
  }

  [[nodiscard]] auto& operator[](this auto& self, size_t idx) noexcept {
    return self._map[idx];
  }

  [[nodiscard]] auto& operator*(this auto& self) noexcept {
    return self._map;
  }
  [[nodiscard]] auto* operator->(this auto& self) noexcept {
    return &self._map;
  }

  /// The count parameter is in counts of T not bytes.
  /// @throws error with errno and errmsg if it fails.
  [[nodiscard]] expected_or_errno<void> remap(size_t count, int flags = 0, void* addr = nullptr) {
    return ok_mmap<T>(mremap(const_cast<std::remove_const_t<T>*>(_map.data()), _map.size() * sizeof(T), count * sizeof(T), flags, addr))
        .transform([count, this](T* p) { _map = {p, count}; });
  }

  void reset(std::span<T> map = {}) noexcept {
    if (auto old = std::exchange(_map, map); !old.empty()) {
      ::munmap(const_cast<std::remove_const_t<T>*>(old.data()), old.size() * sizeof(T));
    }
  }
  std::span<T> release() noexcept {
    return std::exchange(_map, {});
  }
  ~unique_mmap() noexcept {
    reset();
  }
  unique_mmap(const unique_mmap&) = delete;
  unique_mmap& operator=(const unique_mmap&) = delete;
  unique_mmap(unique_mmap&& other) noexcept : _map(other.release()) {}
  unique_mmap& operator=(unique_mmap&& other) noexcept {
    reset(other.release());
    return *this;
  }

 private:
  explicit unique_mmap(std::span<T> map) : _map(map) {}
};

/// An owned and managed file descriptor and mapping to its contents
template <class T>
  requires std::is_trivially_copyable_v<T>
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
  [[nodiscard]] static std::expected<fd_mmap<T>, error> map(unique_fd fd, int prot = PROT_READ, int flags = MAP_SHARED, off_t offset = 0, std::optional<size_t> count = std::nullopt) {
    auto available = [&]() -> std::expected<size_t, error> {
      if (count) return *count;
      return jl::stat(fd.fd()).transform([](struct stat st) { return st.st_size / sizeof(T); });
    }();
    if (!available) return std::unexpected(available.error());

    auto mmap = jl::unique_mmap<T>::map(count.value_or(beyond_offset(*available, offset)), prot, flags, fd.fd(), offset);
    if (!mmap) return std::unexpected(error(mmap.error(), "fd_mmap({})", fd.fd()));

    auto reported = count || offset < static_cast<off_t>(*available) ? **mmap : (**mmap).subspan(0, 0);
    return fd_mmap(offset, std::move(fd), std::move(*mmap), reported);
  }

  [[nodiscard]] static std::expected<fd_mmap<T>, error> open(const std::filesystem::path& path, int mode, int flags = MAP_SHARED, off_t offset = 0, std::optional<size_t> count = std::nullopt) {  // NOLINT(*-swappable-parameters)

    int prot = PROT_READ | (mode != O_RDONLY ? PROT_WRITE : 0);
    return unique_fd::open(path, mode)
        .and_then([&](auto fd) { return fd_mmap<T>::map(std::move(fd), prot, flags, offset, count); })
        .transform_error([&](const error& e) { return error(e.code(), "fd_mmap({}, 0x{:x})", path.c_str(), mode); });
  }

  [[nodiscard]] int fd() const noexcept { return _fd.fd(); }

  [[nodiscard]] auto& operator[](this auto& self, size_t idx) noexcept { return self._map[idx]; }

  [[nodiscard]] auto& operator*(this auto& self) noexcept { return self._map; }
  [[nodiscard]] auto* operator->(this auto& self) noexcept { return &self._map; }

  /// The count parameter is in counts of T not bytes. New mapping is relative
  /// to offset from construction.
  /// @throws error with errno and errmsg if it fails.
  [[nodiscard]] expected_or_errno<void> remap(size_t count, int mremap_flags = 0) {
    return _mmap.remap(count, mremap_flags)
        .transform([this] { _map = *_mmap; });
  }

  /// Remove the mmap and release the fd from construction.
  unique_fd unmap() && {
    _mmap.reset();
    return std::move(_fd);
  }

  /// Truncate the file to this length. Length is in counts of T not bytes.
  /// Length is relative to start of file, but remapping is relative to offset
  /// from construction.
  [[nodiscard]] std::expected<void, error> truncate(size_t length, int mremap_flags = 0) {
    return jl::truncate(*_fd, length * sizeof(T))
        .and_then([=, this] { return _mmap.remap(beyond_offset(length, _offset), mremap_flags)
                                  .transform_error([=](auto ec) { return error(ec, "mremap({}, {})", length, mremap_flags); }); })
        .transform([=, this] { _map = _offset < static_cast<off_t>(length) ? *_mmap : _mmap->subspan(0, 0); });
  }

 private:
  fd_mmap(off_t offset, unique_fd fd, unique_mmap<T> mmap, std::span<T> available)
      : _offset(offset), _fd(std::move(fd)), _mmap(std::move(mmap)), _map(available) {}

  [[nodiscard]] static size_t beyond_offset(size_t count, off_t offset) {
    if (static_cast<off_t>(count) <= offset) return 1;  // 0-sized mmaps are not allowed, so always map something
    return count - offset;
  }
};

/// A circular (aka. ring) buffer with support for copy-free read/write of
/// contiguous elements anywhere in the buffer, even across the wrap-around
/// threshold. Given an atomic Index type, one writer and one reader can safely
/// use this to share data across threads. Even so, it is not thread-safe to
/// use this if there are multiple readers or writers.
template <class T, size_t Capacity, class Index = uint32_t>
  requires std::is_trivially_copyable_v<T>  // because the wrap-around functionality uses mmaps
class CircularBuffer {
  static_assert((sizeof(T) * Capacity) % (4 << 10) == 0,
                "CircularBuffer byte capacity must be page aligned");

  unique_mmap<T> _data;
  RingIndex<Index, Capacity> _fifo;
  size_t _producers_write = 0;
  size_t _consumers_read = 0;

 public:
  [[nodiscard]] static std::expected<CircularBuffer, error> create(const std::string& mmap_name = "CircularBuffer") {
    // _data is a continues virtual memory span twice as big as the Capacity
    // where the first and second half is mapped to the same shared buffer.
    // This gives a circular buffer that supports continuous memory spans even
    // when those bytes span across the wrap-around threshold, because
    // &_data[0] and &_data[Capacity] are both valid addresses that essentially
    // refer to the same physical address.
    //
    // This concept/trick originates from https://github.com/willemt/cbuffer
    return prepare_data(mmap_name).transform([](unique_mmap<T> data) { return CircularBuffer(std::move(data)); });
  }

  /// same as create(...), but throws on unexpected error
  explicit CircularBuffer(const std::string& mmap_name = "CircularBuffer") : _data(unwrap(prepare_data(mmap_name))) {}

  /// @returns a span where you can write new data into the buffer where. Its
  /// size is limited to the amount of free space available.
  [[nodiscard]] std::span<T> peek_back(size_t max) noexcept {
    auto [write, available] = _fifo.write_free(max);
    return {&_data[write % Capacity], std::min(max, static_cast<size_t>(available))};
  }

  /// "Give back" the part at the beginning of the span from peek_back() where
  /// you wrote data.
  size_t commit_written(this auto& self, std::span<T>&& written) noexcept {
    self._producers_write += written.size();
    self._fifo.store_write(self._producers_write);
    return written.size();
  }

  /// @returns a span where you can read available data from the buffer. Its
  /// size is limited by the amount of available data.
  [[nodiscard]] auto peek_front(this auto& self, size_t max) noexcept {
    auto [read, available] = self._fifo.read_filled(max);
    return std::span{&self._data[read % Capacity], std::min(max, static_cast<size_t>(available))};
  }

  /// "Give back" the part at the beginning of the span from peek_front() that
  /// you read.
  size_t commit_read(std::span<const T>&& read) noexcept {
    _consumers_read += read.size();
    _fifo.store_read(_consumers_read);
    return read.size();
  }

  /// @returns the amount of data available to be read. In a threaded
  /// environment where there is exactly one reader and one writer this is
  /// respectively the lower and the upper bound for the current size.
  [[nodiscard]] size_t size() const noexcept { return _fifo.size(); }
  [[nodiscard]] bool empty() const noexcept { return size() == 0; }
  [[nodiscard]] size_t capacity() const noexcept { return Capacity; }

  /// Writes elements from data into the buffer.
  /// @returns the number of elements copied, and appended to the buffer.
  [[nodiscard]] size_t push_back(const std::span<T> data) noexcept {
    auto writeable = peek_back(data.size());
    std::copy(data.begin(), data.begin() + writeable.size(), writeable.begin());
    return commit_written(std::move(writeable));
  }

  /// Read elements from the buffer into data.
  /// @returns the number of elements copied and erased from the buffer.
  [[nodiscard]] size_t fill_from_front(std::span<T> data) noexcept {
    auto readable = peek_front(data.size());
    std::copy(readable.begin(), readable.end(), data.begin());
    return commit_read(std::move(readable));
  }

  /// @returns elements read and copied from the buffer.
  [[nodiscard]] std::vector<T> pop_front(size_t max) {
    auto readable = peek_front(max);
    std::vector<T> output(readable.size());
    output.resize(fill_from_front(output));
    return output;
  }

 private:
  explicit CircularBuffer(unique_mmap<T> data) : _data(std::move(data)) {}

  [[nodiscard]] static std::expected<unique_mmap<T>, error> prepare_data(const std::string& mmap_name = "CircularBuffer") {
    // _data is a continues virtual memory span twice as big as the Capacity
    // where the first and second half is mapped to the same shared buffer.
    // This gives a circular buffer that supports continuous memory spans even
    // when those bytes span across the wrap-around threshold, because
    // &_data[0] and &_data[Capacity] are both valid addresses that essentially
    // refer to the same physical address.
    //
    // This concept/trick originates from https://github.com/willemt/cbuffer
    return unique_mmap<T>::anon(Capacity * 2, PROT_NONE, mmap_name)
        .and_then([](unique_mmap<T> data) -> std::expected<unique_mmap<T>, error> {
          int prot = PROT_READ | PROT_WRITE;
          int flags = MAP_FIXED | MAP_SHARED;
          constexpr off_t len = Capacity * sizeof(T);

          auto fd = tmpfd::unlinked();
          if (!fd) return std::unexpected(fd.error());
          return jl::truncate(fd->fd(), len)
              .and_then([&] { return ok_mmap(mmap(data->data(), len, prot, flags, fd->fd(), 0))
                                  .transform_error([](auto ec) { return error(ec, "mmap(CircularBuffer data)"); }); })
              .and_then([&](auto) { return ok_mmap(mmap(data->data() + Capacity, len, prot, flags, fd->fd(), 0))
                                        .transform_error([](auto ec) { return error(ec, "mmap(CircularBuffer shadow)"); }); })
              .transform([&](auto) { return std::move(data); });
        })
        .transform_error([&](const error& e) { return e.prefixed("CircularBuffer({}): ", mmap_name); });
  }
};

}  // namespace jl
