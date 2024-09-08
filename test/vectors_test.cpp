#include <doctest/doctest.h>
#include <jl.h>

TEST_SUITE("vectors") {
  TEST_CASE("basic") {
    jl::vectors<int, float> vectors{
        {0, 1, 2, 3},
        {0.0, 1.1, 2.2, 3.3},
    };
    vectors.emplace_back(4, 4.4);

    CHECK(std::vector<int>{0, 1, 2, 3, 4} == std::get<0>(vectors));
    CHECK(std::tuple<int, float>{1, 1.1} == vectors[1]);
    CHECK(vectors.size() == 5);

    vectors.reserve(16);
    CHECK(vectors.capacity() == 16);

    vectors.clear();
    CHECK(vectors.size() == 0);
  }
}
