#pragma once

#include <curl/curl.h>
#include <poll.h>

#include <future>
#include <unordered_map>

#include "jl.h"

namespace jl::curl::detail {

class curl_error_category : public std::error_category {
 public:
  [[nodiscard]] const char* name() const noexcept override { return "CURLcode"; }
  [[nodiscard]] std::string message(int ev) const override {
    return curl_easy_strerror(static_cast<CURLcode>(ev));
  }
};
class curlm_error_category : public std::error_category {
 public:
  [[nodiscard]] const char* name() const noexcept override { return "CURLMcode"; }
  [[nodiscard]] std::string message(int ev) const override {
    return curl_multi_strerror(static_cast<CURLMcode>(ev));
  }
};
class curlu_error_category : public std::error_category {
 public:
  [[nodiscard]] const char* name() const noexcept override { return "CURLUcode"; }
  [[nodiscard]] std::string message(int ev) const override {
    return curl_url_strerror(static_cast<CURLUcode>(ev));
  }
};

}  // namespace jl::curl::detail

namespace jl::curl {

inline const std::error_category& easy_error_category() noexcept {
  static detail::curl_error_category instance;
  return instance;
}
inline const std::error_category& multi_error_category() noexcept {
  static detail::curlm_error_category instance;
  return instance;
}
inline const std::error_category& url_error_category() noexcept {
  static detail::curlu_error_category instance;
  return instance;
}

template <class... Args>
std::system_error make_easy_error(CURLcode status, std::format_string<Args...> fmt, Args&&... args) noexcept {
  return {{static_cast<int>(status), easy_error_category()}, std::format(fmt, std::forward<Args>(args)...)};
}
template <class... Args>
std::system_error make_multi_error(CURLMcode status, std::format_string<Args...> fmt, Args&&... args) noexcept {
  return {{static_cast<int>(status), multi_error_category()}, std::format(fmt, std::forward<Args>(args)...)};
}
template <class... Args>
std::system_error make_url_error(CURLUcode status, std::format_string<Args...> fmt, Args&&... args) noexcept {
  return {{static_cast<int>(status), url_error_category()}, std::format(fmt, std::forward<Args>(args)...)};
}

/// WARN: in most cases this must outlive the curl handle it is set on
class unique_slist {
  std::unique_ptr<curl_slist, deleter<curl_slist_free_all>> _list = nullptr;

 public:
  curl_slist* operator*() { return _list.get(); }
  void reset(curl_slist* p) { _list.reset(p); }

  template <class Self>
  auto&& add(this Self&& self, const char* str) {
    decltype(_list) next(curl_slist_append(self._list.get(), str));
    if (!next) throw std::runtime_error("curl_slist_append(...) failed");
    std::swap(self._list, next);
    std::ignore = next.release();
    return std::forward<Self>(self);
  }

  [[nodiscard]] std::vector<std::string_view> dump() const {
    std::vector<std::string_view> result;
    for (const auto* p = _list.get(); p != nullptr; p = p->next) {
      result.emplace_back(p->data);
    }
    return result;
  }
};

/// C++ variant of https://curl.se/libcurl/c/CURLOPT_READFUNCTION.html
using reader = std::function<size_t(std::span<std::byte>)>;
inline size_t no_body(std::span<std::byte> /*ignored*/) { return 0; }
inline reader read_from(std::span<const std::byte> body) {
  return [body](std::span<std::byte> buffer) mutable {
    auto copied = std::min(std::ssize(buffer), std::ssize(body));
    std::copy(body.begin(), body.begin() + copied, buffer.begin());
    body = body.subspan(copied);
    return copied;
  };
}
inline reader read_from(std::string_view body) {
  return read_from(std::as_bytes(std::span(body)));
}

/// C++ variant of https://curl.se/libcurl/c/CURLOPT_WRITEFUNCTION.html
using writer = std::function<size_t(std::string_view)>;
static size_t discard_body(std::string_view buffer) { return buffer.size(); }
static writer append_to(std::string& buffer) {
  return [&buffer](std::string_view s) {
    buffer += s;
    return s.size();
  };
}
static writer overwrite(std::string& buffer) {
  buffer.clear();
  return append_to(buffer);
}

/// Wrapper around a CURL* handle.
class easy {
  /// state that the CURL handle points to, so it needs to be stable across moves
  struct stable_state {
    std::array<char, CURL_ERROR_SIZE> error{};
    writer response = discard_body;
    reader body = no_body;
  };
  std::unique_ptr<stable_state> _state;  // should outlive _curl handle
  std::unique_ptr<CURL, deleter<curl_easy_cleanup>> _curl;

