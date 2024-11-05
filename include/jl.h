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

#include <algorithm>
#include <bit>
#include <cassert>
#include <charconv>
#include <chrono>
#include <climits>
#include <cstring>
#include <filesystem>
#include <format>
#include <functional>
#include <future>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

#if __cpp_concepts >= 202002L
#include <expected>
#elif __clang_major__ >= 17
#define jl_save__cpp_concepts
#pragma clang diagnostic ignored "-Wbuiltin-macro-redefined"
#define __cpp_concepts 202002L  // this is probably stupid, but I really want std::expected

#include <expected>

#pragma clang diagnostic ignored "-Wmacro-redefined"
#define __cpp_concepts jl_save__cpp_concepts
#undef jl_save__cpp_concepts
#endif

// BTW, see e.g. https://stackoverflow.com/a/52158819 about pairwise sharing in newer Intel CPUs
#ifdef __cpp_lib_hardware_interference_size
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winterference-size"
constexpr std::size_t hardware_constructive_interference_size = std::hardware_constructive_interference_size;
constexpr std::size_t hardware_destructive_interference_size = std::hardware_destructive_interference_size;
#pragma GCC diagnostic pop
#else  // on most architectures
constexpr std::size_t hardware_constructive_interference_size = 64;
constexpr std::size_t hardware_destructive_interference_size = 64;
#endif

/// Johs's <mail@johslarsen.net> Library. Use however you see fit.
namespace jl {

template <typename T>
concept numeric = std::integral<T> || std::floating_point<T>;

template <typename T, typename U>
concept bitcastable_to = requires(T t) { std::bit_cast<U>(t); };

template <typename T, typename... Us>
concept any_of = (... || std::same_as<std::remove_cvref_t<T>, Us>);

template <class... Ts>
struct overload : Ts... {
  using Ts::operator()...;
};

/// @returns expected value or throw its error
/// Like: https://doc.rust-lang.org/std/result/enum.Result.html#method.unwrap
template <typename T, typename E>
[[nodiscard]] T unwrap(std::expected<T, E> &&expected) {
  if (!expected.has_value()) throw std::move(expected.error());
  if constexpr (!std::is_void_v<T>) return std::move(*expected);
}

/// @returns promised value now or throw if it is not ready without blocking
template <typename T>
[[nodiscard]] T unwrap(std::future<T> future) {
  if (future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
    throw std::runtime_error("unwrapped future was not ready");
  }
  return future.get();
}

/// @returns iterator to the first ready future without blocking
[[nodiscard]] auto any_ready_of(std::ranges::range auto &&futures) {
  return std::ranges::find_if(futures, [](auto &&f) {
    return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
  });
}

/// @returns std::expected with the given value or the given error
/// Like: https://doc.rust-lang.org/std/option/enum.Option.html#method.ok_or
template <typename T, typename E>
[[nodiscard]] std::expected<T, E> ok_or(std::optional<T> opt, E error) noexcept {
  if (opt) return *opt;
  return std::unexpected(std::move(error));
}
/// @returns std::expected with the given value or the result from calling error
/// Like: https://doc.rust-lang.org/std/option/enum.Option.html#method.ok_or_else
template <typename T, std::invocable F>
[[nodiscard]] std::expected<T, std::invoke_result_t<F>> ok_or_else(std::optional<T> opt, F error) noexcept {
  if (opt) return *opt;
  return std::unexpected(error());
}

template <class... Args>
[[nodiscard]] inline std::system_error make_system_error(std::errc err, std::format_string<Args...> fmt, Args &&...args) noexcept {
  return {std::make_error_code(err), std::format(fmt, std::forward<Args>(args)...)};
}
template <class... Args>
[[nodiscard]] inline std::unexpected<std::system_error> unexpected_system_error(std::errc err, std::format_string<Args...> fmt, Args &&...args) noexcept {
  return std::unexpected(make_system_error(err, fmt, std::forward<Args>(args)...));
}

template <class... Args>
[[nodiscard]] inline std::system_error errno_as_error(std::format_string<Args...> fmt, Args &&...args) noexcept {
  return make_system_error(std::errc(errno), fmt, std::forward<Args>(args)...);
}
template <class... Args>
[[nodiscard]] inline std::unexpected<std::system_error> unexpected_errno(std::format_string<Args...> fmt, Args &&...args) noexcept {
  return std::unexpected(errno_as_error(fmt, std::forward<Args>(args)...));
}
/// @returns non-negative n, 0 for EAGAIN or unexpected_errno(...)
template <std::integral T, class... Args>
std::expected<T, std::system_error> ok_or_errno(T n, std::format_string<Args...> fmt, Args &&...args) {
  if (n >= 0) return n;
  if (errno == EAGAIN) return 0;
  return unexpected_errno(fmt, std::forward<Args>(args)...);
}

template <std::integral T, class... Args>
std::expected<void, std::system_error> zero_or_errno(T n, std::format_string<Args...> fmt, Args &&...args) {
  if (n == 0) return {};
  return unexpected_errno(fmt, std::forward<Args>(args)...);
}

template <typename T = void, class... Args>
std::expected<T *, std::system_error> ok_mmap(void *p, std::format_string<Args...> fmt, Args &&...args) {
  if (p == MAP_FAILED) return unexpected_errno(fmt, std::forward<Args>(args)...);
  return reinterpret_cast<T *>(p);
}

/// Utility to run a method at the end of the scope like a defer statement in Go
template <std::invocable F>
  requires std::is_void_v<std::invoke_result_t<F>>
class defer {
  F _f;

 public:
  [[nodiscard]] explicit defer(F f) : _f(f) {}
  ~defer() noexcept { _f(); }

