#pragma once

#include <curl/curl.h>

#include "jl.h"

namespace jl {
}  // namespace jl

namespace jl::detail {

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

}  // namespace jl::detail

namespace jl {

inline const std::error_category& curl_error_category() noexcept {
  static detail::curl_error_category instance;
  return instance;
}
inline const std::error_category& curlm_error_category() noexcept {
  static detail::curlm_error_category instance;
  return instance;
}

template <class... Args>
std::system_error make_curl_error(CURLcode status, std::format_string<Args...> fmt, Args&&... args) noexcept {
  return {{static_cast<int>(status), curl_error_category()}, std::format(fmt, std::forward<Args>(args)...)};
}
template <class... Args>
std::system_error make_curlm_error(CURLMcode status, std::format_string<Args...> fmt, Args&&... args) noexcept {
  return {{static_cast<int>(status), curlm_error_category()}, std::format(fmt, std::forward<Args>(args)...)};
}

/// Wrapper around a CURL* handle.
class curl {
 public:
  curl() : _curl(curl_easy_init()) {
    if (!_curl) throw std::runtime_error("curl_easy_init() failed");
    reinit();
  }

  /// C++ variant of https://curl.se/libcurl/c/CURLOPT_WRITEFUNCTION.html
  using writer = std::function<size_t(std::string_view)>;
  // writer factories: {
  static size_t discard_body(std::string_view /*ignored*/) { return 0; }
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
  // }

  /// C++ variant of https://curl.se/libcurl/c/CURLOPT_READFUNCTION.html
  using reader = std::function<size_t(std::span<std::byte>)>;
  // reader factories: {
  static size_t no_body(std::span<std::byte> /*ignored*/) { return 0; }
  static reader read_from(std::span<const std::byte> body) {
    return [body](std::span<std::byte> buffer) mutable {
      auto copied = std::min(std::ssize(buffer), std::ssize(body));
      std::copy(body.begin(), body.begin() + copied, buffer.begin());
      body = body.subspan(copied);
      return copied;
    };
  }
  static reader read_from(std::string_view body) {
    return read_from(std::as_bytes(std::span(body)));
  }
  // }

  /// Configure a request. Use e.g. `jl::curl_ok(curl.request(...))`, to run it
  curl& request(const std::string& url, writer response, reader body = no_body) {
    setopt(CURLOPT_URL, url.c_str());
    _writer = std::move(response);
    _reader = std::move(body);
    return *this;
  }

  /// WARN: in most cases this must outlive the curl handle it is set on
  class unique_slist {
    std::unique_ptr<curl_slist, deleter<curl_slist_free_all>> _list = nullptr;

   public:
    curl_slist* operator*() { return _list.get(); }
    void reset(curl_slist* p) { _list.reset(p); }

    unique_slist& add(const char* str) {
      decltype(_list) next(curl_slist_append(_list.get(), str));
      if (!next) throw std::runtime_error("curl_slist_append(...) failed");
      std::swap(_list, next);
      std::ignore = next.release();
      return *this;
    }
    unique_slist& add(const std::string& str) { return add(str.c_str()); }

    [[nodiscard]] std::vector<std::string_view> dump() const {
      std::vector<std::string_view> result;
      for (const auto* p = _list.get(); p != nullptr; p = p->next) {
        result.emplace_back(p->data);
      }
      return result;
    }
  };

  void setopt(CURLoption setopt, const auto& value) {
    if (auto err = curl_easy_setopt(_curl.get(), setopt, value); err != CURLE_OK) {
      throw error(err, "curl_easy_setopt({}) failed", static_cast<int>(setopt));
    }
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
    return make_curl_error(err, "{}: {}", std::format(fmt, std::forward<Args>(args)...), _error.data());
  }

  void swap(curl& other) noexcept {
    _curl.swap(other._curl);
    _error.swap(other._error);
    _writer.swap(other._writer);
    _reader.swap(other._reader);
    reinit();
    other.reinit();
  }
  curl(curl&& other) noexcept {
    swap(other);
  }
  curl& operator=(curl&& other) noexcept {
    swap(other);
    return *this;
  }
  curl(const curl&) = delete;
  curl& operator=(const curl&) = delete;

 private:
  std::unique_ptr<CURL, deleter<curl_easy_cleanup>> _curl;
  std::array<char, CURL_ERROR_SIZE> _error{};
  writer _writer = discard_body;
  reader _reader = no_body;