 public:
  template <class Self>
  auto&& reset(this Self&& self) {
    curl_easy_reset(self._curl.get());
    self.setopt(CURLOPT_ERRORBUFFER, self._state->error.data());
    self.setopt(CURLOPT_WRITEFUNCTION, writefunction);
    self.setopt(CURLOPT_WRITEDATA, &self._state->response);
    self.setopt(CURLOPT_READFUNCTION, readfunction);
    self.setopt(CURLOPT_READDATA, &self._state->body);
    return std::forward<Self>(self);
  }

  template <class Self>
  auto&& setopt(this Self&& self, CURLoption setopt, const auto& value) {
    if (auto err = curl_easy_setopt(self._curl.get(), setopt, value); err != CURLE_OK) {
      throw self.error(err, "curl_easy_setopt({}) failed", static_cast<int>(setopt));
    }
    return std::forward<Self>(self);
  }

  easy() : _state(std::make_unique<stable_state>()), _curl(curl_easy_init()) {
    if (!_curl) throw std::runtime_error("curl_easy_init() failed");
    reset();
  }

  static easy& clean() {
    thread_local easy instance;
    return instance.reset();
  }

  /// Configure a request. Use e.g. `jl::curl_ok(curl.request(...))`, to run it
  template <class Self>
  auto&& request(this Self&& self, const char* url, writer response, reader body = no_body) {
    self.setopt(CURLOPT_URL, url);
    self._state->response = std::move(response);
    self._state->body = std::move(body);
    return std::forward<Self>(self);
  }

  template <typename T>
  T& info(CURLINFO id, T& result) {
    if (auto err = curl_easy_getinfo(_curl.get(), id, &result); err != CURLE_OK) {
      throw error(err, "curl_easy_getinfo({}) failed", static_cast<int>(id));
    }
    return result;
  }
  unique_slist& info(CURLINFO id, unique_slist& result) {
    result.reset(info<curl_slist*>(id));
    return result;
  }
  template <typename T>
  T info(CURLINFO id) {
    T value;
    info(id, value);
    return value;
  }

  CURL* operator*() { return _curl.get(); }
  template <class... Args>
  std::system_error error(CURLcode err, std::format_string<Args...> fmt, Args&&... args) noexcept {
    return make_easy_error(err, "{}: {}", std::format(fmt, std::forward<Args>(args)...), _state->error.data());
  }