  defer(const defer &) = delete;
  defer &operator=(const defer &) = delete;
  defer(defer &&) = delete;
  defer &operator=(defer &&) = delete;
};

static_assert(EAGAIN == EWOULDBLOCK, "Obscure and unsupported platform");

/// @returns n usually or 0 for EAGAIN
/// @throws std::system_error on other errors
template <std::integral T, class... Args>
T check_rw_error(T n, std::format_string<Args...> fmt, Args &&...args) {
  if (n < 0) {
    if (errno == EAGAIN) return 0;
    throw errno_as_error(fmt, std::forward<Args>(args)...);
  }
  return n;
}

/// Retry the f(...) wrapping a system call failing with EAGAIN.
/// @returns the non-negative successful result or the error that occurred.
template <int64_t Attempts = 3, std::invocable F, class... Args>
  requires std::integral<std::invoke_result_t<F>>
std::expected<std::invoke_result_t<F>, std::system_error> retry(F f, std::format_string<Args...> fmt, Args &&...args) {
  for (int64_t attempts = Attempts; attempts != 0; --attempts) {
    if (auto result = f(); result >= 0) return result;  // successful, so exit early
    if (errno != EAGAIN) break;
  }
  return unexpected_errno(fmt, std::forward<Args>(args)...);
}

/// Repeat f(...) wrapping read/write/... operations until the whole input is processed.
/// @returns the amount processed. Usually length unless call returns 0 to indicate EOF.
template <int64_t Attempts = 3, std::invocable<size_t, off_t> F, class... Args>
  requires std::integral<std::invoke_result_t<F, size_t, off_t>>
std::expected<size_t, std::system_error> rw_loop(F f, size_t length, std::format_string<Args...> fmt, Args &&...args) {
  size_t offset = 0;
  for (size_t count = -1; offset < length && count != 0; offset += count) {
    auto result = jl::retry<Attempts>([&] { return f(length - offset, offset); }, fmt, std::forward<Args>(args)...);
    if (!result.has_value()) return std::unexpected(result.error());

    count = *result;
    if (count == 0) break;  // EOF
  }
  return offset;
}

template <typename T>
[[nodiscard]] inline T *nullable(std::optional<T> &opt) {
  if (opt.has_value()) return &(*opt);
  return nullptr;
}

[[nodiscard]] inline std::string str_or_empty(const char *str) {
  return {str == nullptr ? "" : str};
}
template <numeric T>
[[nodiscard]] std::expected<T, std::system_error> from_str(std::string_view s) noexcept {
  std::expected<T, std::system_error> parsed;
  if (auto res = std::from_chars(s.begin(), s.end(), *parsed); res.ec == std::errc()) {
    return parsed;
  } else {
    return unexpected_system_error(res.ec, "Failed to parse \"{}\"", s);
  }
}

/// @returns index of the first unescaped ch or std::string::npos.
/// @returns size-1 if that happens to be an incomplete escape sequence
[[nodiscard]] inline size_t find_unescaped(std::string_view haystack, char ch, size_t pos = 0, char escape = '\\') {  // NOLINT(*-swappable-parameters)
  std::string pattern = {escape, ch};
  while (pos < haystack.size()) {
    pos = haystack.find_first_of(pattern, pos);
    if (pos == std::string::npos) break;
    if (haystack[pos] != escape) return pos;     // unescaped match
    if (pos + 1 == haystack.size()) return pos;  // ends with an incomplete escape sequence

    pos += 2;  // character after the escaped one;
  }
  return std::string::npos;
}
/// @returns index of the first unescaped character matching pattern or std::string::npos.
/// @returns size-1 if that happens to be an incomplete escape sequence
template <std::predicate<char> F>
[[nodiscard]] inline size_t find_unescaped(std::string_view haystack, F needles, size_t pos = 0, char escape = '\\') {
  for (; pos < haystack.size(); ++pos) {
    if (haystack[pos] == escape) {
      if (pos + 1 == haystack.size()) return pos;  // ends with an incomplete escape sequence
      ++pos;                                       // skip next char
    } else if (needles(haystack[pos])) {
      return pos;  // unescaped match
    }
  }
  return std::string::npos;
}

/// @returns true if the Blacklist characters in str needs to be quoted
template <typename Blacklist = decltype([](unsigned char ch) { return std::isalnum(ch) == 0; })>
  requires std::predicate<Blacklist, char>
[[nodiscard]] inline bool needs_quotes(
    std::string_view str,
    char delim = '"',
    char escape = '\\') {
  auto quote_or_blacklisted = [delim](char ch) {
    return ch == delim || Blacklist()(ch);
  };
  for (size_t pos = 0; pos < str.size(); ++pos) {
    pos = find_unescaped(str, quote_or_blacklisted, pos, escape);
    if (pos == std::string::npos) return false;  // end of string without any need for quotes
    if (str[pos] != delim) return true;          // found a blacklisted character (or ends with and incomplete escape sequence)

    pos = find_unescaped(str, delim, pos + 1, escape);               // end of quoted section
    if (pos == std::string::npos || str[pos] != delim) return true;  // mismatched quotes or incomplete escape sequence
  }
  return false;  // end of string without any need for quotes
}

/// An I/O manipulator that inserts str std::quoted if it needs to be or as is
/// if it is already properly quoted or if there are no Blacklist characters in it.
template <typename Blacklist = decltype([](unsigned char ch) { return std::isalnum(ch) == 0; })>
  requires std::predicate<Blacklist, char>
struct MaybeQuoted {
  std::string_view _str;
  char _delim, _escape;

  explicit MaybeQuoted(std::string_view str, char delim = '"', char escape = '\\')  // NOLINT(*-swappable-parameters) to mimic std::quoted
      : _str(str), _delim(delim), _escape(escape) {}
};
template <std::predicate<char> Blacklist>
inline std::ostream &operator<<(std::ostream &os, const MaybeQuoted<Blacklist> &mq) {
  if (needs_quotes<Blacklist>(mq._str, mq._delim, mq._escape)) {
    return os << std::quoted(mq._str, mq._delim, mq._escape);
  }
  return os << mq._str;
}

template <typename R>
  requires std::ranges::input_range<R> && std::constructible_from<std::string, std::ranges::range_value_t<R>>
inline std::string join(const R &&words, const std::string &delimiter = ",") {
  if (std::ranges::empty(words)) return "";

  std::string first(*std::ranges::begin(words));
  return std::ranges::fold_left(words | std::views::drop(1), std::move(first), [delimiter](std::string s, const auto &w) {
    return s += delimiter + w;
  });
}

struct line_eol {
  std::string_view line;
  std::string_view eol;

  size_t size() { return line.size() + eol.size(); }

