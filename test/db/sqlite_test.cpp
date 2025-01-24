#include "mock_test.h"

TEST_SUITE("SQLite3") {
  TEST_CASE("table create insert select drop") {
    verify_queries(jl::db::open("file:///db.sqlite3?mode=memory"), test_table_create_insert_select_drop());
  }
}
