#include <doctest/doctest.h>
#include <jl.h>

TEST_SUITE("rw_loop") {
  TEST_CASE("NominalRepeat") {
    std::vector<off_t> offsets;
    auto process_upto_10 = [&offsets](size_t remaining, off_t off) {
      offsets.push_back(off);
      return remaining > 10 ? 10 : remaining;
    };
    CHECK(0 == jl::rw_loop(process_upto_10, 0, ""));
    CHECK_MESSAGE((std::vector<off_t>{}) == offsets, "never called when there was no length to process");

    offsets.clear();
    CHECK(10 == jl::rw_loop(process_upto_10, 10, ""));
    CHECK_MESSAGE((std::vector<off_t>{0}) == offsets, "called once");

    offsets.clear();
    CHECK(25 == jl::rw_loop(process_upto_10, 25, ""));
    CHECK_MESSAGE((std::vector<off_t>{0, 10, 20}) == offsets, "called repeatedly");
  }

  TEST_CASE("BreakOnEOF") {
    auto eof_at_25 = [available = 25UL](size_t remaining, off_t) mutable {
      size_t batch = remaining > 10 ? 10 : remaining;
      if (available < remaining) batch = available;

      available -= batch;
      return batch;
    };
    CHECK(25 == jl::rw_loop(eof_at_25, 30, ""));
  }

  TEST_CASE("NonRetryableErrors") {
    auto serious_error_only_on_first_attempt = [attempts = 2](size_t, off_t) mutable {
      errno = ETIMEDOUT;
      return --attempts > 0 ? -1 : 42;
    };
    CHECK_THROWS_AS(jl::rw_loop(serious_error_only_on_first_attempt, 100, ""), std::system_error);
  }

  TEST_CASE("RetryAbleErrors") {
    auto five_eagain = [attempts = 5](size_t, off_t) mutable {
      errno = EAGAIN;
      return --attempts > 0 ? -1 : 42;
    };
    CHECK(84 == jl::rw_loop(five_eagain, 84, ""));
    CHECK_THROWS_AS(jl::rw_loop(five_eagain, 84, "", 4), std::system_error);
  }
}
