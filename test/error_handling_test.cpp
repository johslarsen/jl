#include <doctest/doctest.h>
#include <jl.h>

TEST_SUITE("error handling") {
  TEST_CASE("errno_as_error success") {
    errno = 0;
    CHECK(std::system_error(std::make_error_code(std::errc{}), "foo").what() ==
          std::string_view(jl::errno_as_error("foo").what()));
  }

  TEST_CASE("errno_as_error") {
    errno = ETIMEDOUT;
    CHECK(std::system_error(std::make_error_code(std::errc::timed_out), "foo").what() ==
          std::string_view(jl::errno_as_error("foo").what()));
  }

  TEST_CASE("defer") {
    size_t calls = 0;
    {
      bool called_at_end_of_scope = false;
      jl::defer _([&] {
        ++calls;
        CHECK_MESSAGE(called_at_end_of_scope, "defer lambda called before end of scope");
      });
      called_at_end_of_scope = true;
    }
    CHECK(1 == calls);
  }

  TEST_CASE("retry") {
    auto two_eagain = [attempts = 3] mutable {
      errno = EAGAIN;
      return --attempts > 0 ? -1 : 42;
    };
    CHECK(42 == jl::retry(two_eagain, "test", 3));
    CHECK(std::nullopt == jl::retry(two_eagain, "test"));

    auto serious_error_only_on_first_attempt = [attempts = 2] mutable {
      errno = ETIMEDOUT;
      return --attempts > 0 ? -1 : 42;
    };
    CHECK_THROWS_AS(jl::retry(serious_error_only_on_first_attempt, "test", 2), std::system_error);
  }
}
