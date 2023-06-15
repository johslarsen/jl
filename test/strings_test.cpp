#include <gtest/gtest.h>
#include <jl.h>

TEST(Strings, MaybeQuoted) {
  EXPECT_EQ("", (std::ostringstream() << jl::MaybeQuoted("")).str());

  EXPECT_EQ("word", (std::ostringstream() << jl::MaybeQuoted("word")).str());
  EXPECT_EQ("\"one space\"", (std::ostringstream() << jl::MaybeQuoted("one space")).str());
  EXPECT_EQ("\"other\ntype\rof\twhitespace\"", (std::ostringstream() << jl::MaybeQuoted("other\ntype\rof\twhitespace")).str());

  EXPECT_EQ("\"no extra set of quotes\"", (std::ostringstream() << jl::MaybeQuoted("\"no extra set of quotes\"")).str());
}
