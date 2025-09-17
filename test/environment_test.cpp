#include <doctest/doctest.h>
#include <jl.h>

#include <cmath>
#include <stdexcept>

TEST_SUITE("environment") {
  TEST_CASE("example with checked and defaults options") {
    auto read_options_from_env = [] {
      return jl::ok_or_join_with("\n",
                                 jl::env("JL_TEST_ENV_STRING"),
                                 jl::env_as<float>("JL_TEST_ENV_FLOAT"),
                                 jl::env_or<int>("JL_TEST_ENV_INT", 42));
    };

    setenv("JL_TEST_ENV_STRING", "foo", 1);  // NOLINT(*mt-unsafe)
    setenv("JL_TEST_ENV_FLOAT", "13", 1);    // NOLINT(*mt-unsafe)
    unsetenv("JL_TEST_ENV_INT");             // NOLINT(*mt-unsafe)
    auto all_ok = read_options_from_env();
    CHECK_MESSAGE(all_ok, all_ok.error().what());
    CHECK(all_ok == std::tuple(std::string("foo"), 13, 42));

    unsetenv("JL_TEST_ENV_STRING");       // NOLINT(*mt-unsafe)
    unsetenv("JL_TEST_ENV_FLOAT");        // NOLINT(*mt-unsafe)
    setenv("JL_TEST_ENV_INT", "NaN", 1);  // NOLINT(*mt-unsafe)
    auto none_ok = read_options_from_env();
    CHECK(!none_ok);
    CHECK(std::string_view(none_ok.error().what()) ==
          R"(environment JL_TEST_ENV_STRING: Invalid argument
environment JL_TEST_ENV_FLOAT: Invalid argument
environment JL_TEST_ENV_INT failed to parse "NaN": Invalid argument)");
  }

  TEST_CASE("env_as distinguishes missing from parsing error") {
    setenv("JL_TEST_ENV_NAN", "NaN", 1);  // NOLINT(*mt-unsafe)
    CHECK("environment DONT_SET_THIS: Invalid argument" == std::string_view(jl::env_as<int>("DONT_SET_THIS").error().what()));
    CHECK(R"(environment JL_TEST_ENV_NAN failed to parse "NaN": Invalid argument)" == std::string_view(jl::env_as<int>("JL_TEST_ENV_NAN").error().what()));
  }

  TEST_CASE("env_or") {
    SUBCASE("numeric") {
      setenv("JL_TEST_ENV_INT", "42", 1);      // NOLINT(*mt-unsafe)
      setenv("JL_TEST_ENV_FLOAT", "3.14", 1);  // NOLINT(*mt-unsafe)
      CHECK(42 == jl::env_or("JL_TEST_ENV_INT", 13));
      REQUIRE(doctest::Approx(3.14) == jl::env_or("JL_TEST_ENV_FLOAT", 42.0));
      CHECK(3 == jl::env_or("JL_TEST_ENV_FLOAT", 42));

      CHECK(42 == jl::env_or("DONT_SET_THIS", 42));
      REQUIRE(doctest::Approx(3.14) == jl::env_or("DONT_SET_THIS", 3.14));
    }

    SUBCASE("string") {
      setenv("JL_TEST_ENV_OR_STRING", "foo", 1);  // NOLINT(*mt-unsafe)
      CHECK("foo" == jl::env_or("JL_TEST_ENV_OR_STRING", "fallback"));

      CHECK("chars" == jl::env_or("DONT_SET_THIS", "chars"));
      CHECK("string" == jl::env_or("DONT_SET_THIS", std::string("string")));
    }

    SUBCASE("invalid number") {
      setenv("JL_TEST_ENV_NAN", "NaN", 1);  // NOLINT(*mt-unsafe)
      CHECK(!jl::env_as<int>("JL_TEST_ENV_NAN"));
      CHECK(std::isnan(jl::env_as<float>("JL_TEST_ENV_NAN").value()));
    }
  }
}