  static line_eol find_first_in(std::string_view s) {
    auto eol = s.find_first_of("\r\n");
    if (eol == std::string::npos) return {s, s.substr(s.size())};

    auto line = s.substr(0, eol);
    size_t n = s[eol] == '\r' && s.size() > eol + 1 && s[eol + 1] == '\n' ? 2 : 1;
    return {line, s.substr(eol, n)};
  }
};

/// Container that can be used to pass string literal as template parameter.
/// NOTE: Like std::string/_view initializer, the stored string is not null-terminated!
template <size_t N>
struct fixed_string {
  std::array<char, N - sizeof('\0')> chars;
  constexpr fixed_string(const char (&str)[N]) {  // NOLINT(*-member-init, *-explicit-*, *-c-arrays)
    std::copy(str, str + N - sizeof('\0'), chars.data());
  }
  auto operator<=>(const fixed_string &) const = default;
};

// clang-format off
/// Type trait to select a same sized integer based on the size of a type
template <size_t N> struct uint_from_size {};
template <> struct uint_from_size<1> { using type = uint8_t; };
template <> struct uint_from_size<2> { using type = uint16_t; };
template <> struct uint_from_size<4> { using type = uint32_t; };
template <> struct uint_from_size<8> { using type = uint64_t; };
// clang-format on

/// @returns returns byteswapped n on little-endian architectures
template <std::integral Int>
constexpr Int be(Int n) noexcept {
  static_assert(std::endian::native == std::endian::big || std::endian::native == std::endian::little,
                "jl::be only supported on big/little-endian architectures");
  if constexpr (std::endian::native == std::endian::little) {
    return std::byteswap(n);
  } else {
    return n;
  }
}
template <typename T, typename U = uint_from_size<sizeof(T)>::type>
  requires(!std::integral<T>)
constexpr T be(T n) noexcept {
  if constexpr (std::endian::native == std::endian::little) {
    return std::bit_cast<T>(be(std::bit_cast<U>(n)));
  } else {
    return n;
  }
}
/// @returns returns byteswapped n on big-endian architectures
template <std::integral Int>
constexpr Int le(Int n) noexcept {
  static_assert(std::endian::native == std::endian::big || std::endian::native == std::endian::little,
                "jl::le only supported on big/little-endian architectures");
  if constexpr (std::endian::native == std::endian::big) {
    return std::byteswap(n);
  } else {
    return n;
  }
}
template <typename T, typename U = uint_from_size<sizeof(T)>::type>
  requires(!std::integral<T>)
constexpr T le(T n) noexcept {
  if constexpr (std::endian::native == std::endian::big) {
    return std::bit_cast<float>(le(std::bit_cast<U>(n)));
  } else {
    return n;
  }
}

/// @returns ceil(x/y)
auto div_ceil(std::unsigned_integral auto x, std::unsigned_integral auto y) {
  return x / y + (x % y != 0);
}

/// @returns same as span.subspan(...), but empty where it would be ill-formed/UB
template <typename T, std::size_t Extent = std::dynamic_extent>
inline std::span<T, Extent> subspan(std::span<T, Extent> span, size_t offset, size_t count = std::dynamic_extent) {
  if (offset > span.size()) return {};
  if (count == std::dynamic_extent) return span.subspan(offset);
  return span.subspan(offset, std::min(span.size() - offset, count));
}

template <bitcastable_to<char> Char>
inline std::string_view view_of(std::span<Char> bytes) noexcept {
  const char *data = reinterpret_cast<const char *>(bytes.data());  // NOLINT(*reinterpret-cast) Char template requirement makes this safe
  return {data, bytes.size()};
}

template <typename ListOfSpanable>
inline std::vector<iovec> as_iovecs(ListOfSpanable &&spans) noexcept {
  std::vector<iovec> iovecs(spans.size());
  for (size_t i = 0; auto &span : spans) iovecs[i++] = {span.data(), span.size()};
  return iovecs;
}

template <typename T>
inline std::span<T> as_span(auto &&src) noexcept {
  if constexpr (std::same_as<std::remove_cvref_t<decltype(src)>, iovec>) {
    return {reinterpret_cast<T *>(src.iov_base), src.iov_len / sizeof(T)};
  } else {
    return std::span(src);
  }
}

template <typename T>
inline std::vector<std::span<T>> as_spans(std::span<iovec> iovecs) noexcept {
  std::vector<std::span<T>> spans(iovecs.size());
  for (size_t i = 0; auto &iovec : iovecs) spans[i++] = as_span<T>(iovec);
  return spans;
}

/// Copy the concatenation of list of input buffers (e.g. iovecs)
template <typename T, typename ListOfSpanable>
inline std::span<T> copy(ListOfSpanable &&source, std::span<T> dest) noexcept {
  std::span<T> copied;
  for (const auto &spanable : source) {
    if (copied.size() == dest.size()) break;

    auto part = as_span<const T>(spanable);
    auto last = part.begin() + std::min(part.size(), dest.size() - copied.size());
    auto copied_end = std::copy(part.begin(), last, dest.begin() + copied.size());
    copied = {dest.begin(), copied_end};
  }
  return copied;
}

/// A std::random_access_iterator implemented by keeping an index into a range
///
/// NOTE: actually requires std::ranges::random_access_range<R>. However, if
/// that was to be enforced, then this could not be used to implement the
/// iterator for said range, because it would not satisfy the range concept
/// needed for this until it already had a proper iterator implemented.
template <typename R, typename T = std::ranges::range_value_t<R>>
struct idx_iter {
  R *_range = nullptr;
  size_t _i = 0;

  using difference_type = ptrdiff_t;
  using value_type = T;

  [[nodiscard]] decltype(auto) operator*(this auto &self) { return (*self._range)[self._i]; }
  [[nodiscard]] decltype(auto) operator[](this auto &self, difference_type n) { return (*self._range)[self._i + n]; }

  constexpr idx_iter &operator++() { return ++_i, *this; }
  constexpr idx_iter &operator--() { return --_i, *this; }
  constexpr idx_iter operator++(int) { return std::exchange(*this, {_range, _i + 1}); }
  constexpr idx_iter operator--(int) { return std::exchange(*this, {_range, _i - 1}); }

  [[nodiscard]] constexpr difference_type operator-(const idx_iter &other) const { return _i - other._i; }
  [[nodiscard]] constexpr idx_iter operator-(difference_type n) const { return {_range, _i - n}; }
  [[nodiscard]] constexpr friend idx_iter operator+(difference_type n, const idx_iter &iter) { return {iter._range, iter.iter._i + n}; }
  [[nodiscard]] constexpr idx_iter operator+(difference_type n) const { return {_range, _i + n}; }
  [[nodiscard]] constexpr idx_iter &operator+=(difference_type n) { return _i += n, *this; }
  [[nodiscard]] constexpr idx_iter &operator-=(difference_type n) { return _i -= n, *this; }

  [[nodiscard]] constexpr bool operator==(const idx_iter &other) const { return _i == other._i; }
  [[nodiscard]] constexpr auto operator<=>(const idx_iter &other) const { return _i <=> other._i; }
};

template <typename T, std::size_t Extent = std::dynamic_extent>
class chunked {
  std::span<T, Extent> _buffer;
  size_t _n;

 public:
  using value_type = std::span<T>;

  chunked(std::span<T, Extent> buffer, size_t n) : _buffer(buffer), _n(n) {}

  class iter {
    std::span<T, Extent> _buffer{};
    size_t _n = 0, _i = std::dynamic_extent;

   public:
    using difference_type = ptrdiff_t;
    using value_type = std::span<T>;

    iter() = default;
    iter(std::span<T, Extent> buffer, size_t n, size_t i = 0)  // NOLINT(*-swappable-parameters)
        : _buffer(buffer), _n(n), _i(i) {}

    std::span<T> operator*() const { return subspan(_buffer, _i * _n, _n); }
    std::span<T> operator[](difference_type n) const { return subspan(_buffer, (_i + n) * _n, _n); }
    iter &operator++() { return ++_i, *this; }
    iter &operator--() { return --_i, *this; }
    iter operator++(int) { return std::exchange(*this, {_buffer, _n, _i + 1}); }
    iter operator--(int) { return std::exchange(*this, {_buffer, _n, _i - 1}); }

    difference_type operator-(const iter &other) const { return _i - other._i; }
    friend iter operator-(const iter &a, difference_type n) { return {a._buffer, a._n, a._i - n}; }
    friend iter operator+(difference_type n, const iter &a) { return {a._buffer, a._n, n + a._i}; }
    friend iter operator+(const iter &a, difference_type n) { return {a._buffer, a._n, a._i + n}; }
    iter &operator+=(difference_type n) {
      _i += n;
      return *this;
    }
    iter &operator-=(difference_type n) {
      _i -= n;
      return *this;
    }

