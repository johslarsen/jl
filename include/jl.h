#pragma once
#include <algorithm>
#include <bit>
#include <cassert>
#include <charconv>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstring>
#include <expected>
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
concept one_of = (... || std::same_as<std::remove_cvref_t<T>, Us>);

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

/// Utility to run a method at the end of the scope like a defer statement in Go
template <std::invocable F>
  requires std::is_void_v<std::invoke_result_t<F>>
class defer {
  F _f;

 public:
  [[nodiscard]] explicit defer(F f) : _f(std::move(f)) {}
  ~defer() noexcept { _f(); }

  defer(const defer &) = delete;
  defer &operator=(const defer &) = delete;
  defer(defer &&) = delete;
  defer &operator=(defer &&) = delete;
};

class invocable_counter {
  size_t _total_calls = 0;

 public:
  [[nodiscard]] size_t total_calls() const { return _total_calls; }
  auto wrap(auto f) {
    return [f = std::move(f), this](auto &&...args) {
      ++_total_calls;
      return std::invoke(f, std::forward<decltype(args)>(args)...);
    };
  }
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

[[nodiscard]] constexpr std::byte from_xdigit(char c) {
  assert(('0' <= c && c <= '9') || ('a' <= c && c <= 'f') || ('A' <= c && c <= 'F'));
  return c <= '9' ? std::byte(c - '0') : std::byte(10 + std::tolower(c) - 'a');
}

[[nodiscard]] inline std::vector<std::byte> from_xdigits(std::string_view hex) noexcept {
  if (hex.size() >= 2 && std::tolower(hex[1]) == 'x') {
    assert(hex.starts_with("0x") || hex.starts_with("0X") || hex.starts_with("\\x") || hex.starts_with("\\X"));
    hex.remove_prefix(2);
  }

  std::vector<std::byte> bytes;
  bool odd = hex.size() % 2;
  bytes.reserve(hex.size() / 2 + odd);

  if (odd) bytes.emplace_back(from_xdigit(hex[0]));

  for (auto [begin, end] : std::views::chunk(hex.substr(odd), 2)) {
    assert(end - begin == 2);
    bytes.emplace_back(from_xdigit(begin[0]) << 4 | from_xdigit(begin[1]));
  }

  return bytes;
}

[[nodiscard]] inline std::string to_xdigits(std::span<const std::byte> bytes, std::string_view separator = "", std::string_view prefix = "") {
  if (bytes.empty()) return std::string(prefix);

  std::string xdigits;
  xdigits.reserve(prefix.size() + 2 * bytes.size() + (bytes.size() - 1) * separator.size());

  std::format_to(std::back_inserter(xdigits), "{}{:02x}", prefix, static_cast<unsigned>(bytes[0]));
  for (auto b : bytes.subspan(1)) std::format_to(std::back_inserter(xdigits), "{}{:02x}", separator, static_cast<unsigned>(b));

  return xdigits;
}

/// a descriptive version of std::format_to_n_result
template <class OutputIt>
struct format_to_n_error : std::runtime_error {
  OutputIt before;
  std::format_to_n_result<OutputIt> result;

  format_to_n_error(OutputIt before, std::format_to_n_result<OutputIt> result)
      : std::runtime_error("truncated format_to_n"), before(before), result(result) {}
};
/// same as std::format_to_n, but with a descriptive error if it truncated the result
template <class OutputIt, class... Args>
[[nodiscard]] std::expected<OutputIt, format_to_n_error<OutputIt>> format_to_n(OutputIt out, std::iter_difference_t<OutputIt> n, std::format_string<Args...> fmt, Args &&...args) {
  auto result = std::format_to_n(out, n, fmt, std::forward<Args>(args)...);
  if (result.out - out < result.size) return std::unexpected(format_to_n_error(out, result));
  return result.out;
}

/// @returns rest of output after the formatted text have been appended
template <class... Args>
[[nodiscard]] std::expected<std::span<char>, format_to_n_error<std::span<char>::iterator>> format_into(std::span<char> buf, std::format_string<Args...> fmt, Args &&...args) {
  return jl::format_to_n(buf.begin(), buf.size(), fmt, std::forward<Args>(args)...)
      .transform([buf](auto after) { return buf.subspan(after - buf.begin()); });
}

/// @returns rest of output after the formatted text have been appended
template <class... Args>
std::span<char> truncate_into(std::span<char> buf, std::format_string<Args...> fmt, Args &&...args) {
  if (buf.empty()) return buf;  // do not bother calculating the size to format

  auto result = std::format_to_n(buf.begin(), buf.size(), fmt, std::forward<Args...>(args)...);
  return buf.subspan(result.out - buf.begin());
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
    if (eol == std::string::npos) return {.line = s, .eol = s.substr(s.size())};

    auto line = s.substr(0, eol);
    size_t n = s[eol] == '\r' && s.size() > eol + 1 && s[eol + 1] == '\n' ? 2 : 1;
    return {.line = line, .eol = s.substr(eol, n)};
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
    return std::bit_cast<T>(le(std::bit_cast<U>(n)));
  } else {
    return n;
  }
}

