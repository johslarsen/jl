#include <doctest/doctest.h>
#include <jl.h>

TEST_SUITE("stat") {
  TEST_CASE("mean and stddev") {
    std::vector<double> forty_two(42, 42.0);
    auto one_to_hundred = std::views::iota(1, 101);

    jl::istat forty_two_stats;
    forty_two_stats.add(forty_two);
    jl::istat one_to_hundred_stat;
    one_to_hundred_stat.add(one_to_hundred);

    CHECK(forty_two_stats.mean == 42.0);
    CHECK(jl::mean(forty_two) == 42.0);

    CHECK(forty_two_stats.stddev() == 0.0);
    CHECK(jl::stddev(forty_two) == 0.0);

    CHECK(one_to_hundred_stat.mean == 50.5);
    CHECK(jl::mean(one_to_hundred) == 50.5);

    double stddev_1_100 = 28.86607004772212;  // `python3 -c 'import numpy as np; print(np.arange(1,101).std())'`
    CHECK(one_to_hundred_stat.stddev() == stddev_1_100);
    CHECK(jl::stddev(one_to_hundred) == stddev_1_100);
  }

  TEST_CASE("combine istats") {
    jl::istat one_by_one, group_by_group;
    for (auto i : std::views::iota(1, 101)) {
      auto upto_i = std::views::iota(1, i);
      one_by_one.add(upto_i);

      jl::istat group;
      group.add(upto_i);
      group_by_group.add(group);
    }
    CHECK(one_by_one.n == group_by_group.n);
    CHECK(group_by_group.mean == doctest::Approx(one_by_one.mean));
    CHECK(group_by_group.stddev() == doctest::Approx(one_by_one.stddev()));
  }
  TEST_CASE("combine istats special cases") {
    jl::istat empty;
    empty.add(std::array{empty, empty});
    CHECK(empty.n == 0);
    CHECK(empty.mean == 0.0);

    jl::istat stat;
    stat.add(42.0);
    stat.add(empty);
    CHECK(stat.n == 1);
    CHECK(stat.mean == 42.0);
    CHECK(stat.stddev() == 0.0);

    empty.add(stat);
    CHECK(stat.n == 1);
    CHECK(stat.mean == 42.0);
    CHECK(stat.stddev() == 0.0);
  }

  TEST_CASE("peaks") {
    auto one_to_hundred = std::views::iota(1, 101);

    jl::peaks<int> one_to_hundred_peaks;
    one_to_hundred_peaks.add(one_to_hundred);

    CHECK(one_to_hundred_peaks.min().value() == 1);
    CHECK(one_to_hundred_peaks.max().value() == 100);
  }
}
