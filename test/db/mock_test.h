#include <doctest/doctest.h>
#include <jl_db.h>

using query_params_expected = std::tuple<std::string, std::vector<jl::db::param>, std::vector<std::vector<jl::db::param>>, std::vector<std::string>>;

inline std::vector<query_params_expected> test_table_create_insert_select_drop(std::string_view blob_type = "BLOB") {
  using jl::db::null;
  return {{
      {"DROP TABLE IF EXISTS jl_db_test;", {}, {}, {}},
      {std::format("CREATE TABLE jl_db_test(i32 INTEGER, i64 BIGINT, f64 DOUBLE PRECISION, str TEXT, blob {});", blob_type), {}, {}, {}},
      {"SELECT * FROM jl_db_test;", {}, {}, {}},
      {"INSERT INTO jl_db_test VALUES ($1, $2, $3, $4, $5);", {{42, 0x0123456789abcdef, M_PI, "foo", std::as_bytes(std::span("bar"))}}, {}, {}},
      {"INSERT INTO jl_db_test VALUES ($1, $2, $3, $4, $5);", {{null, null, null, null, null}}, {}, {}},
      {"SELECT * FROM jl_db_test;", {}, {
                                            {{42, 0x0123456789abcdef, M_PI, "foo", std::as_bytes(std::span("bar"))}},
                                            {{null, null, null, null, null}},
                                        },
       {"i32", "i64", "f64", "str", "blob"}},
      {"DROP TABLE IF EXISTS jl_db_test;", {}, {}, {}},
      {"-- some backends handle empty / comment only queries differently", {}, {}, {}},
  }};
}
inline void verify_queries(std::unique_ptr<jl::db::connection> db, std::span<const query_params_expected> queries) {
  for (const auto& [sql, params, expected, expected_columns] : queries) {
    CAPTURE(sql);
    auto result = db->execv(sql, params);
    CHECK(result.empty() == expected.empty());

    if (!expected_columns.empty()) {
      std::vector<std::string> columns(result.ncolumn());
      for (int i = 0; i < result.ncolumn(); i++) columns[i] = result[i].name();
      CHECK(columns == expected_columns);
    }

    size_t nrow = 0;
    for (auto& row : result) {
      for (auto [j, field] : std::views::enumerate(expected[nrow++])) {
        INFO("row=", nrow - 1, " col=", j);

        // test lookup via column names if given, otherwise fallback to column index:
        auto v = expected_columns.empty() ? row[static_cast<int>(j)] : row[expected_columns.at(j)];

        std::visit(jl::overload{
                       [&](std::monostate) { CHECK(v.isnull()); },
                       [&](const std::string_view& s) { CHECK(v.str() == s); },
                       [&](int32_t i) { CHECK(v.i32() == i); },
                       [&](int64_t i) { CHECK(v.i64() == i); },
                       [&](double n) { CHECK(v.f64() == n); },
                       [&](std::span<const std::byte> blob) {
                         CHECK(jl::to_xdigits(v.blob().value(), " ") == jl::to_xdigits(blob, " "));
                       },
                   },
                   field);
      }
    }
    CHECK(nrow == expected.size());
  }
}
