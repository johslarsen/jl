#include <doctest/doctest.h>
#include <jl.h>

#include <cmath>
#include <stdexcept>

TEST_SUITE("environment") {
  TEST_CASE("optenv") {
    setenv("JL_TEST_ENV_STRING", "foo", 1);                    // NOLINT(*mt-unsafe)
    CHECK("foo" == jl::optenv("JL_TEST_ENV_STRING").value());  // NOLINT(*unchecked*)

    CHECK(std::nullopt == jl::optenv("DONT_SET_THIS"));
    CHECK("foo" == jl::optenv("DONT_SET_THIS").value_or("foo"));
    CHECK(42 == std::stoi(jl::optenv("DONT_SET_THIS").value_or("42")));
  }

  TEST_CASE("reqenv") {
    setenv("JL_TEST_ENV_STRING", "foo", 1);  // NOLINT(*mt-unsafe)
    CHECK("foo" == jl::reqenv("JL_TEST_ENV_STRING"));

    CHECK_THROWS_AS(std::ignore = jl::reqenv("DONT_SET_THIS"), std::runtime_error);
  }

  TEST_CASE("env_as distinguishes missing from parsing error") {
    setenv("JL_TEST_ENV_NAN", "NaN", 1);  // NOLINT(*mt-unsafe)
    CHECK(std::errc::invalid_argument == jl::env_as<int>("JL_TEST_ENV_NAN").error().code());
    CHECK(std::errc{} == jl::env_as<int>("DONT_SET_THIS").error().code());
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
      CHECK(42 == jl::env_or("JL_TEST_ENV_NAN", 42));
      CHECK(std::isnan(jl::env_or("JL_TEST_ENV_NAN", 3.14)));
    }
  }
}
