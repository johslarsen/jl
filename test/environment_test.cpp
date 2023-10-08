#include <doctest/doctest.h>
#include <gtest/gtest.h>
#include <jl.h>

#include <cmath>
#include <stdexcept>

TEST_SUITE("environment") {
  TEST_CASE("optenv") {
    setenv("JL_TEST_OPTENV", "foo", 1);                    // NOLINT(*mt-unsafe)
    CHECK("foo" == jl::optenv("JL_TEST_OPTENV").value());  // NOLINT(*unchecked*)

    CHECK(std::nullopt == jl::optenv("DONT_SET_THIS"));
    CHECK("foo" == jl::optenv("DONT_SET_THIS").value_or("foo"));
    CHECK(42 == std::stoi(jl::optenv("DONT_SET_THIS").value_or("42")));
  }

  TEST_CASE("reqenv") {
    setenv("JL_TEST_REQENV", "foo", 1);  // NOLINT(*mt-unsafe)
    CHECK("foo" == jl::reqenv("JL_TEST_REQENV"));

    CHECK_THROWS_AS((void)jl::reqenv("DONT_SET_THIS"), std::runtime_error);
  }

  TEST_CASE("env_or numeric") {
    setenv("JL_TEST_ENV_OR_INT", "42", 1);      // NOLINT(*mt-unsafe)
    setenv("JL_TEST_ENV_OR_FLOAT", "3.14", 1);  // NOLINT(*mt-unsafe)
    CHECK(42 == jl::env_or("JL_TEST_ENV_OR_INT", 13));
    CHECK(3.14 == jl::env_or("JL_TEST_ENV_OR_FLOAT", 42.0));
    CHECK(3 == jl::env_or("JL_TEST_ENV_OR_FLOAT", 42));

    CHECK(42 == jl::env_or("DONT_SET_THIS", 42));
    CHECK(3.14 == jl::env_or("DONT_SET_THIS", 3.14));
  }

  TEST_CASE("env_or invalid number throws") {
    setenv("JL_TEST_ENV_OR_NAN", "NaN", 1);  // NOLINT(*mt-unsafe)
    CHECK_THROWS_AS((void)jl::env_or("JL_TEST_ENV_OR_NAN", 42), std::system_error);
    CHECK(std::isnan(jl::env_or("JL_TEST_ENV_OR_NAN", 3.14)));
  }

  TEST_CASE("env_or string") {
    setenv("JL_TEST_ENV_OR_STRING", "foo", 1);  // NOLINT(*mt-unsafe)
    CHECK("foo" == jl::env_or("JL_TEST_ENV_OR_STRING", "fallback"));

    CHECK("chars" == jl::env_or("DONT_SET_THIS", "chars"));
    CHECK("string" == jl::env_or("DONT_SET_THIS", std::string("string")));
  }
}