    friend bool operator==(const iter &a, const iter &b) { return a._i == b._i; }
    friend std::strong_ordering operator<=>(const iter &a, const iter &b) { return a._i <=> b._i; }
  };
  iter begin() const { return iter(_buffer, _n); }
  iter end() const { return iter(_buffer, _n, div_ceil(_buffer.size(), _n)); }
};

/// A file descriptor associated with an explicit offset
struct ofd {
  int fd = -1;
  std::optional<off_t> offset = std::nullopt;
};

/// Copy up to len bytes from in to out (see `man 2 sendfile` for details).
/// NOTE: The system call requires that at most one of the file descriptors is a pipe.
std::expected<size_t, std::system_error> inline sendfileall(int fd_out, ofd in, size_t len) {
  return rw_loop([fd_out, in_fd = in.fd, in_off = nullable(in.offset)](size_t remaining, off_t) {
    return ::sendfile(fd_out, in_fd, in_off, remaining);
  },
                 len, "sendfile({} -> {})", in.fd, fd_out);
}

/// Copy up to len bytes from in to out (see `man 2 splice` for details).
/// NOTE: The system call requires that at least one of the file descriptors is a pipe.
std::expected<size_t, std::system_error> inline spliceall(ofd in, ofd out, size_t len, unsigned flags = 0) {  // NOLINT(*swappable-parameters), to mimic ::splice
  return rw_loop([flags, in = in.fd, in_off = nullable(in.offset), out = out.fd, out_off = nullable(out.offset)](size_t remaining, off_t) {
    return ::splice(in, in_off, out, out_off, remaining, flags);
  },
                 len, "splice({} -> {})", in.fd, out.fd);
}

/// Wrapper around free(...) like functions to use them as a std::unique_ptr deleter
template <auto F>
struct deleter {
  void operator()(auto *p) noexcept {
    if (p) F(p);
  }
};

/// An owned and managed file descriptor.
class unique_fd {
 protected:
  int _fd;

 public:
  /// @throws std::system_error with errno and errmsg if it fails.
  explicit unique_fd(int fd, const std::string &errmsg = "unique_fd(-1)") : _fd(fd) {
    if (_fd < 0) throw errno_as_error("{}", errmsg);
  }

  [[nodiscard]] static std::pair<unique_fd, unique_fd> pipes(int flags = O_CLOEXEC) {
    std::array<int, 2> sv{-1, -1};
    if (pipe2(sv.data(), flags) < 0) {
      throw errno_as_error("pipe2()");
    }
    return {unique_fd(sv[0]), unique_fd(sv[1])};
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
  unique_fd(const unique_fd &) = delete;
  unique_fd &operator=(const unique_fd &) = delete;
  unique_fd(unique_fd &&other) noexcept : _fd(other.release()) {}
  unique_fd &operator=(unique_fd &&other) noexcept {
    reset(other.release());
    return *this;
  }
};

template <typename C>
  requires std::constructible_from<std::span<const typename C::value_type>, C>
[[nodiscard]] size_t write(int fd, const C &data) {
  constexpr size_t size = sizeof(typename C::value_type);
  return check_rw_error(::write(fd, data.data(), size * data.size()), "write({})", fd) / size;
}
[[nodiscard]] size_t inline write(int fd, std::string_view data) {
  return write(fd, std::span(data));
}

template <typename C, size_t Size = sizeof(typename C::value_type)>
  requires std::constructible_from<std::span<const typename C::value_type>, C>
[[nodiscard]] std::expected<size_t, std::system_error> writeall(int fd, const C &data) {
  return rw_loop([fd, buf = data.data()](size_t remaining, off_t offset) { return ::write(fd, buf + offset / Size, remaining); },
                 Size * data.size(), "write({})", fd)
      .transform([](size_t bytes_written) { return bytes_written / Size; });
}

template <typename T>
[[nodiscard]] std::span<T> read(int fd, std::span<T> buffer) {
  auto n = check_rw_error(::read(fd, buffer.data(), sizeof(T) * buffer.size()), "read({})", fd);
  return buffer.subspan(0, n / sizeof(T));
}
template <typename C>
  requires std::constructible_from<std::span<const typename C::value_type>, C>
[[nodiscard]] std::span<typename C::value_type> read(int fd, C &buffer) {
  return read(fd, std::span(buffer));
}
[[nodiscard]] std::string_view inline read(int fd, std::string &buffer) {
  auto result = read(fd, std::span(buffer));
  return {result.data(), result.size()};
}

template <typename T, size_t Size = sizeof(T)>
[[nodiscard]] std::expected<std::span<T>, std::system_error> readall(int fd, std::span<T> buffer) {
  return rw_loop([fd, buf = buffer.data()](size_t remaining, off_t offset) { return ::read(fd, buf + offset / Size, remaining); },
                 Size * buffer.size(), "read({})", fd)
      .transform([buffer](size_t bytes_read) { return buffer.subspan(0, bytes_read / Size); });
}

[[nodiscard]] std::expected<struct stat, std::system_error> inline stat(int fd) {
  struct stat buf {};
  return zero_or_errno(fstat(fd, &buf), "fstat({})", fd).transform([&] { return std::move(buf); });
}

[[nodiscard]] std::expected<void, std::system_error> inline truncate(int fd, off_t length) {
  return zero_or_errno(ftruncate(fd, length), "ftruncate({}, {})", fd, length);
}

inline std::timespec as_timespec(std::chrono::nanoseconds ns) {
  auto s = std::chrono::duration_cast<std::chrono::seconds>(ns);
  return {.tv_sec = s.count(), .tv_nsec = (ns - s).count()};
}

/// std::chrono::duration_cast, but safely clamped close to ToDur::min/max when the input duration have a larger range
template <typename ToDur, typename Rep, typename Period>
ToDur clamped_cast(const std::chrono::duration<Rep, Period> &t) {
  using namespace std::chrono;
  static_assert(duration<double>(duration<Rep, Period>::max()) >= duration<double>(ToDur::max()),
                "boundaries for ToDur must not overflow in the type of the input duration");

  // makes sure boundaries are small enough that loss of precision does not overflow ToDur.
  // NOTE: this is a no-op for integral types, since their epsilon is defined to be 0
  constexpr auto margin = 1 - std::numeric_limits<Rep>::epsilon();

  constexpr auto min = margin * ceil<duration<Rep, Period>>(ToDur::min());
  constexpr auto max = margin * floor<duration<Rep, Period>>(ToDur::max());
  return std::chrono::duration_cast<ToDur>(std::clamp(t, min, max));
}

/// @returns nfd
inline int poll(std::span<pollfd> fds, std::chrono::nanoseconds timeout = std::chrono::nanoseconds(0), std::optional<sigset_t> sigset = std::nullopt) {
  auto ts = as_timespec(timeout);
  int nfd = ::ppoll(fds.data(), fds.size(), &ts, nullable(sigset));
  if (nfd < 0 && errno != EAGAIN) throw errno_as_error("ppoll(#{})", fds.size());
  return nfd < 0 ? 0 : nfd;  // EAGAIN as if timeout
}
/// @returns revents (0 on timeout/EAGAIN)
inline int poll(int fd, short events, std::chrono::nanoseconds timeout = std::chrono::nanoseconds{0}, std::optional<sigset_t> sigset = std::nullopt) {
  pollfd fds{.fd = fd, .events = events, .revents = 0};
  int nfd = poll(std::span{&fds, 1}, timeout, sigset);
  return nfd == 1 ? fds.revents : 0;
}

/// A named file descriptor that is closed and removed upon destruction.
class tmpfd {
  unique_fd _fd;
  std::filesystem::path _path;

