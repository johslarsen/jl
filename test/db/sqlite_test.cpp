#include "mock_test.h"

TEST_SUITE("SQLite3") {
  TEST_CASE("table create insert select drop") {
    verify_queries(jl::db::open("file:///db.sqlite3?mode=memory"), test_table_create_insert_select_drop());
  }

  TEST_CASE("db::mock as a man in the middle proxy") {
    jl::db::mock proxy([backend = jl::db::open("file:///db.sqlite3?mode=memory")](std::string sql, const auto& params) {
      std::ranges::replace(sql, 'o', 'a');
      auto new_params = std::vector<jl::db::param>(params.size(), 42);
      return backend->execv(sql, new_params);
    });
    auto result = proxy.exec("SELECT 'foo', $1, $2", 13, "foo");
    CHECK(result[0].str() == "faa");
    CHECK(result[1].i32() == 42);
    CHECK(result[2].i32() == 42);
  }
}
