#include <gtest/gtest.h>
#include <jl.h>

TEST(ErrorHandling, SuccessAsError) {
  errno = 0;
  EXPECT_STREQ(std::system_error(std::make_error_code(std::errc{}), "foo").what(),
               jl::errno_as_error("foo").what());
}

TEST(ErrorHandling, ErrnoAsError) {
  errno = ETIMEDOUT;
  EXPECT_STREQ(std::system_error(std::make_error_code(std::errc::timed_out), "foo").what(),
               jl::errno_as_error("foo").what());
}

TEST(ErrorHandling, Defer) {
  size_t calls = 0;
  {
    bool called_at_end_of_scope = false;
    jl::defer _([&] {
      ++calls;
      EXPECT_TRUE(called_at_end_of_scope) << "defer lambda called before end of scope";
    });
    called_at_end_of_scope = true;
  }
  EXPECT_EQ(1, calls);
}

TEST(ErrorHandling, Retry) {
  auto two_eagain = [attempts = 3] mutable {
    errno = EAGAIN;
    return --attempts > 0 ? -1 : 42;
  };
  EXPECT_EQ(42, jl::retry(two_eagain, "test", 3));
  EXPECT_EQ(std::nullopt, jl::retry(two_eagain, "test"));

  auto serious_error_only_on_first_attempt = [attempts = 2] mutable {
    errno = ETIMEDOUT;
    return --attempts > 0 ? -1 : 42;
  };
  EXPECT_THROW(jl::retry(serious_error_only_on_first_attempt, "test", 2), std::system_error);
}
