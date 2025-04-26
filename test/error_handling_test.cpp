#include <doctest/doctest.h>
#include <jl.h>

const std::string_view what(auto error) {
  return error.what();
}

const std::string_view error_what(auto expected) {
  if (expected.has_value()) return "";
  return what(expected.error());
}

TEST_SUITE("error handling") {
  TEST_CASE("errno_as_error") {
    SUBCASE("success") {
      errno = 0;
      CHECK_EQ(what(jl::make_system_error(std::errc{}, "foo")),
               what(jl::errno_as_error("foo")));
    }
    SUBCASE("error") {
      errno = ETIMEDOUT;
      CHECK_EQ(what(jl::make_system_error(std::errc::timed_out, "foo")),
               what(jl::errno_as_error("foo")));
    }
  }

  TEST_CASE("unexpected_errno") {
    errno = ETIMEDOUT;
    CHECK_EQ(what(jl::make_system_error(std::errc::timed_out, "foo")),
             what(jl::unexpected_errno("foo").error()));
  }

  TEST_CASE("unwrap") {
    auto timed_out = jl::make_system_error(std::errc::timed_out, "foo");

    SUBCASE("regular type") {
      CHECK(42 == jl::unwrap(std::expected<int, std::system_error>(42)));
      CHECK_THROWS_WITH_AS(std::ignore = jl::unwrap(std::expected<int, std::system_error>(std::unexpected(timed_out))),
                           timed_out.what(), decltype(timed_out));
    }

    SUBCASE("move-only type") {
      CHECK(nullptr == jl::unwrap(std::expected<std::unique_ptr<int>, std::system_error>(nullptr)));
      CHECK_THROWS_WITH_AS(std::ignore = jl::unwrap(std::expected<std::unique_ptr<int>, std::system_error>(std::unexpected(timed_out))),
                           timed_out.what(), decltype(timed_out));
    }

    SUBCASE("nothing expected, but an error could occur") {
      jl::unwrap(std::expected<void, std::system_error>());
      CHECK_THROWS_WITH_AS(jl::unwrap(std::expected<void, std::system_error>(std::unexpected(timed_out))),
                           timed_out.what(), decltype(timed_out));
    }
  }

  TEST_CASE("ok_or") {
    auto timed_out = jl::make_system_error(std::errc::timed_out, "foo");
    CHECK(42 == jl::ok_or(std::optional(42), timed_out).value());
    CHECK(what(timed_out) == error_what(jl::ok_or<int>(std::nullopt, timed_out)));
  }

  TEST_CASE("ok_or_else") {
    auto make_timed_out = [] { return jl::make_system_error(std::errc::timed_out, "foo"); };
    CHECK(42 == jl::ok_or_else(std::optional(42), make_timed_out).value());
    CHECK(what(make_timed_out()) == error_what(jl::ok_or_else<int>(std::nullopt, make_timed_out)));
  }

  TEST_CASE("ok_or_errno") {
    CHECK(42 == jl::unwrap(jl::ok_or_errno(42, "")));

    errno = EAGAIN;
    CHECK(0 == jl::unwrap(jl::ok_or_errno(-1, "")));

    errno = ETIMEDOUT;
    CHECK_EQ(what(jl::make_system_error(std::errc::timed_out, "foo")),
             error_what(jl::ok_or_errno(-1, "foo")));
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

  TEST_CASE("eagain") {
    auto two_eagain = [attempts = 3] mutable {
      errno = EAGAIN;
      return --attempts > 0 ? -1 : 42;
    };
    CHECK(42 == jl::eagain<3>(two_eagain, "foo"));
    CHECK_EQ(what(jl::make_system_error(std::errc(EAGAIN), "foo")),
             error_what(jl::eagain<2>(two_eagain, "foo")));

    auto serious_error_only_on_first_attempt = [attempts = 2] mutable {
      errno = ETIMEDOUT;
      return --attempts > 0 ? -1 : 42;
    };
    CHECK_EQ(what(jl::make_system_error(std::errc::timed_out, "foo")),
             error_what(jl::eagain<2>(serious_error_only_on_first_attempt, "foo")));
  }
}
