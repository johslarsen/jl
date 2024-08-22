#include <doctest/doctest.h>
#include <jl.h>

TEST_SUITE("strings") {
  TEST_CASE("find unescaped") {
    auto isspace = [](char ch) { return ch == ' '; };
    CHECK(3 == jl::find_unescaped("foo bar baz", ' '));
    CHECK(3 == jl::find_unescaped("foo bar baz", isspace));
    CHECK(8 == jl::find_unescaped("foo\\ bar baz", ' '));
    CHECK(8 == jl::find_unescaped("foo\\ bar baz", isspace));
    CHECK(std::string::npos == jl::find_unescaped("foo\\ bar\\ baz", ' '));
    CHECK(std::string::npos == jl::find_unescaped("foo\\ bar\\ baz", isspace));

    CHECK(std::string::npos == jl::find_unescaped("neither escape nor matching", '?'));
    CHECK(std::string::npos == jl::find_unescaped("neither escape nor matching", [](char ch) { return ch == '?'; }));
    CHECK(std::string::npos == jl::find_unescaped("", ' '));
    CHECK(std::string::npos == jl::find_unescaped("", isspace));
    CHECK(std::string::npos == jl::find_unescaped("\\\\", ' '));
    CHECK(std::string::npos == jl::find_unescaped("\\\\", isspace));

    // If it ends with an incomplete sequence return the position of this. Safer than the alternative, right?
    CHECK(0 == jl::find_unescaped("\\", ' '));
    CHECK(0 == jl::find_unescaped("\\", isspace));
    CHECK(5 == jl::find_unescaped("foo\\\\\\", ' '));
    CHECK(5 == jl::find_unescaped("foo\\\\\\", isspace));
  }

  TEST_CASE("needs quotes") {
    CHECK_MESSAGE(!jl::needs_quotes("foo"), "Only safe characters");
    CHECK_MESSAGE(jl::needs_quotes("foo bar"), "With unsafe characters");

    CHECK_MESSAGE(!jl::needs_quotes(""), "Empty string");
    CHECK_MESSAGE(!jl::needs_quotes(R"("foo bar")"), "Fully quoted");
    CHECK_MESSAGE(!jl::needs_quotes(R"(foo\ bar)"), "Escaped");
    CHECK_MESSAGE(!jl::needs_quotes(R"(foo\"bar)"), "Unquoted with escaped quote");
    CHECK_MESSAGE(!jl::needs_quotes(R"("foo\" bar")"), "Quoted with escaped quote");
    CHECK_MESSAGE(!jl::needs_quotes(R"(foo" "b"ar """"b"az)"), "Multiple quoted sections");

    CHECK_MESSAGE(jl::needs_quotes(R"(foo\ bar baz)"), "Partially escaped");
    CHECK_MESSAGE(jl::needs_quotes(R"("foo bar" baz)"), "Partially quoted");
    CHECK_MESSAGE(jl::needs_quotes(R"("foo bar)"), "Unmatched quotes");
    CHECK_MESSAGE(jl::needs_quotes(R"(foo\)"), "Ends with incomplete escape sequence");
  }

  TEST_CASE("MaybeQuoted") {
    SUBCASE("basic") {
      CHECK("" == (std::ostringstream() << jl::MaybeQuoted("")).str());

      CHECK("word" == (std::ostringstream() << jl::MaybeQuoted("word")).str());
      CHECK("\"one space\"" == (std::ostringstream() << jl::MaybeQuoted("one space")).str());
      CHECK("\"other\ntype\rof\twhitespace\"" == (std::ostringstream() << jl::MaybeQuoted("other\ntype\rof\twhitespace")).str());

      CHECK("\"no extra set of quotes\"" == (std::ostringstream() << jl::MaybeQuoted("\"no extra set of quotes\"")).str());
    }
    SUBCASE("JSON") {
      auto isspace = [](unsigned char ch) { return std::isspace(ch) != 0; };
      std::string_view compact_json(R"({"compact":"json with space and \""})");
      CHECK(compact_json == (std::ostringstream() << jl::MaybeQuoted<decltype(isspace)>(compact_json)).str());

      CHECK(R"("{
  \"formatted\": \"json with space and \\\"\"
}")" ==
            (std::stringstream() << jl::MaybeQuoted<decltype(isspace)>(R"({
  "formatted": "json with space and \""
})"))
                .str());
    }
  }

  template <jl::fixed_string Str>
  constexpr std::string_view view_of() {
    return std::string_view(Str.chars.data(), Str.chars.size());
  }
  TEST_CASE("fixed string") {
    CHECK("foo" == view_of<"foo">());
    CHECK("bar" == view_of<"bar">());
  }

  TEST_CASE("join") {
    CHECK("" == jl::join(std::vector<std::string>{}));
    CHECK("foo,bar,baz" == jl::join(std::vector{"foo", "bar", "baz"}));
  }

  TEST_CASE("from_str") {
    CHECK(42 == jl::from_str<int>("42").value());
    REQUIRE(doctest::Approx(3.14) == jl::from_str<float>("3.14").value());
    CHECK(3 == jl::from_str<int>("3.14").value());

    CHECK_MESSAGE(!jl::from_str<int>("").has_value(), "Empty string is not an integer");
    CHECK_MESSAGE(!jl::from_str<int>("abc").has_value(), "Integers starts with digits, not characters");
  }

  TEST_CASE("line_eol") {
    SUBCASE("at end of input") {
      auto [standard_line, nl] = jl::line_eol::find_first_in("foo\n");
      CHECK(standard_line == "foo");
      CHECK(nl == "\n");

      auto [windows_line, crlf] = jl::line_eol::find_first_in("foo\r\n");
      CHECK(windows_line == "foo");
      CHECK(crlf == "\r\n");

      auto [mac_line, cr] = jl::line_eol::find_first_in("foo\r");
      CHECK(mac_line == "foo");
      CHECK(cr == "\r");
    }
    SUBCASE("in the middle of input") {
      auto [standard_line, nl] = jl::line_eol::find_first_in("foo\nbar");
      CHECK(standard_line == "foo");
      CHECK(nl == "\n");

      auto [windows_line, crlf] = jl::line_eol::find_first_in("foo\r\nbar");
      CHECK(windows_line == "foo");
      CHECK(crlf == "\r\n");

      auto [mac_line, cr] = jl::line_eol::find_first_in("foo\rbar");
      CHECK(mac_line == "foo");
      CHECK(cr == "\r");
    }
    SUBCASE("incomplete lines") {
      auto [no_line, no_eol] = jl::line_eol::find_first_in("");
      CHECK(no_line == "");
      CHECK(no_eol == "");

      auto [line, but_no_eol] = jl::line_eol::find_first_in("foo");
      CHECK(line == "foo");
      CHECK(but_no_eol == "");
    }
  }
}