/// @returns ceil(x/y)
constexpr auto div_ceil(std::unsigned_integral auto x, std::unsigned_integral auto y) {
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

/// Given a presorted range, insert v into its sorted position
///
/// Optimized for mostly sorted input data, since std::vector-like structures
/// are cheaper to insert into close to the end than close to the beginning.
template <std::ranges::bidirectional_range R, typename T = std::ranges::range_value_t<R>, class Compare = std::less<T>>
T &sorted_append(R &range, T v, Compare comp = Compare()) {
  assert(std::ranges::is_sorted(range));
  auto [iter, end] = std::ranges::find_last_if(range.begin(), range.end(), [&comp, &v](const auto &c) { return comp(c, v); });
  return *range.insert(iter == end ? range.begin() : ++iter, std::move(v));
}
/// Given a presorted range, binary search for v's sorted location and insert it there
template <std::ranges::random_access_range R, typename T = std::ranges::range_value_t<R>, class Compare = std::less<T>>
T &sorted_insert(R &range, T v, Compare comp = Compare()) {
  return *range.insert(std::ranges::lower_bound(range, v, comp), v);
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
  [[nodiscard]] constexpr friend idx_iter operator+(difference_type n, const idx_iter &iter) { return {iter._range, iter._i + n}; }
  [[nodiscard]] constexpr idx_iter operator+(difference_type n) const { return {_range, _i + n}; }
  constexpr idx_iter &operator+=(difference_type n) { return _i += n, *this; }
  constexpr idx_iter &operator-=(difference_type n) { return _i -= n, *this; }

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

/// Wrapper around free(...) like functions to use them as a std::unique_ptr deleter
template <auto F>
struct deleter {
  void operator()(auto *p) noexcept {
    if (p) F(p);
  }
};

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

template <typename Clock = std::chrono::system_clock>
struct realtimer {
  Clock::duration elapsed{};  //< Total time spent between start()s and stop()s.
  // NOTE: negative after start(), but before stop(), idea from and see details in
  // https://youtu.be/ElUM28ECjy8?feature=shared&t=3015

  void start(Clock::time_point now = Clock::now()) {
    assert(elapsed >= typename Clock::duration{});
    elapsed -= now.time_since_epoch();
  }
  void stop(Clock::time_point now = Clock::now()) {
    assert(elapsed < typename Clock::duration{});
    elapsed += now.time_since_epoch();
  }
};
struct usertimer {
  std::clock_t elapsed{};  //< Total time spent between start()s and stop()s.
  // NOTE: negative after start(), but before stop(), idea from and see details in
  // https://youtu.be/ElUM28ECjy8?feature=shared&t=3015

  void start(clock_t now = std::clock()) {
    assert(elapsed >= 0);
    elapsed -= now;
  }
  void stop(clock_t now = std::clock()) {
    assert(elapsed < 0);
    elapsed += now;
  }
};

/// Start the given timer at construction and stop it on destruction
template <typename Timer>
class scoped_timer {
  Timer &_timer;

 public:
  explicit scoped_timer(Timer &timer) : _timer(timer) { timer.start(); }
  ~scoped_timer() { _timer.stop(); }
  scoped_timer(const scoped_timer &) = delete;
  scoped_timer &operator=(const scoped_timer &) = delete;
};

struct elapsed {
  realtimer<std::chrono::system_clock> real;
  usertimer user;

  void start() { real.start(), user.start(); }
  void stop() { real.stop(), user.stop(); }

  [[nodiscard]] auto
  time_rest_of_scope() {
    return std::forward_as_tuple(scoped_timer(real), scoped_timer(user));
  }
};

[[nodiscard]] inline std::string uri_host(const std::string &host) {
  return host.find(':') == std::string::npos ? host : std::format("[{}]", host);
}

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

struct istat {
  size_t n = 0;
  double mean = 0.0;

  double add(double x) {
    double delta = x - mean;

    ++n;
    mean += delta / static_cast<double>(n);
    sum_square_delta_mean += delta * (x - mean);
    return x;
  }
  auto &&add(std::ranges::range auto &&r) {
    for (const auto &x : r) add(x);
    return std::forward<decltype(r)>(r);
  }

  void add(const istat &other) {
    if (other.n == 0) return;
    if (n == 0) {
      *this = other;
      return;
    }

    // for the math behind this see e.g. https://math.stackexchange.com/a/4567292 , https://en.wikipedia.org/wiki/Pooled_variance

    double combined_mean = (static_cast<double>(n) * mean + static_cast<double>(other.n) * other.mean) / static_cast<double>(n + other.n);

    // sum of "variances":
    sum_square_delta_mean += other.sum_square_delta_mean;
    // + "variance" of means:
    sum_square_delta_mean += static_cast<double>(n) * mean * mean + static_cast<double>(other.n) * other.mean * other.mean;
    sum_square_delta_mean -= static_cast<double>(n + other.n) * combined_mean * combined_mean;

    mean = combined_mean;
    n += other.n;
  }

  double variance() { return sum_square_delta_mean / static_cast<double>(n); }
  double stddev() { return std::sqrt(variance()); }

 private:
  double sum_square_delta_mean = 0.0;
};

double mean(std::ranges::sized_range auto &&r) {
  double n = r.size();
  return std::ranges::fold_left(r, 0.0, std::plus{}) / n;
}
double variance(std::ranges::sized_range auto &&r) {
  auto square_delta_mean = [mu = mean(r)](double sum, double x) { return sum + std::pow(x - mu, 2); };
  return std::ranges::fold_left(r, 0.0, square_delta_mean) / static_cast<double>(r.size());
}
double stddev(std::ranges::sized_range auto &&r) {
  return std::sqrt(variance(std::forward<decltype(r)>(r)));
}

template <typename T>
struct peaks {
  T _min = std::numeric_limits<T>::max();
  T _max = std::numeric_limits<T>::min();

  T add(T x) {
    _min = std::min(_min, x);
    _max = std::max(_max, x);
    return x;
  }
  auto &&add(std::ranges::range auto &&r) {
    for (const auto &x : r) add(static_cast<double>(x));
    return std::forward<decltype(r)>(r);
  }

  std::optional<T> min() { return _min < _max ? std::optional(_min) : std::nullopt; }
  std::optional<T> max() { return _min < _max ? std::optional(_max) : std::nullopt; }
};

}  // namespace jl