 public:
  explicit tmpfd(const std::string &prefix = "/tmp/jl_tmpfile_", const std::string &suffix = "")
      : tmpfd(std::format("{}XXXXXX{}", prefix, suffix), static_cast<int>(suffix.length())) {}

  [[nodiscard]] auto *operator->(this auto &self) noexcept { return &self._fd; }

  [[nodiscard]] const std::filesystem::path &path() const noexcept { return _path; }
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
  tmpfd(const tmpfd &) = delete;
  tmpfd &operator=(const tmpfd &) = delete;
  tmpfd(tmpfd &&other) noexcept : _fd(std::move(other._fd)),
                                  _path(std::exchange(other._path, {})) {}
  tmpfd &operator=(tmpfd &&other) noexcept {
    _fd = std::move(other._fd);
    std::swap(_path, other._path);  // delegate unlink of our old _path to the other
    return *this;
  }

 private:
  tmpfd(std::string path, int suffixlen)
      : _fd(mkstemps(path.data(), suffixlen)), _path(std::move(path)) {}
};

[[nodiscard]] inline std::string uri_host(const std::string &host) {
  return host.find(':') == std::string::npos ? host : std::format("[{}]", host);
}

/// An owned addrinfo wrapper that also remembers the hostname you looked up.
class unique_addr {
  std::string _host;
  std::string _port;

  std::unique_ptr<addrinfo, deleter<freeaddrinfo>> _addr;

 public:
  unique_addr(std::string host, std::string port, int family = 0, addrinfo hints = {}) : _host(std::move(host)), _port(std::move(port)) {
    hints.ai_family = family;
    if (host.empty()) hints.ai_flags |= AI_PASSIVE;

    addrinfo *result = nullptr;
    if (int status = ::getaddrinfo(_host.empty() ? nullptr : _host.c_str(), _port.c_str(), &hints, &result); status != 0) {
      if (status == EAI_SYSTEM) throw errno_as_error("getaddrinfo({})", string());
      throw std::runtime_error(std::format("getaddrinfo({}): {}", string(), gai_strerror(status)));
    }
    _addr.reset(result);
  }

  [[nodiscard]] auto *get(this auto &self) { return self._addr.get(); }
  [[nodiscard]] std::string string() const { return std::format("{}:{}", uri_host(_host), _port); }

  ~unique_addr() = default;
  unique_addr(const unique_addr &) = delete;
  unique_addr &operator=(const unique_addr &) = delete;
  unique_addr(unique_addr &&) noexcept = default;
  unique_addr &operator=(unique_addr &&) noexcept = default;
};

struct type_erased_sockaddr {
  sockaddr_storage _buffer{};
  socklen_t length = sizeof(_buffer);

  static type_erased_sockaddr from(int fd) {
    type_erased_sockaddr addr;
    if (getsockname(fd, addr.get(), &addr.length) != 0) throw errno_as_error("getsockname({})", fd);
    return addr;
  }

  [[nodiscard]] const sockaddr *get() const { return reinterpret_cast<const sockaddr *>(&_buffer); }  // NOLINT(*reinterpret-cast) to type-erased C-struct
  [[nodiscard]] sockaddr *get() { return reinterpret_cast<sockaddr *>(&_buffer); }                    // NOLINT(*reinterpret-cast) to type-erased C-struct
};

/// A converter from the IPv4/6 type-erased sockaddr stuctures
struct host_port {
  std::string host;
  uint16_t port = 0;

  [[nodiscard]] static host_port from(const sockaddr *addr) {
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
  [[nodiscard]] static host_port from(const addrinfo *ai) { return from(ai->ai_addr); }
  [[nodiscard]] static host_port from(const unique_addr &addr) { return from(addr.get()); }
  [[nodiscard]] static host_port from(const type_erased_sockaddr &addr) { return from(addr.get()); }
  [[nodiscard]] static host_port from(int fd) { return from(type_erased_sockaddr::from(fd)); }

  [[nodiscard]] std::string string() const { return std::format("{}:{}", uri_host(host), port); }
  bool operator==(const host_port &) const = default;
};

template <typename T>
[[nodiscard]] std::expected<void, std::system_error> setsockopt(int fd, int level, int option_name, const T &value) {
  return zero_or_errno(::setsockopt(fd, level, option_name, &value, sizeof(value)),
                       "setsockopt({}, {}, {})", fd, level, option_name);
}

[[nodiscard]] std::expected<void, std::system_error> inline linger(int fd, std::chrono::seconds timeout) {
  return setsockopt(fd, SOL_SOCKET, SO_LINGER,
                    ::linger{.l_onoff = 1, .l_linger = static_cast<int>(timeout.count())});
}

/// An owned socket descriptor that simplifies common network usage.
class unique_socket : public unique_fd {
 public:
  explicit unique_socket(int fd) : unique_fd(fd, "unique_socket(-1)") {}

  [[nodiscard]] static std::pair<unique_socket, unique_socket> pipes(int domain = AF_UNIX, int type = SOCK_STREAM) {
    std::array<int, 2> sv{-1, -1};
    if (socketpair(domain, type, 0, sv.data()) < 0) {
      throw errno_as_error("socketpair()");
    }
    return {unique_socket(sv[0]), unique_socket(sv[1])};
  }

  [[nodiscard]] static unique_socket bound(
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
    throw errno_as_error("socket/bind({})", source.string());
  }
  [[nodiscard]] static unique_socket udp(
      const unique_addr &source = {"::", "0"},
      std::optional<int> domain = {},
      std::optional<int> protocol = IPPROTO_UDP,
      const std::function<void(unique_socket &)> &before_bind = [](auto &) {}) {
    return bound(source, domain, SOCK_DGRAM, protocol, before_bind);
  }
  [[nodiscard]] static unique_socket tcp(
      const unique_addr &source = {"::", "0"},
      std::optional<int> domain = {},
      std::optional<int> protocol = IPPROTO_TCP,
      const std::function<void(unique_socket &)> &before_bind = [](auto &) {}) {
    return bound(source, domain, SOCK_STREAM, protocol, [&](auto &fd) {
      jl::unwrap(setsockopt(*fd, SOL_SOCKET, SO_REUSEADDR, 1));
      before_bind(fd);
    });
  }

