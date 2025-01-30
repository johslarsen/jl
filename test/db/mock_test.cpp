#include "mock_test.h"

#include <numbers>

TEST_SUITE("mock db") {
  TEST_CASE("exec varargs") {
    jl::db::mock db([](const auto& /*sql*/, const auto& params) {
      if (!params.empty()) {
        CHECK(std::get<int32_t>(params[0]) == 42);
        CHECK(std::get<int32_t>(params[1]) == -1);
        CHECK(std::get<int32_t>(params[2]) == 1);
        CHECK(std::get<int32_t>(params[3]) == 0);

        CHECK(std::get<int64_t>(params[4]) == 0);
        CHECK(std::get<int64_t>(params[5]) == 0xf00ba4ba2);

        CHECK(std::get<double>(params[6]) == std::numbers::pi_v<float>);
        CHECK(std::get<double>(params[7]) == M_PI);

        CHECK(std::get<const char*>(params[8]) == "");
        CHECK(std::get<const char*>(params[9]) == "foo");
        CHECK(std::get<std::string>(params[10]) == "bar");
        CHECK(std::get<std::string_view>(params[11]) == "baz");

        CHECK(jl::to_xdigits(std::get<std::span<const std::byte>>(params[12])) == jl::to_xdigits(std::span<const std::byte>{}));
        CHECK(jl::to_xdigits(std::get<std::span<const std::byte>>(params[13])) == "666f6f00");  // i.e. foo\0

        CHECK(std::get<std::monostate>(params[14]) == std::monostate{});  // i.e. NULL
        CHECK(std::get<std::monostate>(params[15]) == std::monostate{});  // i.e. NULL
        CHECK(std::get<std::monostate>(params[16]) == std::monostate{});  // i.e. NULL
      }
      return jl::db::mock::table({});
    });
    db.exec("");  // no prepared parameters
    db.exec("",
            42, -1, true, false,                                            // i32
            0L, 0xf00ba4ba2L,                                               // i64
            std::numbers::pi_v<float>, M_PI,                                // f64
            "", "foo", std::string("bar"), std::string_view("baz"),         // text
            std::span<const std::byte>{}, std::as_bytes(std::span("foo")),  // blob
            jl::db::null, std::monostate{}, jl::db::param{}                 // NULL
    );
  }
  TEST_CASE("field accessors") {
    auto blob = jl::from_xdigits("f00ba4");
    auto table = jl::db::mock::table(
        {{
            int32_t(42),
            int64_t(0xdeadbeef),
            M_PI,
            std::string("foo"),
            std::string_view("bar"),
            "baz",
            blob,
            jl::db::null,
        }},
        std::vector<std::string>{"i32", "i64", "f64", "str", "sv", "c_str", "blob", "null"});
    CHECK(table.ncolumn() == 8);

    CHECK(table["i32"].i32() == 42);
    CHECK(table["i64"].i64() == 0xdeadbeef);
    CHECK(table["f64"].f64() == M_PI);
    CHECK(table["str"].str() == "foo");
    CHECK(table["sv"].str() == "bar");
    CHECK(table["c_str"].str() == "baz");
    CHECK(jl::to_xdigits(table["blob"].blob().value()) == jl::to_xdigits(blob));

    CHECK(!table[0].isnull());
    CHECK(table[7].isnull());
    CHECK(table["null"].str() == std::nullopt);
  }

  TEST_CASE("table create insert select drop") {
    auto results = test_table_create_insert_select_drop();
    auto db = std::make_unique<jl::db::mock>([&results, i = 0](const auto& /*sql*/, const auto& /*params*/) mutable {
      const auto& [_sql, _params, expected, columns] = results.at(i++);
      return jl::db::mock::table(expected, columns);
    });
    verify_queries(std::move(db), results);
  }

  TEST_CASE("result is ranges compatible") {
    auto db = std::make_unique<jl::db::mock>([](const auto& /*sql*/, const auto& /*params*/) {
      return jl::db::mock::table({{1}, {2}, {3}});
    });
    auto as_ints = std::views::transform(db->exec("ignored"), [](auto& r) { return r[0].i32().value_or(0); });
    CHECK(std::ranges::fold_left(as_ints, 0, std::plus{}) == 1 + 2 + 3);
  }
}
