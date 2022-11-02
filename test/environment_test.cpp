#include <gtest/gtest.h>
#include <jl.h>

#include <cmath>
#include <stdexcept>

TEST(Environment, optenv) {
  setenv("JL_TEST_OPTENV", "foo", 1);  // NOLINT(*mt-unsafe)
  EXPECT_EQ("foo", jl::optenv("JL_TEST_OPTENV").value());

  EXPECT_EQ(std::nullopt, jl::optenv("DONT_SET_THIS"));
  EXPECT_EQ("foo", jl::optenv("DONT_SET_THIS").value_or("foo"));
  EXPECT_EQ(42, std::stoi(jl::optenv("DONT_SET_THIS").value_or("42")));
}

TEST(Environment, reqenv) {
  setenv("JL_TEST_REQENV", "foo", 1);  // NOLINT(*mt-unsafe)
  EXPECT_EQ("foo", jl::reqenv("JL_TEST_REQENV"));

  EXPECT_THROW((void)jl::reqenv("DONT_SET_THIS"), std::runtime_error);
}

TEST(Environment, EnvOrNumeric) {
  setenv("JL_TEST_ENV_OR_INT", "42", 1);      // NOLINT(*mt-unsafe)
  setenv("JL_TEST_ENV_OR_FLOAT", "3.14", 1);  // NOLINT(*mt-unsafe)
  EXPECT_EQ(42, jl::env_or("JL_TEST_ENV_OR_INT", 13));
  EXPECT_EQ(3.14, jl::env_or("JL_TEST_ENV_OR_FLOAT", 42.0));
  EXPECT_EQ(3, jl::env_or("JL_TEST_ENV_OR_FLOAT", 42));

  EXPECT_EQ(42, jl::env_or("DONT_SET_THIS", 42));
  EXPECT_EQ(3.14, jl::env_or("DONT_SET_THIS", 3.14));
}

TEST(Environment, EnvOrInvalidNumberThrows) {
  setenv("JL_TEST_ENV_OR_NAN", "NaN", 1);  // NOLINT(*mt-unsafe)
  EXPECT_THROW((void)jl::env_or("JL_TEST_ENV_OR_NAN", 42), std::system_error);
  EXPECT_TRUE(std::isnan(jl::env_or("JL_TEST_ENV_OR_NAN", 3.14)));
}

TEST(Environment, EnvOrString) {
  setenv("JL_TEST_ENV_OR_STRING", "foo", 1);  // NOLINT(*mt-unsafe)
  EXPECT_EQ("foo", jl::env_or("JL_TEST_ENV_OR_STRING", "fallback"));

  EXPECT_EQ("chars", jl::env_or("DONT_SET_THIS", "chars"));
  EXPECT_EQ("string", jl::env_or("DONT_SET_THIS", std::string("string")));
}