  std::vector<std::system_error> linger(std::chrono::seconds timeout) && {
    std::vector<std::system_error> errors;
    if (auto status = jl::linger(fd(), timeout); !status) errors.push_back(status.error());
    if (int fd = release(); close(fd) != 0) errors.push_back(errno_as_error("close({})", fd));
    return errors;
  }
  std::vector<std::system_error> terminate() && {
    return std::move(*this).linger(std::chrono::seconds(0));
  }
};

template <typename C>
  requires std::constructible_from<std::span<const typename C::value_type>, C>
[[nodiscard]] size_t send(int fd, const C &data, int flags = 0) {
  constexpr size_t size = sizeof(typename C::value_type);
  return check_rw_error(::send(fd, data.data(), size * data.size(), flags), "send({})", fd) / size;
}
[[nodiscard]] size_t inline send(int fd, std::string_view data, int flags = 0) {
  return send(fd, std::span(data), flags);
}

template <typename T>
[[nodiscard]] std::span<T> recv(int fd, std::span<T> buffer, int flags = 0) {
  auto n = check_rw_error(::recv(fd, buffer.data(), sizeof(T) * buffer.size(), flags), "recv({})", fd);
  return buffer.subspan(0, n / sizeof(T));
}
template <typename C>
  requires std::constructible_from<std::span<const typename C::value_type>, C>
[[nodiscard]] std::span<typename C::value_type> recv(int fd, C &data, int flags = 0) {
  return recv(fd, std::span(data), flags);
}
[[nodiscard]] std::string_view inline recv(int fd, std::string &buffer, int flags = 0) {
  auto result = recv(fd, std::span(buffer), flags);
  return {result.data(), result.size()};
}
void inline bind(int fd, const unique_addr &source = unique_addr("", "0")) {
  for (const auto *p = source.get(); p != nullptr; p = p->ai_next) {
    if (::bind(fd, p->ai_addr, p->ai_addrlen) == 0) return;
  }
  throw errno_as_error("bind({})", source.string());
}

void inline connect(int fd, const type_erased_sockaddr &addr) {
  if (::connect(fd, addr.get(), addr.length) != 0) {
    throw errno_as_error("connect({})", host_port::from(addr.get()).string());
  }
}

void inline connect(int fd, const unique_addr &source) {
  for (const auto *p = source.get(); p != nullptr; p = p->ai_next) {
    if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) return;
  }
  throw errno_as_error("connect({})", source.string());
}

void inline listen(int fd, int backlog) {
  check_rw_error(::listen(fd, backlog), "listen({})", fd);
}
[[nodiscard]] inline std::optional<std::pair<unique_socket, host_port>> accept(int fd, int flags = 0) {
  type_erased_sockaddr addr;
  auto client = check_rw_error(::accept4(fd, addr.get(), &addr.length, flags), "accept({})", fd);
  if (client < 0) return std::nullopt;
  return {{unique_socket(client), host_port::from(addr.get())}};
}

/// An abstraction for managing the POSIX "span" structures required by
/// multi-message system calls like recvmmsg/sendmmsg.
template <typename T = char>
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
  [[nodiscard]] size_t sendmmsg(off_t off = 0, std::optional<size_t> count = std::nullopt, int flags = MSG_WAITFORONE) {
    return check_rw_error(::sendmmsg(*_fd, &_msgs[off], count.value_or(_msgs.size() - off), flags), "sendmmsg");
  }

  /// Receives message into buffers off through off + count. Returned spans are
  /// valid until further operations on those same message slots.
  [[nodiscard]] std::span<std::span<T>> recvmmsg(off_t off = 0, std::optional<size_t> count = std::nullopt, int flags = MSG_WAITFORONE) {
    int msgs = check_rw_error(::recvmmsg(*_fd, &_msgs[off], count.value_or(_msgs.size() - off), flags, nullptr), "recvmmsg");
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

  template <std::ranges::sized_range R>
    requires std::same_as<typename R::value_type, std::span<T>>
  void reset(const R &buffers) {
    _msgs.resize(std::ranges::size(buffers));
    _iovecs.resize(std::ranges::size(buffers));
    for (size_t i = 0; const auto &buffer : buffers) {
      _iovecs[i].iov_base = buffer.data();
      _iovecs[i].iov_len = sizeof(T) * buffer.size();
      _msgs[i].msg_hdr.msg_iov = &_iovecs[i];
      _msgs[i++].msg_hdr.msg_iovlen = 1;
    }
  }

  ~mmsg_socket() = default;
  mmsg_socket(const mmsg_socket &) = delete;
  mmsg_socket &operator=(const mmsg_socket &) = delete;
  mmsg_socket(mmsg_socket &&) noexcept = default;
  mmsg_socket &operator=(mmsg_socket &&) noexcept = default;
};

/// Same as mmsg_socket, but with a self-managed buffer
template <typename T = char>
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
  mmsg_buffer(const mmsg_buffer &) = delete;
  mmsg_buffer &operator=(const mmsg_buffer &) = delete;
  mmsg_buffer(mmsg_buffer &&) noexcept = default;
  mmsg_buffer &operator=(mmsg_buffer &&) noexcept = default;
};

/// An owned and managed memory mapped span.
template <typename T>
  requires std::is_trivially_copyable_v<T>
class unique_mmap {
  std::span<T> _map;

 public:
  /// A common mmap. The count/offset parameters are in counts of T, not bytes.
  /// @throws std::system_error with errno and errmsg if it fails.
  explicit unique_mmap(size_t count, int prot = PROT_NONE, int flags = MAP_SHARED, int fd = -1, off_t offset = 0, const std::string &errmsg = "mmap()")
      : unique_mmap(nullptr, count, prot, flags, fd, offset, errmsg) {
  }

  /// More advanced constructor for e.g. MAP_FIXED when you need the addr
  /// parameter. The size/offset parameters are in counts of T, not bytes.
  /// @throws std::system_error with errno and errmsg if it fails.
  unique_mmap(void *addr, size_t count, int prot = PROT_NONE, int flags = MAP_SHARED, int fd = -1, off_t offset = 0, const std::string &errmsg = "mmap()")
      : _map(unwrap(ok_mmap<T>(mmap(addr, count * sizeof(T), prot, flags, fd, offset * sizeof(T)), "{}", errmsg)), count) {}

  static unique_mmap<T> anon(size_t count, int prot = PROT_NONE, const std::string &name = "unique_mmap", int flags = MAP_ANONYMOUS | MAP_PRIVATE, const std::string &errmsg = "anon mmap") {
    unique_mmap<T> map(count, prot, flags, -1, 0, errmsg);
#ifdef PR_SET_VMA
    std::ignore = prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, &map[0], count * sizeof(T), name.c_str());  // best effort, so okay if it fails silently
#else
    std::ignore = name;
#endif
    return map;
  }

  [[nodiscard]] auto &operator[](this auto &self, size_t idx) noexcept { return self._map[idx]; }

  [[nodiscard]] auto &operator*(this auto &self) noexcept { return self._map; }
  [[nodiscard]] auto *operator->(this auto &self) noexcept { return &self._map; }

  /// The count parameter is in counts of T not bytes.
  /// @throws std::system_error with errno and errmsg if it fails.
  [[nodiscard]] std::expected<void, std::system_error> remap(size_t count, int flags = 0, void *addr = nullptr, const std::string &errmsg = "mremap()") {
    return ok_mmap<T>(mremap(const_cast<std::remove_const_t<T> *>(_map.data()), _map.size() * sizeof(T), count * sizeof(T), flags, addr), "{}", errmsg)
        .transform([count, this](T *p) { _map = {p, count}; });
  }

  void reset(std::span<T> map = {}) noexcept {
    if (auto old = std::exchange(_map, map); !old.empty()) {
      ::munmap(const_cast<std::remove_const_t<T> *>(old.data()), old.size() * sizeof(T));
    }
  }
  std::span<T> release() noexcept {
    return std::exchange(_map, {});
  }
  ~unique_mmap() noexcept { reset(); }
  unique_mmap(const unique_mmap &) = delete;
  unique_mmap &operator=(const unique_mmap &) = delete;
  unique_mmap(unique_mmap &&other) noexcept : _map(other.release()) {}
  unique_mmap &operator=(unique_mmap &&other) noexcept {
    reset(other.release());
    return *this;
  }
};

