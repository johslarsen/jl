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
}
