#include <doctest/doctest.h>
#include <jl_db.h>

using namespace std::string_literals;

using query_params_expected = std::tuple<std::string, std::vector<jl::db::connection::param>, std::vector<std::vector<jl::db::connection::param>>>;

inline std::array<query_params_expected, 8> test_table_create_insert_select_drop(std::string_view blob_type = "BLOB") {
  return {{
      {"DROP TABLE IF EXISTS jl_db_test;", {}, {}},
      {std::format("CREATE TABLE jl_db_test(i32 INTEGER, i64 BIGINT, f64 DOUBLE PRECISION, str TEXT, blob {});", blob_type), {}, {}},
      {"SELECT * FROM jl_db_test;", {}, {}},
      {"INSERT INTO jl_db_test VALUES ($1, $2, $3, $4, $5);", {{42, 0x0123456789abcdef, M_PI, "foo"s, std::as_bytes(std::span("bar"))}}, {}},
      {"INSERT INTO jl_db_test VALUES ($1, $2, $3, $4, $5);", {{{}, {}, {}, {}, {}}}, {}},
      {"SELECT * FROM jl_db_test;", {}, {
                                            {{42, 0x0123456789abcdef, M_PI, "foo"s, std::as_bytes(std::span("bar"))}},
                                            {{{}, {}, {}, {}, {}}},
                                        }},
      {"DROP TABLE IF EXISTS jl_db_test;", {}, {}},
      {"-- some backends handle empty / comment only queries differently", {}, {}},
  }};
}
inline void verify_queries(std::unique_ptr<jl::db::connection> db, std::span<const query_params_expected> queries) {
  for (const auto& [sql, params, expected] : queries) {
    CAPTURE(sql);
    auto result = db->exec(sql, params);
    CHECK(result.empty() == expected.empty());
    size_t nrow = 0;
    for (auto& row : result) {
      for (auto [j, field] : std::views::enumerate(expected[nrow++])) {
        INFO("row=", nrow - 1, " col=", j);
        std::visit(jl::overload{
                       [&](std::monostate) { CHECK(row[j].isnull()); },
                       [&](const std::string_view& s) { CHECK(row[j].str() == s); },
                       [&](int32_t i) { CHECK(row[j].i32() == i); },
                       [&](int64_t i) { CHECK(row[j].i64() == i); },
                       [&](double n) { CHECK(row[j].f64() == n); },
                       [&](std::span<const std::byte> blob) {
                         CHECK(jl::to_xdigits(row[j].blob().value(), " ") == jl::to_xdigits(blob, " "));
                       },
                   },
                   field);
      }
    }
    CHECK(nrow == expected.size());
  }
}
