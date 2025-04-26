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
#include <random>
#include <ranges>
#include <span>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

/// Johs's <mail@johslarsen.net> Library. Use however you see fit.
namespace jl {

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

/// @returns ceil(x/y)
constexpr auto div_ceil(std::unsigned_integral auto x, std::unsigned_integral auto y) {
  return x / y + (x % y != 0);
}

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

template <typename E, class F, class... Args>
constexpr std::expected<std::invoke_result_t<F, Args...>, E> try_catch(F f, Args&&... args) {
  try {
    return std::invoke(f, std::forward<Args>(args)...);
  } catch (const E& e) {
    return std::unexpected(e);
  }
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

/// The state of a retry interval function that can be configured to back off
struct backoff {
  std::chrono::nanoseconds init;
  uint_fast16_t base;  ///< 1: fixed interval, 2: exponential 2^n backoff, ...

  [[nodiscard]] static backoff exp_1ms() { return {.init = std::chrono::milliseconds(1), .base = 10}; }

  [[nodiscard]] backoff next() const {
    assert(base > 0);
    return {.init = init * base, .base = base};
  }
  [[nodiscard]] std::chrono::nanoseconds operator++(int) {
    return std::exchange(*this, next()).init;
  }
};

/// When an operation should give up and the timeout strategy to use until then
struct deadline {
  std::chrono::system_clock::time_point deadline;
  backoff timeout = backoff::exp_1ms();

  [[nodiscard]] static struct deadline after(
      std::chrono::system_clock::duration total_time,
      backoff timeout = backoff::exp_1ms(),
      decltype(deadline) now = std::chrono::system_clock::now()) {
    return {.deadline = now + total_time, .timeout = timeout};
  }

  [[nodiscard]] std::optional<std::chrono::system_clock::duration> remaining(decltype(deadline) now = std::chrono::system_clock::now()) const {
    if (now > deadline) return std::nullopt;
    return deadline - now;
  }
  template <typename Duration = decltype(deadline)::duration>
  [[nodiscard]] std::optional<Duration> next_delay(decltype(deadline) now = std::chrono::system_clock::now()) {
    return remaining(now).transform([this](auto r) { return std::min(r, timeout++); });
  }
  [[nodiscard]] std::optional<std::chrono::system_clock::time_point> next_attempt(decltype(deadline) now = std::chrono::system_clock::now()) {
    return next_delay(now).transform([now](auto t) { return now + t; });
  }
};

// Retry f(...) until it returns true-ish or it exceeds the given deadline
// @returns the true-ish result or the false-ish error
template <class Monadic, class... Args>
[[nodiscard]] std::invoke_result_t<Monadic, Args...> retry_until(deadline deadline, Monadic f, Args... args) {
  while (true) {
    auto result = std::invoke(f, args...);
    if (result) return result;
    if (auto backoff = deadline.next_delay(); backoff) {
      std::this_thread::sleep_for(*backoff);
    } else {
      return result;
    }
  }
}
template <class Monadic, class... Args>
[[nodiscard]] std::invoke_result_t<Monadic, Args...> retry_for(std::chrono::nanoseconds timeout, Monadic f, Args... args) {
  return retry_until(deadline::after(timeout), std::move(f), std::move(args)...);
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
std::expected<std::invoke_result_t<F>, std::system_error> eagain(F f, std::format_string<Args...> fmt, Args &&...args) {
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
    auto result = jl::eagain<Attempts>([&] { return f(length - offset, offset); }, fmt, std::forward<Args>(args)...);
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

/// NOTE: require explicit ResultType, because some generators (e.g. std::mt19937) use fast uints that can be padded
template <typename ResultType, size_t Extent>
inline void urandom_into(std::span<std::byte, Extent> buffer, auto &&gen) {
  for (size_t i = 0; i < buffer.size(); i += sizeof(ResultType)) {
    ResultType rand = gen();
    std::memcpy(buffer.data() + i, &rand, std::min(sizeof(ResultType), buffer.size() - i));
  }
}
template <size_t Extent>
inline void urandom_into(std::span<std::byte, Extent> buffer) {
  thread_local std::mt19937_64 gen(std::random_device{}());
  urandom_into<uint64_t>(buffer, gen);
}
/// NOTE: require explicit ResultType, because some generators (e.g. std::mt19937) use fast uints that can be padded
template <typename ResultType>
inline std::string urandom(size_t total_bytes, auto &&gen) {
  std::string buffer(total_bytes, 0);
  urandom_into<ResultType>(std::as_writable_bytes(std::span(buffer)), gen);
  return buffer;
}
inline std::string urandom(size_t total_bytes) {
  std::string buffer(total_bytes, 0);
  urandom_into(std::as_writable_bytes(std::span(buffer)));
  return buffer;
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
#ifdef __SIZEOF_INT128__
using int128 = __int128;
using uint128 = unsigned __int128;
template <> struct uint_from_size<16> { using type = uint128; };
#endif
// clang-format on

template <typename T>
  requires std::is_trivially_copyable_v<T>
[[nodiscard]] constexpr T native(std::span<const std::byte, sizeof(T)> bytes) {
  if constexpr (sizeof(T) < 256) {  // to stay within expression nesting limit
    return std::bit_cast<T>([bytes]<size_t... Is>(std::index_sequence<Is...>) {
      return std::array{bytes[Is]...};
    }(std::make_index_sequence<sizeof(T)>{}));
  } else {  // fallback to memcpy, which have limited consteval support
    T obj;
    std::memcpy(&obj, bytes.data(), sizeof(T));
    return obj;
  }
}

/// @returns returns byteswapped n on little-endian architectures
template <std::integral Int>
[[nodiscard]] constexpr Int be(Int n) noexcept {
  static_assert(std::endian::native == std::endian::big || std::endian::native == std::endian::little,
                "jl::be only supported on big/little-endian architectures");
  return std::endian::native == std::endian::little ? std::byteswap(n) : n;
}
template <typename T, typename U = uint_from_size<sizeof(T)>::type>
  requires(!std::integral<T>)
[[nodiscard]] constexpr T be(T n) noexcept {
  return std::endian::native == std::endian::little ? std::bit_cast<T>(be(std::bit_cast<U>(n))) : n;
}
template <typename T>
[[nodiscard]] constexpr T be(std::span<const std::byte, sizeof(T)> bytes) {
  return be(native<T>(bytes));
}

/// @returns returns byteswapped n on big-endian architectures
template <std::integral Int>
[[nodiscard]] constexpr Int le(Int n) noexcept {
  static_assert(std::endian::native == std::endian::big || std::endian::native == std::endian::little,
                "jl::le only supported on big/little-endian architectures");
  return std::endian::native == std::endian::big ? std::byteswap(n) : n;
}
template <typename T, typename U = uint_from_size<sizeof(T)>::type>
  requires(!std::integral<T>)
[[nodiscard]] constexpr T le(T n) noexcept {
  return std::endian::native == std::endian::big ? std::bit_cast<T>(le(std::bit_cast<U>(n))) : n;
}
template <typename T>
[[nodiscard]] constexpr T le(std::span<const std::byte, sizeof(T)> bytes) {
  return le(native<T>(bytes));
}

template <std::integral T, size_t Offset, size_t Count>
[[nodiscard]] constexpr inline T bits(std::integral auto n) {
  static_assert(Offset + Count <= 8 * sizeof(n));
  static_assert(Count <= 8 * sizeof(T));
  static_assert(sizeof(T) <= sizeof(n));
  T at_msb = static_cast<T>((n << Offset) >> (8 * (sizeof(n) - sizeof(T))));
  return at_msb >> (8 * sizeof(T) - Count);
}
template <std::integral T, size_t Offset, size_t Count, size_t Extent, bool Byteswap = false>
[[nodiscard]] constexpr inline T bits(std::span<const std::byte, Extent> bytes) {
  static_assert(Extent != std::dynamic_extent);
  static_assert(Offset + Count <= 8 * Extent);
  static_assert(sizeof(T) <= Extent);

  constexpr size_t end = Offset + Count;
  constexpr size_t nth_byte = div_ceil(end - std::min(8 * sizeof(T), end), 8UZ);
  static_assert(Offset >= 8 * nth_byte, "no byte-aligned T window fits over the bits to extract");

  T n = native<T>(bytes.template subspan<nth_byte, sizeof(T)>());
  return bits<T, Offset - 8 * nth_byte, Count>(Byteswap ? std::byteswap(n) : n);
}
template <std::integral T, size_t Offset, size_t Count, size_t Extent>
[[nodiscard]] constexpr inline T be_bits(std::span<const std::byte, Extent> bytes) {
  static_assert(std::endian::native == std::endian::big || std::endian::native == std::endian::little,
                "jl::be_bits only supported on big/little-endian architectures");
  return bits < T, Offset, Count, Extent, std::endian::native == std::endian::little > (bytes);
}
template <std::integral T, size_t Offset, size_t Count, size_t Extent>
[[nodiscard]] constexpr inline T le_bits(std::span<const std::byte, Extent> bytes) {
  static_assert(std::endian::native == std::endian::big || std::endian::native == std::endian::little,
                "jl::le_bits only supported on big/little-endian architectures");
  return bits < T, Offset, Count, Extent, std::endian::native == std::endian::big > (bytes);
}

[[nodiscard]] constexpr std::byte bitswap(std::byte b) {
  /// https://graphics.stanford.edu/~seander/bithacks.html#ReverseByteWith64Bits
  return static_cast<std::byte>(((static_cast<uint64_t>(b) * 0x80200802UL) & 0x0884422110UL) * 0x0101010101UL >> 32);
}
[[nodiscard]] constexpr auto bitswap(std::integral auto n) {
  auto bytes = std::bit_cast<std::array<std::byte, sizeof(n)>>(n);
  for (auto &b : bytes) b = bitswap(b);
  return std::byteswap(std::bit_cast<decltype(n)>(bytes));
}

/// A generic Cyclic Redundancy Check calculator for byte-aligned CRC lengths
///
/// NOTE: The CRC catalogs specifies reflected input and output separately, but
/// combined here since none of the common schemes have them set differently.
template <std::unsigned_integral T, T Poly, T Init, bool Reflected, T XorOut>
struct crc {
  template <size_t Extent>
  [[nodiscard]] static constexpr T compute(std::span<const std::byte, Extent> bytes) {
    // for more information see https://www.sunshine2k.de/articles/coding/crc/understanding_crc.html
    //
    // CRC bytewise shifts out its MSB and applies a polynomial division of it
    // combined with the input stream. the actual polynomial division is
    // precomputed and applied via a from a static lookup table.
    //
    // a common CRC variant is to apply the calculation to a reflected input
    // and/or return a reflected output. instead of bitswapping each input byte
    // this implementation instead bitswaps the polynomial in the
    // precomputations and reverses the CRC shifts, which should be equivalent.
    static constexpr T msb_shift = 8 * (sizeof(T) - 1);
    static constexpr std::array<T, 256> lut = []() {
      std::array<T, 256> lut{};
      for (size_t dividend = 0; dividend < lut.size(); ++dividend) {
        T crc = Reflected ? dividend : dividend << msb_shift;
        for (size_t bit = 0; bit < 8; ++bit) {
          // "shift out" the least/most significant bit, and apply the
          // polynomial if the bit was set (* ... below):
          if constexpr (Reflected) {
            crc = (crc >> 1) ^ (bitswap(Poly) * (crc & 0x1));
          } else {
            crc = (crc << 1) ^ (Poly * (crc >> (msb_shift + 7)));
          }
        }
        lut[dividend] = crc;
      }
      return lut;
    }();

    T crc = Init;
    for (auto b : bytes) {
      // "shift out" the least/most significant CRC byte,
      // and combine with precomputed division:
      if constexpr (Reflected) {
        crc = (crc >> 8) ^ lut[static_cast<uint8_t>(b) ^ (crc & 0xff)];
      } else {
        crc = (crc << 8) ^ lut[static_cast<uint8_t>(b) ^ (crc >> msb_shift)];
      }
    }

    // NOTE: Reflected above could probably be replaced by RefIn, and bitswap
    // crc before XorOut if RefIn != RefOut, but the catalog have no examples
    // with a check value for any byte-aligned CRCs with RefIn != RefOut
    return crc ^ XorOut;
  }
  [[nodiscard]] static constexpr T compute(std::string_view bytes) {
    return compute(std::as_bytes(std::span(bytes)));
  }
};
struct crc16_ccitt : crc<uint16_t, 0x1021, 0x0000, true, 0x0000> {};
struct crc32c : crc<uint32_t, 0x1edc'6f41, 0xffff'ffff, true, 0xffff'ffff> {};
// for more CRC variants see https://reveng.sourceforge.io/crc-catalogue/

/// @returns same as span.subspan(...), but truncated/empty where it would be ill-formed/UB
template <typename T>
[[nodiscard]] constexpr inline std::span<T> upto(std::span<T> span, size_t offset, size_t count = std::dynamic_extent) {
  if (offset > span.size()) return {};
  if (count == std::dynamic_extent) return span.subspan(offset);
  return span.subspan(offset, std::min(span.size() - offset, count));
}

/// @returns same as span.subspan(...) or an error of span where it would be ill-formed/UB
template <size_t Offset, size_t Count, typename T>
[[nodiscard]] constexpr inline std::expected<std::span<T, Count>, std::invalid_argument> atleast(std::span<T> span) {
  static_assert(Count != std::dynamic_extent);
  if (span.size() < Offset + Count) return std::unexpected(std::invalid_argument(std::format("subspan({}, {}) < {}", Offset, Count, span.size())));
  return span.template subspan<Offset, Count>();
}

/// @returns same as span.subspan<Offset, Count>(), but throws where it would be ill-formed/UB
template <size_t Offset, size_t Count, typename T, size_t Extent = std::dynamic_extent>
[[nodiscard]] constexpr inline std::span<T, Count> subspan(std::span<T, Extent> span) {
  static_assert(Count != std::dynamic_extent);
  if constexpr (Extent == std::dynamic_extent) {
    return unwrap(atleast<Offset, Count>(span));
  } else {
    static_assert(Offset + Count <= Extent);
    return span.template subspan<Offset, Count>();
  }
}

template <bitcastable_to<char> Char>
inline std::string_view view_of(std::span<Char> bytes) noexcept {
  const char *data = reinterpret_cast<const char *>(bytes.data());  // NOLINT(*reinterpret-cast) Char template requirement makes this safe
  return {data, bytes.size()};
}

/// Given a presorted range, find the lower_bound using a linear search from the end
///
/// Optimized for mostly sorted input data, since std::vector-like structures
/// are cheaper to insert into close to the end than close to the beginning.
template <std::ranges::bidirectional_range R, typename T = std::ranges::range_value_t<R>, class Compare = std::less<T>>
std::ranges::borrowed_iterator_t<R> rsearch_lower_bound(R &&range, const T &v, Compare comp = {}) {
  assert(std::ranges::is_sorted(range));
  auto [iter, end] = std::ranges::find_last_if(range.begin(), range.end(), [&comp, &v](const auto &c) { return comp(c, v); });
  return iter == end ? range.begin() : ++iter;
}

/// Insert v only if it is not equal to the value at lower_bound
///
/// @param lower_bound must point to either something == v, if it is exists; something >= v or end of the range
/// @returns iterator to the inserted v or end
template <std::ranges::range R, typename T = std::ranges::range_value_t<R>, class Compare = std::less<T>>
auto insert_unique(R &range, std::ranges::iterator_t<R> lower_bound, T &&v, Compare comp = {}) {
  if (auto end = std::ranges::end(range); lower_bound != end && !comp(v, *lower_bound)) return end;
  return range.insert(lower_bound, std::forward<T>(v));
}

/// Given a presorted range, linear reverse search for v's sorted location and insert it there
template <std::ranges::bidirectional_range R, typename T = std::ranges::range_value_t<R>, class Compare = std::less<T>>
T &sorted_append(R &range, T v, Compare comp = Compare()) {
  return *range.insert(rsearch_lower_bound(range, v, comp), std::move(v));
}
/// Given a presorted range, binary search for v's sorted location and insert it there
template <std::ranges::random_access_range R, typename T = std::ranges::range_value_t<R>, class Compare = std::less<T>>
T &sorted_insert(R &range, T v, Compare comp = Compare()) {
  return *range.insert(std::ranges::lower_bound(range, v, comp), std::move(v));
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

    std::span<T> operator*() const { return upto(_buffer, _i * _n, _n); }
    std::span<T> operator[](difference_type n) const { return upto(_buffer, (_i + n) * _n, _n); }
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
  auto s = std::chrono::floor<std::chrono::seconds>(ns);
  return {.tv_sec = s.count(), .tv_nsec = (ns - s).count()};
}

template <typename Duration>
constexpr std::pair<std::chrono::year_month_day, std::chrono::hh_mm_ss<Duration>> ymdhms(std::chrono::sys_time<Duration> t) {
  auto days = std::chrono::floor<std::chrono::days>(t);
  return {std::chrono::year_month_day(days), std::chrono::hh_mm_ss(t - days)};
};

/// https://en.wikipedia.org/wiki/International_Atomic_Time
constexpr std::chrono::sys_days tai_epoch = []() {
  using namespace std::chrono;
  return sys_days(1958y / January / 1);
}();
constexpr std::chrono::sys_days gps_epoch = []() {
  using namespace std::chrono;
  return sys_days(1980y / January / 6);
}();

/// https://en.wikipedia.org/wiki/Terrestrial_Time
constexpr std::chrono::tai_time terrestrial_time(std::chrono::milliseconds(-32184));
/// https://en.wikipedia.org/wiki/Epoch_(astronomy)#Julian_years_and_J2000
constexpr std::chrono::tai_time j2000_tt = []() {
  using namespace std::chrono;
  return terrestrial_time + (sys_days(2000y / January / 1) - tai_epoch) + 12h;
}();

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
struct elapsed {
  realtimer<std::chrono::system_clock> real;
  usertimer user;

  void start() { real.start(), user.start(); }
  void stop() { real.stop(), user.stop(); }
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
