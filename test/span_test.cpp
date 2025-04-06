#include <doctest/doctest.h>
#include <jl_posix.h>

#include <algorithm>

TEST_SUITE("span") {
  constexpr std::string data = "01234567";
  constexpr std::span eight(data);
  TEST_CASE("upto") {
    SUBCASE("offset > size") {
      CHECK(jl::upto(eight, 9, std::dynamic_extent).empty());
      CHECK(jl::upto(eight, 9, 1).empty());
    }
    SUBCASE("offset + count > size") {
      CHECK(jl::upto(eight, 0, 9).size() == 8);
    }
  }

  TEST_CASE("checked subspan") {
    auto four = jl::subspan<4, 4>(eight);
    CHECK(jl::subspan<2, 2>(four).size() == 2);

    CHECK_THROWS(std::ignore = jl::subspan<9, 1>(eight));
    // jl::subspan<2,3>(four); // fails static assert
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
    std::ranges::for_each(in_tens, [&tens](auto t) { tens << std::string_view(t) << " "; });
    CHECK(tens.str() == "01234567 ");
  }
}