  void reinit() noexcept {  // NOLINT(*exception*) none of these CURLOPTs are supposed to be able to fail,
    if (!_curl) return;     // but make sure we bail out early if null because that always fails
    setopt(CURLOPT_ERRORBUFFER, _error.data());
    setopt(CURLOPT_WRITEFUNCTION, writefunction);
    setopt(CURLOPT_WRITEDATA, &_writer);
    setopt(CURLOPT_READFUNCTION, readfunction);
    setopt(CURLOPT_READDATA, &_reader);
  }

  static size_t writefunction(char* ptr, size_t size, size_t nmemb, void* userdata) {
    return (*reinterpret_cast<writer*>(userdata))(std::string_view(ptr, ptr + size * nmemb));
  }
  static size_t readfunction(std::byte* ptr, size_t size, size_t nmemb, void* userdata) {
    return (*reinterpret_cast<reader*>(userdata))(std::span(ptr, ptr + size * nmemb));
  }
};

/// @returns response code from running a blocking preconfigured CURL request
[[nodiscard]] inline std::expected<long, std::system_error> curl_easy(curl& curl) {
  if (auto err = curl_easy_perform(*curl); err != CURLE_OK) {
    return std::unexpected(curl.error(err, "CURL request failed"));
  }
  return curl.info<long>(CURLINFO_RESPONSE_CODE);
}

/// @returns response code from successful blocking preconfigured CURL request or error if it is >= 400
[[nodiscard]] inline std::expected<long, std::system_error> curl_ok(curl& curl) {
  curl.setopt(CURLOPT_FAILONERROR, 1);
  return curl_easy(curl);
}

/// @returns the response body from a successful preconfigured CURL request
[[nodiscard]] inline std::expected<std::string, std::system_error> curl_ok(curl& curl, const std::string& url, std::string_view body = "", std::string buffer = "") {
  curl.setopt(CURLOPT_POSTFIELDSIZE, body.size());  // to avoid chunked transfer encoding
  return curl_ok(curl.request(url, curl::overwrite(buffer), curl::read_from(body)))
      .transform([&buffer](long /*status*/) { return std::move(buffer); });
}

[[nodiscard]] inline std::expected<std::string, std::system_error> http_get(curl& curl, const std::string& url, std::string buffer = "") {
  curl.setopt(CURLOPT_HTTPGET, 1);  // NOTE: automatically resets CURLOPT_POST/UPLOAD
  return curl_ok(curl, url, "", std::move(buffer));
}
[[nodiscard]] inline std::expected<std::string, std::system_error> http_post(curl& curl, const std::string& url, std::string_view body, std::string buffer = "") {
  curl.setopt(CURLOPT_POST, 1);  // NOTE: automatically resets CURLOPT_HTTPGET/UPLOAD
  return curl_ok(curl, url, body, std::move(buffer));
}
[[nodiscard]] inline std::expected<std::string, std::system_error> http_put(curl& curl, const std::string& url, std::string_view body, std::string buffer = "") {
  curl.setopt(CURLOPT_UPLOAD, 1);  // NOTE: automatically resets CURLOPT_HTTPGET/POST
  return curl_ok(curl, url, body, std::move(buffer));
}

/// Wrapper around a CURLM* handle.
class curlm {
  std::unique_ptr<CURLM, deleter<curl_multi_cleanup>> _curlm;
  std::unordered_map<CURL*, curl> _curls;  // in order to lookup from e.g. CURLMsg

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

  curl& handle(curl handle = {}) {
    auto p = *handle;
    if (auto ok = curl_multi_add_handle(_curlm.get(), p); ok != CURLM_OK) {
      throw make_curlm_error(ok, "curl_multi_add_handle({}, {}) failed", _curlm.get(), p);
    }
    return _curls.emplace(p, std::move(handle)).first->second;
  }

  std::expected<int, std::system_error> perform() {
    int active = 0;
    if (auto ok = curl_multi_perform(_curlm.get(), &active); ok != CURLM_OK) {
      return std::unexpected(make_curlm_error(ok, "curl_multi_perform({}) failed", _curlm.get()));
    }
    return active;
  }

  curlm(const curlm&) = delete;
  curlm& operator=(const curlm&) = delete;
  curlm(curlm&&) = default;
  curlm& operator=(curlm&&) = default;
};

}  // namespace jl
