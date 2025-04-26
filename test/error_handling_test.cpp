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

  TEST_CASE("try_catch") {
    SUBCASE("successful") {
      CHECK(jl::try_catch<std::system_error>([](auto v) { return v; }, true) == true);
    }
    SUBCASE("operation throws") {
      auto error = jl::make_system_error(std::errc::no_link, "");
      auto result = jl::try_catch<std::system_error>([](const auto& v) -> int { throw v; }, error);
      CHECK(!result.has_value());
      CHECK(result.error().code() == error.code());
    }
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

  TEST_CASE("backoff") {
    using namespace std::chrono;
    SUBCASE("fixed interval") {
      jl::backoff interval{.init = 1s, .base = 1};
      CHECK(1s == interval++);
      CHECK(1s == interval++);
    }
    SUBCASE("binary exponential") {
      jl::backoff interval{.init = 1s, .base = 2};
      CHECK(1s == interval++);
      CHECK(2s == interval++);
      CHECK(4s == interval++);
    }
    SUBCASE("power-of-10 exponential") {
      auto interval = jl::backoff::exp_1ms();
      CHECK(1ms == interval++);
      CHECK(10ms == interval++);
      CHECK(100ms == interval++);
    }
  }
  TEST_CASE("retry_until") {
    using namespace std::chrono;

    std::vector<system_clock::time_point> calls;
    calls.reserve(11);

    auto only = [&calls](auto v) {
      calls.emplace_back(system_clock::now());
      return v;
    };

    auto deadline = jl::deadline::after(1ms, {.init = 1us, .base = 2});
    SUBCASE("deadline") {
      CHECK(jl::retry_until(deadline, only, false) == false);
      CHECK(calls.size() > 4);
      CHECK(calls.size() <= 11);

      auto first_delay = calls[1] - calls[0];
      auto last_full_delay = calls[calls.size() - 2] - calls[calls.size() - 3];
      // NOTE: last delay is whatever is left until the deadline
      CHECK(last_full_delay > first_delay);
    }
    SUBCASE("eventual success") {
      auto result = jl::retry_until(deadline, [&calls]() {
        calls.emplace_back(std::chrono::system_clock::now());
        return calls.size() == 3;
      });
      CHECK(result == true);
      CHECK(calls.size() == 3);
    }

    SUBCASE("with optional result") {
      CHECK(jl::retry_until(deadline, only, std::make_optional(42)) == 42);
      CHECK(calls.size() == 1);
    }
    SUBCASE("without optional result") {
      CHECK(jl::retry_until(deadline, only, std::optional<int>(std::nullopt)) == std::nullopt);
      CHECK(calls.size() > 1);
    }

    SUBCASE("with expected result") {
      CHECK(jl::retry_until(deadline, only, std::expected<int, std::system_error>(42)) == 42);
      CHECK(calls.size() == 1);
    }
    SUBCASE("without expected result") {
      std::expected<int, std::errc> error = std::unexpected(std::errc::no_link);
      CHECK(jl::retry_until(deadline, only, error) == error);
      CHECK(calls.size() > 1);
    }
  }
  TEST_CASE("retry_for") {
    using namespace std::chrono;
    CHECK(jl::retry_for(1us, []() { return true; }) == true);
    CHECK(jl::retry_for(1us, [](auto v) { return v; }, false) == false);
  }
}
