#include <gtest/gtest.h>
#include <jl.h>

#include <stdexcept>

TEST(Environment, optenv) {
  setenv("JL_TEST_OPTENV", "foo", 1);
  EXPECT_EQ("foo", jl::optenv("JL_TEST_OPTENV").value());

  EXPECT_EQ(std::nullopt, jl::optenv("DONT_SET_THIS"));
  EXPECT_EQ("foo", jl::optenv("DONT_SET_THIS").value_or("foo"));
  EXPECT_EQ(42, std::stoi(jl::optenv("DONT_SET_THIS").value_or("42")));
}

TEST(Environment, reqenv) {
  setenv("JL_TEST_REQENV", "foo", 1);
  EXPECT_EQ("foo", jl::reqenv("JL_TEST_REQENV"));

  EXPECT_THROW(jl::reqenv("DONT_SET_THIS"), std::runtime_error);
}
