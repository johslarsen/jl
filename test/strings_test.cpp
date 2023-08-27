#include <gtest/gtest.h>
#include <jl.h>

TEST(Strings, FindUnescaped) {
  auto isspace = [](char ch) { return ch == ' '; };
  EXPECT_EQ(3, jl::find_unescaped("foo bar baz", ' '));
  EXPECT_EQ(3, jl::find_unescaped("foo bar baz", isspace));
  EXPECT_EQ(8, jl::find_unescaped("foo\\ bar baz", ' '));
  EXPECT_EQ(8, jl::find_unescaped("foo\\ bar baz", isspace));
  EXPECT_EQ(std::string::npos, jl::find_unescaped("foo\\ bar\\ baz", ' '));
  EXPECT_EQ(std::string::npos, jl::find_unescaped("foo\\ bar\\ baz", isspace));

  EXPECT_EQ(std::string::npos, jl::find_unescaped("neither escape nor matching", '?'));
  EXPECT_EQ(std::string::npos, jl::find_unescaped("neither escape nor matching", [](char ch) { return ch == '?'; }));
  EXPECT_EQ(std::string::npos, jl::find_unescaped("", ' '));
  EXPECT_EQ(std::string::npos, jl::find_unescaped("", isspace));
  EXPECT_EQ(std::string::npos, jl::find_unescaped("\\\\", ' '));
  EXPECT_EQ(std::string::npos, jl::find_unescaped("\\\\", isspace));

  // If it ends with an incomplete sequence return the position of this. Safer than the alternative, right?
  EXPECT_EQ(0, jl::find_unescaped("\\", ' '));
  EXPECT_EQ(0, jl::find_unescaped("\\", isspace));
  EXPECT_EQ(5, jl::find_unescaped("foo\\\\\\", ' '));
  EXPECT_EQ(5, jl::find_unescaped("foo\\\\\\", isspace));
}

TEST(Strings, NeedsQuotes) {
  EXPECT_FALSE(jl::needs_quotes("foo")) << "Only safe characters";
  EXPECT_TRUE(jl::needs_quotes("foo bar")) << "With unsafe characters";

  EXPECT_FALSE(jl::needs_quotes("")) << "Empty string";
  EXPECT_FALSE(jl::needs_quotes(R"("foo bar")")) << "Fully quoted";
  EXPECT_FALSE(jl::needs_quotes(R"(foo\ bar)")) << "Escaped";
  EXPECT_FALSE(jl::needs_quotes(R"(foo\"bar)")) << "Unquoted with escaped quote";
  EXPECT_FALSE(jl::needs_quotes(R"("foo\" bar")")) << "Quoted with escaped quote";
  EXPECT_FALSE(jl::needs_quotes(R"(foo" "b"ar """"b"az)")) << "Multiple quoted sections";

  EXPECT_TRUE(jl::needs_quotes(R"(foo\ bar baz)")) << "Partially escaped";
  EXPECT_TRUE(jl::needs_quotes(R"("foo bar" baz)")) << "Partially quoted";
  EXPECT_TRUE(jl::needs_quotes(R"("foo bar)")) << "Unmatched quotes";
  EXPECT_TRUE(jl::needs_quotes(R"(foo\)")) << "Ends with incomplete escape sequence";
}

TEST(Strings, MaybeQuoted) {
  EXPECT_EQ("", (std::ostringstream() << jl::MaybeQuoted("")).str());

  EXPECT_EQ("word", (std::ostringstream() << jl::MaybeQuoted("word")).str());
  EXPECT_EQ("\"one space\"", (std::ostringstream() << jl::MaybeQuoted("one space")).str());
  EXPECT_EQ("\"other\ntype\rof\twhitespace\"", (std::ostringstream() << jl::MaybeQuoted("other\ntype\rof\twhitespace")).str());

  EXPECT_EQ("\"no extra set of quotes\"", (std::ostringstream() << jl::MaybeQuoted("\"no extra set of quotes\"")).str());
}

TEST(String, MaybeQuotedJSON) {
  auto isspace = [](unsigned char ch) { return std::isspace(ch) != 0; };
  std::string_view compact_json(R"({"compact":"json with space and \""})");
  EXPECT_EQ(compact_json, (std::ostringstream() << jl::MaybeQuoted<decltype(isspace)>(compact_json)).str());
  EXPECT_EQ(R"("{
  \"formatted\": \"json with space and \\\"\"
}")",
            (std::stringstream() << jl::MaybeQuoted<decltype(isspace)>(R"({
  "formatted": "json with space and \""
})"))
                .str());
}

template <jl::fixed_string Str>
constexpr std::string_view view_of() {
  return std::string_view(Str.chars.data(), Str.chars.size());
}
TEST(String, FixedString) {
  EXPECT_EQ("foo", view_of<"foo">());
  EXPECT_EQ("bar", view_of<"bar">());
}