 private:
  static size_t writefunction(char* ptr, size_t size, size_t nmemb, writer* userdata) {
    return (*userdata)(std::string_view(ptr, ptr + size * nmemb));
  }
  static size_t readfunction(std::byte* ptr, size_t size, size_t nmemb, reader* userdata) {
    return (*userdata)(std::span(ptr, ptr + size * nmemb));
  }
};

/// @returns response code from running a blocking preconfigured CURL request
[[nodiscard]] inline std::expected<long, std::system_error> perform(easy& curl) {
  if (auto err = curl_easy_perform(*curl); err != CURLE_OK) {
    return std::unexpected(curl.error(err, "CURL request failed"));
  }
  return curl.info<long>(CURLINFO_RESPONSE_CODE);
}

/// @returns response code from successful blocking preconfigured CURL request or error if it is >= 400
[[nodiscard]] inline std::expected<long, std::system_error> ok(easy& curl) {
  curl.setopt(CURLOPT_FAILONERROR, 1);
  return perform(curl);
}

/// @returns the response body from a successful preconfigured CURL request
[[nodiscard]] inline std::expected<std::string, std::system_error> ok(easy& curl, const char* url, std::string_view body = "", std::string buffer = "") {
  curl.setopt(CURLOPT_POSTFIELDSIZE, body.size());  // to avoid chunked transfer encoding
  return ok(curl.request(url, overwrite(buffer), read_from(body)))
      .transform([&buffer](long /*status*/) { return std::move(buffer); });
}

[[nodiscard]] inline std::expected<std::string, std::system_error> GET(const char* url, easy& curl = easy::clean(), std::string buffer = "") {
  curl.setopt(CURLOPT_HTTPGET, 1);  // NOTE: automatically resets CURLOPT_POST/UPLOAD
  return ok(curl, url, "", std::move(buffer));
}
[[nodiscard]] inline std::expected<std::string, std::system_error> POST(const char* url, std::string_view body, easy& curl = easy::clean(), std::string buffer = "") {
  curl.setopt(CURLOPT_POST, 1);  // NOTE: automatically resets CURLOPT_HTTPGET/UPLOAD
  return ok(curl, url, body, std::move(buffer));
}
[[nodiscard]] inline std::expected<std::string, std::system_error> PUT(const char* url, std::string_view body, easy& curl = easy::clean(), std::string buffer = "") {
  curl.setopt(CURLOPT_UPLOAD, 1);  // NOTE: automatically resets CURLOPT_HTTPGET/POST
  return ok(curl, url, body, std::move(buffer));
}

/// Wrapper around a CURLM* handle, and the CURL* it manages
class curlm {
  std::unique_ptr<CURLM, deleter<curl_multi_cleanup>> _curlm;
  std::unordered_map<CURL*, easy> _curls;  // in order to lookup from e.g. CURLMsg

 public:
  curlm() : _curlm(curl_multi_init()) {
    if (!_curlm) throw std::runtime_error("curl_multi_init() failed");
  }
  ~curlm() {
    // https://curl.se/libcurl/c/curl_multi_cleanup.html suggests the deletion order:
    for (auto& [_, curl] : _curls) {  // Step 1, disassociate
      curl_multi_remove_handle(_curlm.get(), *curl);
    }
    _curls.clear();  // Step 2, cleanup CURL* handles
    _curlm.reset();  // Step 3, cleanup CURLM* handle
  }

  easy& add(easy curl = {}) {
    auto p = *curl;
    if (auto err = curl_multi_add_handle(_curlm.get(), p); err != CURLM_OK) {
      throw make_multi_error(err, "curl_multi_add_handle({}, {}) failed", _curlm.get(), p);
    }
    return _curls.emplace(p, std::move(curl)).first->second;
  }
  easy& at(CURL* ptr) { return _curls.at(ptr); }
  easy release(CURL* ptr) {
    auto iter = _curls.find(ptr);
    if (iter == _curls.end()) throw std::out_of_range("not in this CURLM*");

    if (auto err = curl_multi_remove_handle(_curlm.get(), ptr); err != CURLM_OK) {
      throw make_multi_error(err, "curl_multi_remove_handle({}, {}) failed", _curlm.get(), ptr);
    }

    easy released = std::move(iter->second);
    _curls.erase(iter);
    return released;
  }

  template <class Self>
  auto&& setopt(this Self&& self, CURLMoption setopt, const auto& value) {
    if (auto err = curl_multi_setopt(self._curlm.get(), setopt, value); err != CURLM_OK) {
      throw make_multi_error(err, "curl_multi_setopt({}) failed", static_cast<int>(setopt));
    }
    return std::forward<Self>(self);
  }

