#include <doctest/doctest.h>
#include <jl.h>

TEST_SUITE("time") {
  using namespace std::chrono_literals;
  TEST_CASE("timespec") {
    auto zero = jl::as_timespec(0ns);
    auto one_second = jl::as_timespec(1s);
    auto one_ns = jl::as_timespec(1ns);
    auto minus_one_ns = jl::as_timespec(-1ns);

    CHECK(0 == zero.tv_sec);
    CHECK(0 == zero.tv_nsec);
    CHECK(1 == one_second.tv_sec);
    CHECK(0 == one_second.tv_nsec);
    CHECK(0 == one_ns.tv_sec);
    CHECK(1 == one_ns.tv_nsec);
    CHECK(-1 == minus_one_ns.tv_sec);
    CHECK(999'999'999 == minus_one_ns.tv_nsec);
  }

  TEST_CASE("ymdhms") {
    using namespace std::chrono;
    SUBCASE("system_clock") {
      auto [ymd, hms] = jl::ymdhms(sys_time<nanoseconds>(seconds(1UL << 31) + 42ns));
      CHECK(ymd == 2038y / January / 19d);
      CHECK(hms.hours() == 3h);
      CHECK(hms.minutes() == 14min);
      CHECK(hms.seconds() == 8s);
      CHECK(hms.subseconds() == 42ns);
    }
    SUBCASE("j2000_tt in various clocks") {
      auto [ymd, hms] = jl::ymdhms(jl::tai_epoch + jl::j2000_tt.time_since_epoch());
      CHECK("2000-01-01 11:59:27.816" == std::format("{}", jl::j2000_tt));
      CHECK(ymd == 2000y / January / 1d);
      CHECK(hms.hours() == 11h);
      CHECK(hms.minutes() == 59min);
      CHECK(hms.seconds() == 27s);
      CHECK(hms.subseconds() == 816ms);

      auto j2000_tt_utc = std::chrono::clock_cast<std::chrono::system_clock>(jl::j2000_tt);
      auto [uymd, uhms] = jl::ymdhms(j2000_tt_utc);
      CHECK("2000-01-01 11:58:55.816" == std::format("{}", j2000_tt_utc));
      CHECK(uymd == 2000y / January / 1d);
      CHECK(uhms.hours() == 11h);
      CHECK(uhms.minutes() == 58min);
      CHECK(uhms.seconds() == 55s);
      CHECK(uhms.subseconds() == 816ms);

      auto j2000_tt_gps = std::chrono::clock_cast<std::chrono::gps_clock>(jl::j2000_tt);
      auto [gymd, ghms] = jl::ymdhms(jl::gps_epoch + j2000_tt_gps.time_since_epoch());
      CHECK("2000-01-01 11:59:08.816" == std::format("{}", j2000_tt_gps));
      CHECK(gymd == 2000y / January / 1d);
      CHECK(ghms.hours() == 11h);
      CHECK(ghms.minutes() == 59min);
      CHECK(ghms.seconds() == 8s);
      CHECK(ghms.subseconds() == 816ms);
    }
  }

  TEST_CASE_TEMPLATE("clamped_cast", T, std::chrono::seconds, std::chrono::days, std::chrono::nanoseconds, std::chrono::duration<double>) {
    using namespace std::chrono;
    CHECK(jl::clamped_cast<nanoseconds>(T::min()) <= nanoseconds::min() + duration_cast<nanoseconds>(T(1)));
    CHECK(jl::clamped_cast<nanoseconds>(T::max()) >= nanoseconds::max() - duration_cast<nanoseconds>(T(1)));
  }
  TEST_CASE("clamped_cast only compiles if it is safe to use") {
    // jl::clamped_cast<std::chrono::nanoseconds>(std::chrono::duration<int8_t>::min());
    // jl::clamped_cast<std::chrono::nanoseconds>(std::chrono::duration<int64_t, std::pico>::min());
  }

  TEST_CASE("realtime") {
    jl::realtimer timer;

    auto y2k38 = std::chrono::sys_seconds(std::chrono::seconds(1L << 31));

    timer.start(y2k38);
    timer.stop(y2k38 + std::chrono::seconds(42));
    CHECK(timer.elapsed == std::chrono::seconds(42));
  }
  TEST_CASE("usertime") {
    jl::usertimer timer;

    timer.start(42);
    timer.stop(52);
    CHECK(timer.elapsed == 10);
  }

  TEST_CASE("scoped_timer") {
    jl::realtimer timer;
    {
      jl::scoped_timer _(timer);
    }
    CHECK(timer.elapsed > std::chrono::nanoseconds(0));
  }

  TEST_CASE("elapsed") {
    jl::elapsed timer;
    timer.start();
    timer.stop();

    auto real = timer.real.elapsed;
    auto user = timer.user.elapsed;
    CHECK(real > std::chrono::nanoseconds(0));
    CHECK(user > 0);

    {
      auto _ = timer.time_rest_of_scope();
    }
    CHECK(timer.real.elapsed > real);
    CHECK(timer.user.elapsed > user);
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
