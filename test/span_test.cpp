#include <doctest/doctest.h>
#include <jl.h>

#include <algorithm>

TEST_SUITE("span") {
  std::string data = "01234567";
  std::span eight(data);
  TEST_CASE("subspan") {
    SUBCASE("offset > size") {
      CHECK(jl::subspan(eight, 9, std::dynamic_extent).empty());
      CHECK(jl::subspan(eight, 9, 1).empty());
    }
    SUBCASE("offset + count > size") {
      CHECK(jl::subspan(eight, 0, 9).size() == 8);
    }
  }

  TEST_CASE("chunked") {
    static_assert(std::ranges::random_access_range<jl::chunked<char>>);

    std::stringstream pairs, triplets, tens;
    for (const auto& pair : jl::chunked(eight, 2)) {
      pairs << std::string_view(pair) << " ";
    }
    CHECK(pairs.str() == "01 23 45 67 ");

    std::ranges::for_each(jl::chunked(eight, 3), [&triplets](auto t) { triplets << std::string_view(t) << " "; });
    CHECK(triplets.str() == "012 345 67 ");

    jl::chunked in_tens(eight, 10);
    std::for_each(in_tens.begin(), in_tens.end(), [&tens](auto t) { tens << std::string_view(t) << " "; });
    CHECK(tens.str() == "01234567 ");
  }

  TEST_CASE("as_iovecs") {
    std::vector<std::string> strings{"foo", "barbaz"};
    auto iovecs = jl::as_iovecs(strings);
    auto spans = jl::as_spans<const char>(iovecs);
    REQUIRE(2 == spans.size());
    CHECK("foo" == jl::view_of(spans[0]));
    CHECK("barbaz" == jl::view_of(spans[1]));
  }

  TEST_CASE("copy") {
    std::vector<std::string> strings = {"foobar", "baz"};
    auto iovecs = jl::as_iovecs(strings);
    std::string dest = "0123456789";

    auto fo = jl::copy(strings, std::span(dest).subspan(0, 2));
    CHECK("fo" == jl::view_of(fo));
    CHECK("fo23456789" == dest);

    auto foobar = jl::copy(iovecs, std::span(dest).subspan(0, 6));
    CHECK("foobar" == jl::view_of(foobar));

    auto foobarba = jl::copy(jl::as_spans<const char>(iovecs), std::span(dest).subspan(0, 8));
    CHECK("foobarba" == jl::view_of(foobarba));

    auto whole_input = jl::copy(strings, std::span(dest));
    CHECK("foobarbaz" == jl::view_of(whole_input));
    CHECK("foobarbaz9" == dest);
  }
}
