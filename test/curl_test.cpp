#include <doctest/doctest.h>
#include <jl_curl.h>

const std::string url_to_this_file = std::format("file://{}", __FILE__);

TEST_SUITE("curl") {
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

  TEST_CASE("curlm happy path") {
    jl::curl::multi curlm;
    std::string a, b;
    curlm.handle().request(url_to_this_file, jl::curl::overwrite(a));
    curlm.handle().request(url_to_this_file, jl::curl::overwrite(b));
    CHECK(jl::unwrap(curlm.perform()) == 0);
    CHECK(a.size() == std::filesystem::file_size(__FILE__));
    CHECK(b.size() == std::filesystem::file_size(__FILE__));
  }

  TEST_CASE("make_curl_error") {
    auto ok = jl::curl::make_easy_error(CURLE_OK, "foo");
    CHECK("foo: No error" == std::string(ok.what()));
    CHECK(jl::curl::easy_error_category() == ok.code().category());

    CHECK("CURLcode" == std::string(jl::curl::easy_error_category().name()));
    CHECK("Unknown error" == jl::curl::easy_error_category().message(0xdeadbeef));
  }
  TEST_CASE("make_curlm_error") {
    auto ok = jl::curl::make_multi_error(CURLM_OK, "foo");
    CHECK("foo: No error" == std::string(ok.what()));
    CHECK(jl::curl::multi_error_category() == ok.code().category());

    CHECK("CURLMcode" == std::string(jl::curl::multi_error_category().name()));
    CHECK("Unknown error" == jl::curl::multi_error_category().message(0xdeadbeef));
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
