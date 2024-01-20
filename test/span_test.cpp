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

    std::ranges::for_each(jl::chunked(eight, 3), [&triplets](auto t) {triplets << std::string_view(t) << " ";});
    CHECK(triplets.str() == "012 345 67 ");

    jl::chunked in_tens(eight, 10);
    //std::ranges::end(in_tens);
    std::for_each(in_tens.begin(), in_tens.end(), [&tens](auto t) { tens << std::string_view(t) << " "; });
    CHECK(tens.str() == "01234567 ");
  }
}
