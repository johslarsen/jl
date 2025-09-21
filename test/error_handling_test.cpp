#include <doctest/doctest.h>
#include <jl.h>

const std::string_view what(auto error) {
  return error.what();
}

const std::string_view error_what(auto expected) {
  if (expected.has_value()) return "";
  return what(expected.error());
}

namespace jl {
using std::error_code;

}  // namespace jl

TEST_SUITE("error handling") {
  TEST_CASE("jl::error remembers") {
    SUBCASE("no what") {
      CHECK(jl::error().msg() == "");
      CHECK(std::string_view(jl::error().what()) == "Success");
    }
    SUBCASE("empty what") {
      jl::error e(std::errc::io_error, "");
      CHECK(e.msg() == "");
      CHECK(std::string_view(e.what()) == ": Input/output error");
    }
    SUBCASE("formatted what") {
      jl::error e(std::make_error_code(std::errc::timed_out), "{}", 42);
      CHECK(e.msg() == "42");
      CHECK(std::string_view(e.what()) == "42: Connection timed out");
    }
  }
  TEST_CASE("jl::error::prefixed") {
    auto e = jl::error(std::errc::io_error, "bar").prefixed("foo ");
    CHECK(e.msg() == "foo bar");
    CHECK(e.code() == std::make_error_code(std::errc::io_error));
    CHECK(std::string_view(e.what()) == "foo bar: Input/output error");
  }

  TEST_CASE("jl::error constructed in transform_error") {
    auto from_errno = std::expected<int, int>(std::unexpected(EIO))
                          .transform_error([](auto ec) { return jl::error(ec, "foo {}", 42); });
    auto from_ec = std::expected<int, std::error_code>(std::unexpected(std::make_error_code(std::errc::io_error)))
                       .transform_error([](auto ec) { return jl::error(ec, "foo {}", 42); });

    std::string expected = std::system_error(jl::as_ec(EIO), "foo 42").what();
    CHECK(error_what(from_errno) == expected);
    CHECK(error_what(from_ec) == expected);
  }

  TEST_CASE("ok_or_join_with tuple") {
    auto truish = []<class T>(T v) -> std::expected<T, std::runtime_error> {
      if (v) return v;
      return std::unexpected(std::runtime_error(std::format("falsish {}", v)));
    };
    auto ok = jl::ok_or_join_with("\n", truish(true), truish(42));
    CHECK(ok.has_value());
    CHECK(*ok == std::tuple{true, 42});

    auto one_error = jl::ok_or_join_with("\n", truish(false));
    CHECK(!one_error.has_value());
    CHECK(std::string_view(one_error.error().what()) == "falsish false");

    auto some_errors = jl::ok_or_join_with("\n", truish(false), truish(true), truish(0));
    CHECK(std::string_view(some_errors.error().what()) == "falsish false\nfalsish 0");
  }

  TEST_CASE("unwrap") {
    jl::error timed_out(ETIMEDOUT, "foo");

    SUBCASE("regular type") {
      CHECK(42 == jl::unwrap(std::expected<int, jl::error>(42)));
      CHECK_THROWS_WITH_AS(std::ignore = jl::unwrap(std::expected<int, jl::error>(std::unexpected(timed_out))),
                           timed_out.what(), decltype(timed_out));
    }

    SUBCASE("move-only type") {
      CHECK(nullptr == jl::unwrap(std::expected<std::unique_ptr<int>, jl::error>(nullptr)));
      CHECK_THROWS_WITH_AS(std::ignore = jl::unwrap(std::expected<std::unique_ptr<int>, jl::error>(std::unexpected(timed_out))),
                           timed_out.what(), decltype(timed_out));
    }

    SUBCASE("nothing expected, but an error could occur") {
      jl::unwrap(std::expected<void, jl::error>());
      CHECK_THROWS_WITH_AS(jl::unwrap(std::expected<void, jl::error>(std::unexpected(timed_out))),
                           timed_out.what(), decltype(timed_out));
    }
  }

  TEST_CASE("ok_or") {
    jl::error timed_out(ETIMEDOUT, "foo");
    CHECK(42 == jl::ok_or(std::optional(42), timed_out).value());
    CHECK(what(timed_out) == error_what(jl::ok_or<int>(std::nullopt, timed_out)));
  }

  TEST_CASE("ok_or_else") {
    auto make_timed_out = [] { return jl::error(ETIMEDOUT, "foo"); };
    CHECK(42 == jl::ok_or_else(std::optional(42), make_timed_out).value());
    CHECK(what(make_timed_out()) == error_what(jl::ok_or_else<int>(std::nullopt, make_timed_out)));
  }

  TEST_CASE("retryable_as") {
    static_assert(jl::expected_or_errno<int>(42).or_else(jl::retryable_as<EAGAIN>(0)) == 42);
    static_assert(jl::expected_or_errno<int>(std::unexpected(EAGAIN)).or_else(jl::retryable_as<EAGAIN>(0)) == 0);
    static_assert(jl::expected_or_errno<int>(std::unexpected(ETIMEDOUT)).or_else(jl::retryable_as<EAGAIN>(0)).error() == ETIMEDOUT);

    std::expected<int, std::error_code> ok(42);
    CHECK(ok.or_else(jl::retryable_as<EAGAIN>(0)) == 42);
    CHECK(ok.or_else(jl::retryable_as(0, jl::as_ec(EAGAIN))) == 42);

    std::expected<int, std::error_code> eagain = std::unexpected(jl::as_ec(EAGAIN));
    CHECK(eagain.or_else(jl::retryable_as<EAGAIN>(0)) == 0);
    CHECK(eagain.or_else(jl::retryable_as(0, jl::as_ec(EAGAIN))) == 0);

    std::expected<int, std::error_code> etimedout = std::unexpected(jl::as_ec(ETIMEDOUT));
    CHECK(etimedout.or_else(jl::retryable_as<EAGAIN>(0)).error() == jl::as_ec(ETIMEDOUT));
    CHECK(etimedout.or_else(jl::retryable_as(0, jl::as_ec(EAGAIN))).error() == jl::as_ec(ETIMEDOUT));
  }

  TEST_CASE("ok_or_errno") {
    CHECK(42 == jl::unwrap(jl::ok_or_errno(42)));

    errno = EAGAIN;
    CHECK(0 == jl::ok_or_errno(-1).or_else(jl::retryable_as<EAGAIN>(0)));

    errno = ETIMEDOUT;
    CHECK(ETIMEDOUT == jl::ok_or_errno(-1).or_else(jl::retryable_as<EAGAIN>(0)).error());
  }

  TEST_CASE("try_catch") {
    SUBCASE("successful") {
      CHECK(jl::try_catch<jl::error>([](auto v) { return v; }, true) == true);
    }
    SUBCASE("operation throws") {
      auto error = jl::error(std::make_error_code(std::errc::no_link));
      auto result = jl::try_catch<jl::error>([](const auto& v) -> int { throw v; }, error);
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

  TEST_CASE("retry_syscall") {
    auto two_eagain = [attempts = 2] mutable {
      errno = EAGAIN;
      return attempts-- ? -1 : 42;
    };
    CHECK(42 == jl::retry_syscall<jl::attempts{3}, EAGAIN>(two_eagain));
    CHECK_EQ(EAGAIN, jl::retry_syscall<jl::attempts{2}, EAGAIN>(two_eagain).error());

    auto serious_error_only_on_first_attempt = [attempts = 1] mutable {
      errno = ETIMEDOUT;
      return --attempts >= 0 ? -1 : 42;
    };
    CHECK_EQ(ETIMEDOUT, jl::retry_syscall<jl::attempts{3}, EAGAIN>(serious_error_only_on_first_attempt).error());
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
  TEST_CASE("deadline") {
    using namespace std::chrono;
    sys_seconds now(seconds(1 << 31));
    auto deadline = jl::deadline::after(1min, {.init = 1s, .base = 10}, now);

    auto simulate_backoff = [&now, &deadline]() -> std::string {
      if (auto t = deadline.backoff_duration<seconds>(now); t) {
        now += *t;
        return std::format("{}", *t);
      }
      return "nullopt";
    };

    CHECK(simulate_backoff() == "1s");
    CHECK(simulate_backoff() == "10s");
    CHECK(simulate_backoff() == "49s");  // time left to 1min instead of 100s
    CHECK(simulate_backoff() == "nullopt");
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
      CHECK(jl::retry_until(deadline, only, std::expected<int, jl::error>(42)) == 42);
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
