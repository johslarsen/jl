#include "mock_test.h"

TEST_SUITE("mock db") {
  TEST_CASE("field accessors") {
    auto blob = jl::from_xdigits("f00ba4");
    auto table = jl::db::mock::table({{
        int32_t(42),
        int64_t(0xdeadbeef),
        M_PI,
        "foo"s,
        std::string_view("bar"),
        blob,
        {},  // i.e. NULL

    }});
    CHECK(table.ncolumn() == 7);

    CHECK(table[0].i32() == 42);
    CHECK(table[1].i64() == 0xdeadbeef);
    CHECK(table[2].f64() == M_PI);
    CHECK(table[3].str() == "foo");
    CHECK(table[4].str() == "bar");
    CHECK(jl::to_xdigits(table[5].blob().value()) == jl::to_xdigits(blob));

    CHECK(!table[0].isnull());
    CHECK(table[6].isnull());
    CHECK(table[6].str() == std::nullopt);
  }

  TEST_CASE("table create insert select drop") {
    auto results = test_table_create_insert_select_drop();
    auto db = std::make_unique<jl::db::mock>([&results, i = 0](const auto& /*sql*/, const auto& /*params*/) mutable {
      const auto& [_sql, _params, expected] = results.at(i++);
      return jl::db::mock::table(expected);
    });
    verify_queries(std::move(db), results);
  }
}
