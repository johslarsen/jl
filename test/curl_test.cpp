#include <doctest/doctest.h>
#include <jl_curl.h>

const std::string url_to_this_file = std::format("file://{}", __FILE__);

TEST_SUITE("synchronize easy API") {
  TEST_CASE("file://...") {
    SUBCASE("GET") {
      auto content = jl::unwrap(jl::curl::GET(url_to_this_file));
      CHECK(content.size() == std::filesystem::file_size(__FILE__));
    }
    SUBCASE("PUT") {
      jl::tmpfd tmp;
      auto response = jl::unwrap(jl::curl::PUT(tmp.url(), "foo"));
      CHECK(response == "");

      jl::fd_mmap<char> content(std::move(tmp).unlink());
      CHECK(jl::view_of(*content) == "foo");
    }
  }

  // assumes a local `docker run --rm -p 8080:80 ealen/echo-server:0.9.2`
  TEST_CASE("http echoserver" * doctest::skip()) {
    SUBCASE("GET") {
      auto echo = jl::unwrap(jl::curl::GET("http://localhost:8080/foo"));
      CHECK(doctest::String(echo.c_str()) == doctest::Contains("GET"));
      CHECK(doctest::String(echo.c_str()) == doctest::Contains("/foo"));
    }
    SUBCASE("POST") {
      auto echo = jl::unwrap(jl::curl::POST("http://localhost:8080/foo", "bar"));
      CHECK(doctest::String(echo.c_str()) == doctest::Contains("POST"));
      CHECK(doctest::String(echo.c_str()) == doctest::Contains("bar"));
    }
    SUBCASE("PUT") {
      // CURL have no default Content-Type for PUT, and then ealen/echo-server does not output the body at all
      auto headers = jl::curl::unique_slist().add("Content-Type: application/x-www-form-urlencoded");
      auto curl = jl::curl::easy().setopt(CURLOPT_HTTPHEADER, *headers);

      auto echo = jl::unwrap(jl::curl::PUT("http://localhost:8080/foo", "bar", curl));
      CHECK(doctest::String(echo.c_str()) == doctest::Contains("PUT"));
      CHECK(doctest::String(echo.c_str()) == doctest::Contains("bar"));
    }
  }

  TEST_CASE("make_curl_error") {
    auto ok = jl::curl::make_easy_error(CURLE_OK, "foo");
    CHECK("foo: No error" == std::string(ok.what()));
    CHECK(jl::curl::easy_error_category() == ok.code().category());

    CHECK("CURLcode" == std::string(jl::curl::easy_error_category().name()));
    CHECK("Unknown error" == jl::curl::easy_error_category().message(0xdeadbeef));
  }

  TEST_CASE("GET file:///NOT_FOUND") {
    CHECK(!jl::curl::GET("file:///NOT_FOUND").has_value());
  }

  TEST_CASE("options") {
    jl::curl::easy curl;
    curl.setopt(CURLOPT_TIMEOUT, 1);

    CHECK_THROWS_AS(curl.setopt(CURLOPT_POSTFIELDSIZE, -42L), std::system_error);
  }

  TEST_CASE("info") {
    jl::curl::easy curl;
    CHECK(nullptr == curl.info<char*>(CURLINFO_PRIVATE));

    auto list = curl.info<jl::curl::unique_slist>(CURLINFO_SSL_ENGINES);
    CHECK(*list != nullptr);

    // from https://github.com/curl/curl/blob/master/lib/getinfo.c it seems like
    // this is the only kind of error it returns, and UBSAN whines about that cast:
    // CHECK_THROWS_AS(curl.info<long>(static_cast<CURLINFO>(0xdeadbeef)), std::system_error);
  }

  TEST_CASE("unique_slist") {
    jl::curl::unique_slist headers;
    headers.add("Content-Type: text/plain").add("Connection: keep-alive");

    jl::curl::easy curl;
    curl.setopt(CURLOPT_HTTPHEADER, *headers);
  }
}

TEST_SUITE("asynchronous multi API") {
  TEST_CASE("file://...") {  // which completes on first action() without every registering any poll sockets
    jl::curl::multi curlm;
    std::string a, b;
    auto af = curlm.start(jl::curl::easy().request(url_to_this_file, jl::curl::overwrite(a)));
    auto bf = curlm.start(jl::curl::easy().request(url_to_this_file, jl::curl::overwrite(b)));
    CHECK(curlm.action() == 0);

    CHECK(jl::unwrap(std::move(af)).first == CURLE_OK);
    CHECK(a.size() == std::filesystem::file_size(__FILE__));
    CHECK(jl::unwrap(std::move(bf)).first == CURLE_OK);
    CHECK(b.size() == std::filesystem::file_size(__FILE__));
  }

  // assumes a local `docker run --rm -p 8080:80 ealen/echo-server:0.9.2`
  TEST_CASE("http echoserver" * doctest::skip()) {
    jl::curl::multi curlm;
    std::string a, b;
    auto af = curlm.start(jl::curl::easy().request("http://localhost:8080/foo", jl::curl::overwrite(a)));
    auto bf = curlm.start(jl::curl::easy().request("http://localhost:8080/bar", jl::curl::overwrite(b)));

    while (curlm.action() != 0) {
      if (jl::poll(curlm.fds()) == 0) continue;
      for (const auto& fd : curlm.fds()) {
        if (fd.revents != 0) curlm.action(fd.fd);
      }
    }

    auto [ar, ae] = jl::unwrap(std::move(af));
    CHECK(ar == CURLE_OK);
    CHECK(std::string(ae.info<const char*>(CURLINFO_EFFECTIVE_URL)) == "http://localhost:8080/foo");
    CHECK(a.size() > 0);

    auto [br, be] = jl::unwrap(std::move(bf));
    CHECK(std::string(be.info<const char*>(CURLINFO_EFFECTIVE_URL)) == "http://localhost:8080/bar");
    CHECK(br == CURLE_OK);
    CHECK(b.size() > 0);
  }

  TEST_CASE("make_curlm_error") {
    auto ok = jl::curl::make_multi_error(CURLM_OK, "foo");
    CHECK("foo: No error" == std::string(ok.what()));
    CHECK(jl::curl::multi_error_category() == ok.code().category());

    CHECK("CURLMcode" == std::string(jl::curl::multi_error_category().name()));
    CHECK("Unknown error" == jl::curl::multi_error_category().message(0xdeadbeef));
  }
}
