#include <doctest/doctest.h>
#include <jl.h>

TEST_SUITE("time") {
  using namespace std::chrono_literals;
  TEST_CASE("timespec") {
    auto zero = jl::as_timespec(0ns);
    auto one_second = jl::as_timespec(1s);
    auto one_ns = jl::as_timespec(1ns);

    CHECK(0 == zero.tv_sec);
    CHECK(0 == zero.tv_nsec);
    CHECK(1 == one_second.tv_sec);
    CHECK(0 == one_second.tv_nsec);
    CHECK(0 == one_ns.tv_sec);
    CHECK(1 == one_ns.tv_nsec);
  }

  TEST_CASE("clock conversion") {
    using namespace std::chrono;
    auto last_leap = utc_clock::from_sys(sys_days(January / 1 / 2017)) - 1s;

    SUBCASE("utc_clock ticks continuously") {
      CHECK("2016-12-31 23:59:58" == std::format("{}", last_leap - 2s));
      CHECK("2016-12-31 23:59:59" == std::format("{}", last_leap - 1s));
      CHECK("2016-12-31 23:59:60" == std::format("{}", last_leap));
      CHECK("2017-01-01 00:00:00" == std::format("{}", last_leap + 1s));
    }

    SUBCASE("conversion from UTC to TAI is consistent") {
      CHECK("2017-01-01 00:00:35" == std::format("{}", tai_clock::from_utc(last_leap - 1s)));
      CHECK("2017-01-01 00:00:36" == std::format("{}", tai_clock::from_utc(last_leap)));
      CHECK("2017-01-01 00:00:37" == std::format("{}", tai_clock::from_utc(last_leap + 1s)));
    }

    SUBCASE("conversion from TAI to UTC is consistent") {
      auto two_to_leap = tai_clock::from_utc(last_leap - 2s);
      CHECK("2016-12-31 23:59:58" == std::format("{}", tai_clock::to_utc(two_to_leap)));
      CHECK("2016-12-31 23:59:59" == std::format("{}", tai_clock::to_utc(two_to_leap + 1s)));
      CHECK("2016-12-31 23:59:60" == std::format("{}", tai_clock::to_utc(two_to_leap + 2s)));
      CHECK("2017-01-01 00:00:00" == std::format("{}", tai_clock::to_utc(two_to_leap + 3s)));
    }

    SUBCASE("conversion from UTC to system_clock skips the leap") {
      CHECK("2016-12-31 23:59:59" == std::format("{}", utc_clock::to_sys(last_leap - 1s)));
      CHECK("2016-12-31 23:59:59" == std::format("{}", utc_clock::to_sys(last_leap)));
      CHECK("2017-01-01 00:00:00" == std::format("{}", utc_clock::to_sys(last_leap + 1s)));

      CHECK("2016-12-31 23:59:59" == std::format("{}", utc_clock::to_sys(last_leap - 1s)));
      CHECK("2017-01-01 00:00:00" == std::format("{}", utc_clock::to_sys(last_leap - 1s) + 1s));
    }

    SUBCASE("conversion from system_clock to UTC skips the leap") {
      auto two_to_leap = utc_clock::to_sys(last_leap - 2s);
      CHECK("2016-12-31 23:59:58" == std::format("{}", utc_clock::from_sys(two_to_leap)));
      CHECK("2016-12-31 23:59:59" == std::format("{}", utc_clock::from_sys(two_to_leap + 1s)));
      CHECK("2017-01-01 00:00:00" == std::format("{}", utc_clock::from_sys(two_to_leap + 2s)));
    }
  }
}
