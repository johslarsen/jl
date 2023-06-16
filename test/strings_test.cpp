#include <gtest/gtest.h>
#include <jl.h>

TEST(Strings, MaybeQuoted) {
  EXPECT_EQ("", (std::ostringstream() << jl::MaybeQuoted("")).str());

  EXPECT_EQ("word", (std::ostringstream() << jl::MaybeQuoted("word")).str());
  EXPECT_EQ("\"one space\"", (std::ostringstream() << jl::MaybeQuoted("one space")).str());
  EXPECT_EQ("\"other\ntype\rof\twhitespace\"", (std::ostringstream() << jl::MaybeQuoted("other\ntype\rof\twhitespace")).str());

  EXPECT_EQ("\"no extra set of quotes\"", (std::ostringstream() << jl::MaybeQuoted("\"no extra set of quotes\"")).str());
}

TEST(String, MaybeQuotedJSON) {
  std::string_view compact_json(R"({"compact":"json with space and \""}")");
  EXPECT_EQ(compact_json, (std::ostringstream() << jl::MaybeQuoted(compact_json)).str());
  EXPECT_EQ(R"("{
  \"formatted\": \"json with space and \\\"\"
}")",
            (std::stringstream() << jl::MaybeQuoted(R"({
  "formatted": "json with space and \""
})"))
                .str());
}

TEST(String, MaybeQuotedWithCheckLimit) {
  std::string_view str = "string with space";
  EXPECT_EQ(str, (std::ostringstream() << jl::MaybeQuoted(str).check_first(6)).str());
  EXPECT_EQ("\"string with space\"", (std::ostringstream() << jl::MaybeQuoted(str).check_first(7)).str());
}

template <jl::fixed_string Str>
constexpr std::string_view view_of() {
  return std::string_view(Str.chars.data(), Str.chars.size());
}
TEST(String, FixedString) {
  EXPECT_EQ("foo", view_of<"foo">());
  EXPECT_EQ("bar", view_of<"bar">());
}