  CURLM* operator*() { return _curlm.get(); }

  curlm(const curlm&) = delete;
  curlm& operator=(const curlm&) = delete;
  curlm(curlm&&) = default;
  curlm& operator=(curlm&&) = default;

 private:
};

static constexpr short as_poll_events(int curl_poll) {
  switch (curl_poll) {
    case CURL_POLL_INOUT:
      return POLLIN | POLLOUT;
    case CURL_POLL_IN:
      return POLLIN;
    case CURL_POLL_OUT:
      return POLLOUT;
    case CURL_POLL_REMOVE:
    default:
      return 0;
  }
}

// Wrapper around the curl_multi_socket_... interface
class multi {
  struct stable_state {
    std::vector<pollfd> pollfds;
    curlm multi;
  };
  std::unique_ptr<stable_state> _state;

 public:
  multi() : _state(std::make_unique<stable_state>()) {
    _state->multi.setopt(CURLMOPT_SOCKETDATA, _state.get());
    _state->multi.setopt(CURLMOPT_SOCKETFUNCTION, socket_callback);
  }

  /// Adds this request to the CURLM* handle.
  /// WARN: nothing happens unless action(...) is called to progress
  void send(easy preconfigured_request) {
    _state->multi.add(std::move(preconfigured_request));
  }

  /// poll(...) these for activity to delegate to action(...)
  [[nodiscard]] std::span<pollfd> fds() const { return _state->pollfds; }
  /// upon non-zero revents on a relevant fd, call this with that fd, also call
  /// this periodically with CURL_SOCKET_TIMEOUT to progress.
  /// @returns number of still active easy requests on this CURLM
  int action(curl_socket_t sockfd = CURL_SOCKET_TIMEOUT) {
    int active = 0;
    // NOTE: https://github.com/curl/curl/blob/c8c64c882c8ed1b2d0875504687b2162db2b6387/lib/multi.c#L3194 does not actually use ev_bitmask
    if (auto err = curl_multi_socket_action(*_state->multi, sockfd, 0, &active); err != CURLM_OK) {
      throw make_multi_error(err, "curl_multi_socket_action({}, {}) failed", *_state->multi, sockfd);
    }
    return active;
  }

  /// @returns one of the responses (if any), and the removed handle associated with it
  std::optional<std::pair<CURLcode, easy>> pop_response() {
    int ignore_nmsg;
    const CURLMsg* m = curl_multi_info_read(*_state->multi, &ignore_nmsg);
    if (m == nullptr || m->msg != CURLMSG_DONE) return std::nullopt;

    return std::pair{m->data.result, _state->multi.release(m->easy_handle)};
  };

 private:
  static int socket_callback(CURL* /*easy*/, curl_socket_t s, int what, stable_state* clientp, void* /*socketp*/) {  // NOLINT(*swappable-parameters)
    auto& fds = clientp->pollfds;
    auto fd = std::ranges::find(fds, s, &pollfd::fd);
    if (fd != fds.end()) {
      // NOTE: CURL* handles, and their FDs are often reused, so just disable on CURL_POLL_REMOVE rather than actually removing them
      fd->events = as_poll_events(what);
    } else if (what != CURL_POLL_REMOVE) {
      fds.emplace_back(pollfd{.fd = s, .events = as_poll_events(what), .revents = 0});
    }
    return 0;
  }
};

// Wrapper around the curl_multi_socket_... interface that tracks responses to originating request
class async {
  multi _multi;
  std::unordered_map<CURLM*, std::promise<std::pair<CURLcode, easy>>> _results;

 public:
  /// Adds this request to the CURLM* handle.
  /// WARN: nothing happens unless action(...) is called to progress
  [[nodiscard]] std::future<std::pair<CURLcode, easy>> send(easy preconfigured_request) {
    CURL* ptr = *preconfigured_request;
    _multi.send(std::move(preconfigured_request));
    auto [iter, _] = _results.emplace(ptr, std::promise<std::pair<CURLcode, easy>>());
    return iter->second.get_future();
  }

