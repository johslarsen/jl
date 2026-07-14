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
  static constexpr std::string_view view_of() {
    return std::string_view(Str.chars.data(), Str.chars.size());
  }
  TEST_CASE("fixed string") {
    CHECK("foo" == view_of<"foo">());
    CHECK("bar" == view_of<"bar">());
  }

  TEST_CASE("join and map_to_s") {
    using namespace std::literals;
    CHECK("" == jl::join(std::vector<std::string>{}, ' '));
    CHECK("foo,bar,baz" == jl::join(std::array{"foo"sv, "bar"sv, "baz"sv}, ','));
    CHECK("1, 2, 3" == jl::join(std::views::iota(1, 4) | jl::map_to_s(), ", "sv));
  }

  TEST_CASE("from_str") {
    CHECK(42 == jl::from_str<int>("42").value());
    REQUIRE(doctest::Approx(3.14) == jl::from_str<float>("3.14").value());
    CHECK(3 == jl::from_str<int>("3.14").value());

    CHECK_MESSAGE(!jl::from_str<int>("").has_value(), "Empty string is not an integer");
    CHECK_MESSAGE(!jl::from_str<int>("abc").has_value(), "Integers starts with digits, not characters");
  }

  TEST_CASE("format_into") {
    SUBCASE("format_to_n_error") {
      std::string buffer = "???";
      auto rest = jl::format_into(buffer, "foobar");
      CHECK(!rest.has_value());

      CHECK(buffer == "foo");
    }

    SUBCASE("span") {
      std::string buffer(10, '?');
      auto rest = jl::unwrap(jl::format_into(buffer, "foo"));
      rest = jl::unwrap(jl::format_into(rest, "{}bar", 42));

      CHECK(rest.size() == 2);
      CHECK(buffer == "foo42bar??");
    }

    SUBCASE("silently truncate_into") {
      std::string buffer = "?????";

      auto rest = jl::truncate_into(buffer, "foo");
      rest = jl::truncate_into(rest, "bar");
      rest = jl::truncate_into(rest, "baz");

      CHECK(rest.size() == 0);
      CHECK(buffer == "fooba");
    }
  }

  TEST_CASE("cstr_view") {
    const std::string string("foo");
    constexpr const char* cstr = "bar";

    SUBCASE("basics") {
      const char* from_string = jl::cstr_view(string);
      CHECK(from_string == string.data());
      CHECK(jl::cstr_view(string).data() == string.data());
      CHECK(jl::cstr_view(string).size() == 3);

      const char* from_cstr = jl::cstr_view(cstr);
      CHECK(from_cstr == cstr);
      CHECK(jl::cstr_view(cstr).data() == cstr);
      CHECK(jl::cstr_view(cstr).size() == 3);

      const char* from_literal = jl::cstr_view("bar");
      CHECK(std::string_view(from_literal) == "bar");
    }

    SUBCASE("from std::string_view should not compile") {
      // jl::cstr_view from_sv(std::string_view("foo")); // should not compile
    }
    SUBCASE("convert to std::string should be explicit") {
      std::ignore = std::string(jl::cstr_view(string));
      // std::stoi(jl::cstr_view("42")); // should not compile
    }

    SUBCASE("provided size is used instead of strlen") {
      CHECK(jl::cstr_view(cstr, 1).size() == 1);
      CHECK(jl::cstr_view(cstr, 5).size() == 5);
    }

    SUBCASE("view_of") {
      std::string_view view = jl::view_of(jl::cstr_view(cstr));
      CHECK(view.data() == cstr);
    }
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

  TEST_CASE("linewise") {
    std::vector<std::string> lines;
    auto writer = jl::linewise::pushed_to(lines);

    SUBCASE("aligned") {
      writer("a line\n");
      CHECK(std::exchange(lines, {}) == std::vector<std::string>{"a line"});
      writer("another\n");
      CHECK(std::exchange(lines, {}) == std::vector<std::string>{"another"});
    }

    SUBCASE("misaligned") {
      writer("incomplete");
      CHECK(std::exchange(lines, {}) == std::vector<std::string>{});
      writer(", then finished\nand a complete one\nand incomplete");
      CHECK(std::exchange(lines, {}) == std::vector<std::string>{"incomplete, then finished", "and a complete one"});
      writer(", then continuing");
      CHECK(std::exchange(lines, {}) == std::vector<std::string>{});
      writer(", and finally done\n");
      CHECK(std::exchange(lines, {}) == std::vector<std::string>{"and incomplete, then continuing, and finally done"});
    }

    SUBCASE("special cases") {
      writer("");
      CHECK(std::exchange(lines, {}) == std::vector<std::string>{});
      writer("\n\n");
      CHECK(std::exchange(lines, {}) == std::vector<std::string>{"", ""});
      writer("\r\n");
      CHECK(std::exchange(lines, {}) == std::vector<std::string>{""});
      writer("carriage-return\ris close enough to separate line\n");
      CHECK(std::exchange(lines, {}) == std::vector<std::string>{"carriage-return", "is close enough to separate line"});
    }
  }
}