/// An owned and managed file descriptor and mapping to its contents
template <typename T>
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
  /// @throws std::system_error with errno and errmsg if it fails.
  explicit fd_mmap(unique_fd fd, int prot = PROT_READ, int flags = MAP_SHARED, off_t offset = 0, std::optional<size_t> count = std::nullopt, const std::string &errmsg = "fd_mmap()")  // NOLINT(*-member-init) false positive: https://github.com/llvm/llvm-project/issues/37250
      : fd_mmap(with_count(std::move(fd), count), prot, flags, offset, count, errmsg) {}

  [[nodiscard]] int fd() const noexcept { return _fd.fd(); }

  [[nodiscard]] auto &operator[](this auto &self, size_t idx) noexcept { return self._map[idx]; }

  [[nodiscard]] auto &operator*(this auto &self) noexcept { return self._map; }
  [[nodiscard]] auto *operator->(this auto &self) noexcept { return &self._map; }

  /// The count parameter is in counts of T not bytes. New mapping is relative
  /// to offset from construction.
  /// @throws std::system_error with errno and errmsg if it fails.
  [[nodiscard]] std::expected<void, std::system_error> remap(size_t count, int mremap_flags = 0) {
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
  /// @throws std::system_error with errno and errmsg if it fails.
  [[nodiscard]] std::expected<void, std::system_error> truncate(size_t length, int mremap_flags = 0) {
    return jl::truncate(*_fd, length * sizeof(T))
        .and_then([=, this] { return _mmap.remap(beyond_offset(length), mremap_flags); })
        .transform([=, this] { _map = _offset < static_cast<off_t>(length) ? *_mmap : _mmap->subspan(0, 0); });
  }

 private:
  static std::pair<unique_fd, size_t> with_count(unique_fd fd, std::optional<size_t> specified_count) {
    auto count = specified_count.value_or(jl::unwrap(stat(*fd)).st_size / sizeof(T));
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

/// A set of read/write indexes with operations that is suitable for
/// implementing efficient single-producer-single-consumer data structures.
/// There is one implementation for raw integers that gives a better performance
/// if thread-safety is not needed and one lock-free implementation for atomics.
template <typename T, size_t Capacity, bool is_atomic = !std::unsigned_integral<T>>
/// See https://www.snellman.net/blog/archive/2016-12-13-ring-buffers/ for further details
  requires std::unsigned_integral<T> || (std::unsigned_integral<typename T::value_type> && T::is_always_lock_free)
class RingIndex {
  static_assert(std::has_single_bit(Capacity),
                "Ring capacity must be a power-of-2 for performance, and so it divides the integer overflow evenly");
  static_assert(std::bit_width(Capacity) < static_cast<T>(CHAR_BIT * sizeof(T)),
                "Ring capacity needs the \"sign\" bit to detect if it is full in the presence of overflow");
};
template <typename T, size_t Capacity>
class RingIndex<T, Capacity, false> {
  T _read = 0;
  T _write = 0;

 public:
  [[nodiscard]] T size() const { return _write - _read; }
  [[nodiscard]] std::pair<T, T> write_free(size_t /*max_needed*/ = Capacity) const { return {_write, Capacity - size()}; }
  [[nodiscard]] std::pair<T, T> read_filled(size_t /*max_needed*/ = Capacity) const { return {_read, size()}; }

  void store_write(size_t write) { _write = write; }
  void store_read(size_t read) { _read = read; }
};
template <typename Atomic, size_t Capacity>
class RingIndex<Atomic, Capacity, true> {
  using T = Atomic::value_type;
  alignas(hardware_destructive_interference_size) Atomic _read = 0;
  alignas(hardware_destructive_interference_size) Atomic _write = 0;
  mutable T _producers_read = 0;
  mutable T _consumers_write = 0;

 public:
  [[nodiscard]] T size() const {
    return _write.load(std::memory_order_relaxed) - _read.load(std::memory_order_relaxed);
  }
  [[nodiscard]] std::pair<T, T> write_free(size_t max_needed = Capacity) const {
    assert(max_needed > 0);  // otherwise it happily retries forever when full
    auto write = _write.load(std::memory_order_relaxed);
    if (auto size = free(write); size >= max_needed) return {write, size};

    _producers_read = _read.load(std::memory_order_acquire);
    return {write, free(write)};
  }
  [[nodiscard]] std::pair<T, T> read_filled(size_t max_needed = Capacity) const {
    assert(max_needed > 0);  // otherwise it happily retries forever when empty
    auto read = _read.load(std::memory_order_relaxed);
    if (auto size = filled(read); size >= max_needed) return {read, size};

    _consumers_write = _write.load(std::memory_order_acquire);
    return {read, filled(read)};
  }

  void store_write(size_t write) { _write.store(write, std::memory_order_release); }
  void store_read(size_t read) { _read.store(read, std::memory_order_release); }

 private:
  /// Only safe to call by the producer!
  [[nodiscard]] T free(T write) const { return Capacity - (write - _producers_read); }
  /// Only safe to call by the consumer!
  [[nodiscard]] T filled(T read) const { return _consumers_write - read; }
};

/// A basic ring buffer. Given an atomic Index type, one writer and one reader
/// can safely use this to share data across threads. Even so, it is not
/// thread-safe to use this if there are multiple readers or writers.
template <typename T, size_t Capacity, typename Index = uint32_t>
class Ring {
  std::array<T, Capacity> _buffer;
  RingIndex<Index, Capacity> _fifo;
  size_t _producers_write = 0;
  size_t _consumers_read = 0;

 public:
  bool push(T value) {
    return push_from(value);
  }
  bool push_from(T &value) {
    auto [write, free] = _fifo.write_free(1);
    if (free == 0) return false;

    _buffer[write % Capacity] = std::move(value);
    _fifo.store_write(write + 1);
    return true;
  }

  std::optional<T> pop() {
    auto [read, available] = _fifo.read_filled(1);
    if (available == 0) return std::nullopt;

    _fifo.store_read(read + 1);
    return std::move(_buffer[read % Capacity]);
  }
};

/// A circular (aka. ring) buffer with support for copy-free read/write of
/// contiguous elements anywhere in the buffer, even across the wrap-around
/// threshold. Given an atomic Index type, one writer and one reader can safely
/// use this to share data across threads. Even so, it is not thread-safe to
/// use this if there are multiple readers or writers.
template <typename T, size_t Capacity, typename Index = uint32_t>
  requires std::is_trivially_copyable_v<T>  // because the wrap-around functionality uses mmaps
class CircularBuffer {
  static_assert((sizeof(T) * Capacity) % (4 << 10) == 0,
                "CircularBuffer byte capacity must be page aligned");

  unique_mmap<T> _data;
  RingIndex<Index, Capacity> _fifo;
  size_t _producers_write = 0;
  size_t _consumers_read = 0;

 public:
  explicit CircularBuffer(const std::string &mmap_name = "CircularBuffer")
      : _data(unique_mmap<T>::anon(Capacity * 2, PROT_NONE, mmap_name)) {
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
    unique_fd fd = tmpfd().unlink();
    constexpr off_t len = Capacity * sizeof(T);
    auto status = jl::truncate(*fd, len)
                      .and_then([&] { return ok_mmap(mmap(_data->data(), len, prot, flags, *fd, 0), "mmap(CircularBuffer data)"); })
                      .and_then([&](auto) { return ok_mmap(mmap(_data->data() + Capacity, len, prot, flags, *fd, 0), "mmap(CircularBuffer shadow)"); });
    if (!status) throw status.error();
  }

  /// @returns a span where you can write new data into the buffer where. Its
  /// size is limited to the amount of free space available.
  [[nodiscard]] std::span<T> peek_back(size_t max) noexcept {
    auto [write, available] = _fifo.write_free(max);
    return {&_data[write % Capacity], std::min(max, static_cast<size_t>(available))};
  }

  /// "Give back" the part at the beginning of the span from peek_back() where
  /// you wrote data.
  size_t commit_written(this auto &self, std::span<T> &&written) noexcept {
    self._producers_write += written.size();
    self._fifo.store_write(self._producers_write);
    return written.size();
  }

  /// @returns a span where you can read available data from the buffer. Its
  /// size is limited by the amount of available data.
  [[nodiscard]] auto peek_front(this auto &self, size_t max) noexcept {
    auto [read, available] = self._fifo.read_filled(max);
    return std::span{&self._data[read % Capacity], std::min(max, static_cast<size_t>(available))};
  }

  /// "Give back" the part at the beginning of the span from peek_front() that
  /// you read.
  size_t commit_read(std::span<const T> &&read) noexcept {
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
};

[[nodiscard]] inline std::optional<std::string> optenv(const char *name) noexcept {
  const char *value = std::getenv(name);  // NOLINT(*mt-unsafe)
  if (value == nullptr) return std::nullopt;
  return value;
}

template <numeric T>
[[nodiscard]] inline std::expected<T, std::system_error> env_as(const char *name) {
  return ok_or_else(optenv(name), [name] { return make_system_error({}, "Missing {} environment value", name); })
      .and_then(from_str<T>);
}

template <numeric T>
[[nodiscard]] inline T env_or(const char *name, T fallback) noexcept {
  return env_as<T>(name).value_or(fallback);
}
[[nodiscard]] inline std::string env_or(const char *name, const std::string &fallback) noexcept {
  return optenv(name).value_or(fallback);
}

/// @throws std::runtime_error if there is no environment variable with this name.
[[nodiscard]] inline std::string reqenv(const char *name) {
  const char *value = std::getenv(name);  // NOLINT(*mt-unsafe)
  if (value == nullptr) throw std::runtime_error(std::format("Missing {} environment variable", name));
  return value;
}

/// A tuple of array-like structures acting like a array-like structure of tuples
template <std::ranges::random_access_range... Rs>
class rows {
 public:
  // Inspired by the initial and simplest version from Björn Fahller's talk:
  //     https://youtu.be/XJzs4kC9d-Y?feature=shared&t=549

  using iterator = idx_iter<rows, std::tuple<std::ranges::range_reference_t<Rs>...>>;

  constexpr rows() = default;
  constexpr explicit rows(Rs... columns) noexcept : _columns(std::move(columns)...) {
    [&]<size_t... Js>(std::index_sequence<Js...>) {
      // assert((std::get<Js>(_columns).size() == ...));
    }(std::index_sequence_for<Rs...>{});
  }

  template <size_t j>
  [[nodiscard]] constexpr auto &column() const { return std::get<j>(_columns); }

  // gory implementation details to quack like the most relevant parts of std::span<std::tuple<Ts...>> follows:

  /// @returns std::tuple<maybe const &, ...>
  template <typename Self>
  constexpr auto operator[](this Self &self, size_t i) {
    return [&]<size_t... Js>(std::index_sequence<Js...>) {
      if constexpr (std::is_const_v<std::remove_reference_t<Self>>) {
        return std::tuple<std::ranges::range_const_reference_t<Rs>...>{std::get<Js>(self._columns)[i]...};
      } else {
        return std::tuple<std::ranges::range_reference_t<Rs>...>{std::get<Js>(self._columns)[i]...};
      }
    }(std::index_sequence_for<Rs...>{});
  }
  /// @returns std::tuple<maybe const &, ...> or throws on out of bounds
  constexpr auto at(this auto &self, size_t i) {
    if (i >= self.size()) throw std::out_of_range(std::format("rows::at {} >= {}", i, self.size()));
    return self.operator[](i);
  }

  [[nodiscard]] constexpr std::size_t size() const { return std::get<0>(_columns).size(); }
  [[nodiscard]] constexpr bool empty() const { return std::get<0>(_columns).empty(); }

  [[nodiscard]] iterator begin() { return {this, 0}; }
  [[nodiscard]] iterator end() { return {this, size()}; }

 protected:
  template <size_t j>
  [[nodiscard]] auto &mutable_column() { return std::get<j>(_columns); }

 private:
  std::tuple<Rs...> _columns;
};

template <size_t N, typename... Ts>
using arrays = rows<std::array<Ts, N>...>;

template <std::ranges::random_access_range... Rs>
class resizeable_rows : public rows<Rs...> {  // e.g. std::valarray
 public:
  using rows<Rs...>::rows;

  constexpr void resize(size_t count) {
    [&]<size_t... Js>(std::index_sequence<Js...>) {
      (rows<Rs...>::template mutable_column<Js>().resize(count), ...);
    }(std::index_sequence_for<Rs...>{});
  }

  // implemented via resize, because std::valarray have no clear
  constexpr void clear() { resize(0); }
};

template <std::ranges::random_access_range... Rs>
class dynamic_rows : public resizeable_rows<Rs...> {  // e.g. std::deque
 public:
  using resizeable_rows<Rs...>::resizeable_rows;

  template <typename... Args>
  constexpr std::tuple<std::ranges::range_value_t<Rs> &...> emplace_back(Args &&...args) {
    return [&]<size_t... Js, typename T>(std::index_sequence<Js...>, T &&t) {
      return std::tuple<std::ranges::range_value_t<Rs> &...>{
          rows<Rs...>::template mutable_column<Js>().emplace_back(std::get<Js>(std::forward<T>(t)))...,
      };
    }(std::index_sequence_for<Rs...>{}, std::forward_as_tuple(std::forward<Args>(args)...));
  }
};

/// A tuple of vector-like structures acting like a vector-like structure of tuples
template <std::ranges::random_access_range... Rs>
class reservable_rows : public dynamic_rows<Rs...> {
 public:
  using dynamic_rows<Rs...>::dynamic_rows;

  [[nodiscard]] constexpr std::size_t capacity() const { return rows<Rs...>::template column<0>().capacity(); }
  constexpr void reserve(size_t new_cap) {
    [&]<size_t... Js>(std::index_sequence<Js...>) {
      (rows<Rs...>::template mutable_column<Js>().reserve(new_cap), ...);
    }(std::index_sequence_for<Rs...>{});
  }
};

template <typename... Ts>
using vectors = reservable_rows<std::vector<Ts>...>;

}  // namespace jl