  /// poll(...) these for activity to delegate to action(...)
  [[nodiscard]] std::span<pollfd> fds() const { return _multi.fds(); }
  /// upon non-zero revents on a relevant fd, call this with that fd, also call
  /// this periodically with CURL_SOCKET_TIMEOUT to progress.
  /// @returns number of still active easy requests on this CURLM
  int action(curl_socket_t sockfd = CURL_SOCKET_TIMEOUT) {
    int active = _multi.action(sockfd);
    for (std::optional<std::pair<CURLcode, easy>> r; (r = _multi.pop_response());) {
      if (auto promise = _results.find(*r->second); promise != _results.end()) {
        promise->second.set_value(std::move(*r));
        _results.erase(promise);
      }
    }
    return active;
  }
};

// An owned and managed CURLU* handle
using unique_url = std::unique_ptr<CURLU, deleter<curl_url_cleanup>>;

/// A wrapper around the CURLU* handle, that is their URL parsing interface
///
/// Everything in the CURL URL library can fail, but usually more in an
/// std::optional fashion (e.g. that part does not exist, albeit with a
/// descriptive error code), so exceptions are not suitable here. Everything
/// could return std::expected<...>, but all the `and_then`s get annoying pretty
/// fast. Turns out we can do one better, and just be an extended version of
/// std::expected<unique_url, std::system_error> that chains those `and_then`s.
///
/// NOTE: I am not really sure whether to be impressed or horrified that this is
/// actually possible, but at least it is cute so I am trying it out.
class url : public std::expected<unique_url, std::system_error> {
 public:
  static url parse(const char* url, int flags = 0) {
    return curl::url().with(CURLUPART_URL, url, flags);
  }

  // NOTE: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=116492 before gcc 14.3
  using std::expected<unique_url, std::system_error>::expected;
  url() : std::expected<unique_url, std::system_error>(curl_url()) {
    if (value() == nullptr) {
      *this = unexpected_system_error(std::errc::not_enough_memory, "curl_url");
    }
  }

  [[nodiscard]] url with(CURLUPart part, const char* value, int flags = 0) && {
    if (!has_value()) return std::unexpected(error());
    if (auto ok = set(part, value, flags); !ok) return std::unexpected(ok.error());
    return std::move(*this);
  }

  [[nodiscard]] std::expected<std::string, std::system_error> str(int flags = 0) const {
    return get(CURLUPART_URL, flags);
  }

  [[nodiscard]] std::expected<uint16_t, std::system_error> port(int flags = 0) const {
    return get(CURLUPART_PORT, flags).and_then([](const auto& s) { return jl::from_str<uint16_t>(s); });
  }

  [[nodiscard]] std::expected<std::string, std::system_error> get(CURLUPart part, int flags = 0) const {
    return and_then([part, flags](const auto& url) -> std::expected<std::string, std::system_error> {
      char* copy = nullptr;
      if (auto err = curl_url_get(url.get(), part, &copy, flags); err != CURLUE_OK) {
        return std::unexpected(make_url_error(err, "curl_url_get"));
      }
      std::unique_ptr<char, deleter<curl_free>> managed(copy);
      return std::string(managed.get());
    });
  }
  [[nodiscard]] std::expected<void, std::system_error> set(CURLUPart part, const char* value, int flags = 0) {
    return and_then([part, value, flags](const auto& url) -> std::expected<void, std::system_error> {
      if (auto err = curl_url_set(url.get(), part, value, flags); err != CURLUE_OK) {
        return std::unexpected(make_url_error(err, "curl_url_set"));
      }
      return {};
    });
  }

  [[nodiscard]] CURLU* ptr() const {
    if (!has_value()) throw std::system_error(error());
    return (*this)->get();
  }
};

}  // namespace jl::curl
