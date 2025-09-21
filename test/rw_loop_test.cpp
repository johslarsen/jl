#include <doctest/doctest.h>
#include <jl.h>

TEST_SUITE("rw_loop") {
  TEST_CASE("nominal repeat") {
    std::vector<off_t> offsets;
    auto process_upto_10 = [&offsets](ssize_t remaining, off_t off) {
      offsets.push_back(off);
      return remaining > 10 ? 10 : remaining;
    };
    CHECK(0 == jl::unwrap(jl::rw_loop(0, process_upto_10)));
    CHECK_MESSAGE((std::vector<off_t>{}) == offsets, "never called when there was no length to process");

    offsets.clear();
    CHECK(10 == jl::unwrap(jl::rw_loop(10, process_upto_10)));
    CHECK_MESSAGE((std::vector<off_t>{0}) == offsets, "called once");

    offsets.clear();
    CHECK(25 == jl::unwrap(jl::rw_loop(25, process_upto_10)));
    CHECK_MESSAGE((std::vector<off_t>{0, 10, 20}) == offsets, "called repeatedly");
  }

  TEST_CASE("break on EOF") {
    auto eof_at_25 = [available = 25L](ssize_t remaining, off_t) mutable {
      ssize_t batch = remaining > 10 ? 10 : remaining;
      if (available < batch) batch = available;
      available -= batch;
      return batch;
    };
    CHECK(25 == jl::unwrap(jl::rw_loop(30, eof_at_25)));
  }

  TEST_CASE("non-retryable errors") {
    auto serious_error_only_on_first_attempt = [attempts = 2](size_t, off_t) mutable {
      errno = ETIMEDOUT;
      return --attempts > 0 ? -1 : 42;
    };
    CHECK(!jl::rw_loop<jl::attempts{2}>(100, serious_error_only_on_first_attempt));
  }

  TEST_CASE("retryable errors") {
    auto five_eagain = [attempts = 5](size_t, off_t) mutable {
      errno = EAGAIN;
      return --attempts > 0 ? -1 : 42;
    };
    CHECK(84 == jl::unwrap(jl::rw_loop<jl::attempts{5}>(84, std::function(five_eagain))));
    CHECK(!jl::rw_loop<jl::attempts{4}>(84, std::function(five_eagain)));
  }
}
