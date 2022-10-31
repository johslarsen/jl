#include <gtest/gtest.h>
#include <jl.h>

TEST(ErrnoAsError, Success) {
  errno = 0;
  EXPECT_STREQ(std::system_error(std::make_error_code(std::errc{}), "foo").what(),
               jl::errno_as_error("foo").what());
}

TEST(ErrnoAsError, TimedOut) {
  errno = ETIMEDOUT;
  EXPECT_STREQ(std::system_error(std::make_error_code(std::errc::timed_out), "foo").what(),
               jl::errno_as_error("foo").what());
}
