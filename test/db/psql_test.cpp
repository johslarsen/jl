#include "mock_test.h"

static auto psql_uri = jl::env("POSTGRES_URL");

TEST_SUITE("psql" * doctest::skip(!psql_uri.has_value())) {
  TEST_CASE("table create insert select drop") {
    verify_queries(jl::db::open(*psql_uri), test_table_create_insert_select_drop("bytea"));
  }
}
