#include <doctest/doctest.h>
#include <jl.h>

#include <deque>
#include <valarray>

TEST_SUITE("idx_iter") {
  TEST_CASE("random_access") {
    std::array<char, 4> array{"foo"};
    jl::idx_iter iter{._range = &array, ._i = 0};

    iter += 1;
    iter = iter + 1;
    iter = 1 + iter;
    iter -= 1;
    iter = iter - 1;
    CHECK(iter - iter == 0);
    CHECK(iter[0] == 'o');  // NOTE: +3-2 = 1 above, i.e. array[1]

    CHECK(iter < (iter + 1));
    CHECK((iter + 1) > iter);

    CHECK(iter >= iter);
    CHECK(iter <= iter);
  }
}

TEST_SUITE("columns") {
  TEST_CASE("arrays") {
    jl::arrays<0, int, float> empty;
    CHECK(empty.size() == 0);
    CHECK(empty.empty());
    CHECK_THROWS(empty.at(1));

    jl::arrays<3, int, float> default_initialized;
    CHECK(default_initialized.size() == 3);
    CHECK(!default_initialized.empty());
    CHECK(std::tuple<int, float>{0, 0.0} == default_initialized[1]);
    CHECK(std::tuple<int, float>{0, 0.0} == default_initialized.at(1));

    jl::arrays<3, int, float> list_initialized{
        {0, 1, 2},
        {0.0, 1.1, 2.2},
    };
    CHECK(std::tuple<int, float>{1, 1.1} == list_initialized[1]);
  }

  TEST_CASE("vectors") {
    jl::vectors<int, float> vectors{
        {0, 1, 2, 3},
        {0.0, 1.1, 2.2, 3.3},
    };
    vectors.emplace_back(4, 4.4);

    CHECK(std::vector<int>{0, 1, 2, 3, 4} == vectors.column<0>());
    CHECK(std::tuple<int, float>{1, 1.1} == vectors[1]);
    CHECK(vectors.size() == 5);

    vectors.reserve(16);
    CHECK(vectors.capacity() == 16);

    vectors.clear();
    CHECK(vectors.size() == 0);
  }

  TEST_CASE("different kinds of containers") {
    std::array<int, 3> span_backing{-0, -1, -2};
    jl::rows<std::array<uint32_t, 3>,
             std::vector<float>,
             std::span<int>,
             std::string,
             std::string_view,
             std::deque<int>,
             std::valarray<int>>
        rows{
            {0, 1, 2},
            {0.0, 1.1, 2.2},
            std::span(span_backing),
            "abc",
            "ABC",
            {0, 10, 20},
            {0, 100, 200},
        };
    CHECK(std::tuple<uint32_t, float, int, char, char, int, int>{1, 1.1, -1, 'b', 'B', 10, 100} == rows[1]);
  }

  TEST_CASE("compatible with ranges") {
    static_assert(std::ranges::random_access_range<jl::rows<jl::vectors<int, float>>>);
    jl::vectors<int, float> vectors{
        {0, 1, 2, 3},
        {0.0, 1.1, 2.2, 3.3},
    };
    auto tuples = std::ranges::to<std::vector>(vectors);
    CHECK(std::tuple<int, float>(1, 1.1) == tuples.at(1));
  }
}
