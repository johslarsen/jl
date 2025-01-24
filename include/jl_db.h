#pragma once

#include <jl.h>

#include <functional>

namespace jl::db {

struct connection {
  virtual ~connection() = default;
  connection(const connection &) = delete;
  connection &operator=(const connection &) = delete;

  class result {
   public:
    struct sentinel {};
    struct impl {
      virtual ~impl() = default;

      virtual bool operator==(sentinel) = 0;
      virtual void operator++() = 0;

      virtual int ncolumn() = 0;
      virtual std::string_view column_name(int col) = 0;
      virtual bool isnull(int col) = 0;

      virtual int32_t i32(int col) = 0;
      virtual int64_t i64(int col) = 0;
      virtual double f64(int col) = 0;
      virtual std::string_view str(int col) = 0;
      virtual std::span<const std::byte> blob(int col) = 0;

     protected:
      impl() = default;
      impl(const impl &) = default;
      impl &operator=(const impl &) = default;
      impl(impl &&) = default;
      impl &operator=(impl &&) = default;
    };
    explicit result(std::unique_ptr<impl> pimpl) : _pimpl(std::move(pimpl)) {}

    class input_iter {
      result &_result;

     public:
      explicit input_iter(result &result) : _result(result) {}
      result &operator*() { return _result; }
      void operator++() { ++(*_result._pimpl); }
      bool operator==(sentinel /*unused*/) { return (*_result._pimpl) == sentinel{}; }
    };
    input_iter begin() { return input_iter(*this); }
    sentinel end() { return {}; }

    struct field {
      impl &result;
      int col;

      [[nodiscard]] int32_t raw_i32() { return result.i32(col); }
      [[nodiscard]] int64_t raw_i64() { return result.i64(col); }
      [[nodiscard]] double raw_f64() { return result.f64(col); }
      [[nodiscard]] std::string_view raw_str() { return result.str(col); }
      [[nodiscard]] std::span<const std::byte> raw_blob() { return result.blob(col); }

      [[nodiscard]] std::optional<int32_t> i32() { return isnull() ? std::nullopt : std::optional(raw_i32()); }
      [[nodiscard]] std::optional<int64_t> i64() { return isnull() ? std::nullopt : std::optional(raw_i64()); }
      [[nodiscard]] std::optional<double> f64() { return isnull() ? std::nullopt : std::optional(raw_f64()); }
      [[nodiscard]] std::optional<std::string_view> str() { return isnull() ? std::nullopt : std::optional(raw_str()); }
      [[nodiscard]] std::optional<std::span<const std::byte>> blob() { return isnull() ? std::nullopt : std::optional(raw_blob()); }

      [[nodiscard]] std::string_view name() { return result.column_name(col); }
      [[nodiscard]] bool isnull() { return result.isnull(col); }
    };
    [[nodiscard]] field operator[](int col) { return {.result = *_pimpl, .col = col}; };

    [[nodiscard]] int ncolumn() { return _pimpl->ncolumn(); }
    [[nodiscard]] bool empty() { return *_pimpl == end(); }

   private:
    std::unique_ptr<impl> _pimpl;
  };

  using param = std::variant<std::monostate, int32_t, int64_t, double, std::string, std::string_view, std::span<const std::byte>>;
  virtual result exec(const std::string &sql, std::span<const param> params) = 0;

  template <std::convertible_to<param>... Params>
  result exec(const std::string &sql, Params... params) { return exec(sql, {params...}); }

  connection() = default;
};

class mock final : public connection {
  std::move_only_function<connection::result(const std::string &, std::span<const param>)> _exec;

 public:
  explicit mock(decltype(_exec) exec) : _exec(std::move(exec)) {}
  ~mock() override = default;
  mock(const mock &) = delete;
  mock &operator=(const mock &) = delete;

  class result final : public connection::result::impl {
    std::vector<std::vector<param>> _rows;
    std::vector<std::string> _column_names;
    size_t _i = 0;

   public:
    explicit result(std::vector<std::vector<param>> rows, std::vector<std::string> column_names = {}) : _rows(std::move(rows)), _column_names(std::move(column_names)) {
      if (_column_names.empty() && !_rows.empty()) {
        _column_names = std::vector<std::string>(_rows.front().size(), "unknown");
      }
      assert(std::ranges::all_of(_rows, [n = _column_names.size()](const auto &r) { return r.size() == n; }));
    }

    ~result() override = default;
    result(const result &) = default;
    result &operator=(const result &) = default;
    result(result &&) = default;
    result &operator=(result &&) = default;

    bool operator==(connection::result::sentinel /*unused*/) override {
      return _rows.size() == _i;
    }
    void operator++() override {
      assert(_i < _rows.size());
      ++_i;
    }

    int ncolumn() override {
      return static_cast<int>(_rows.at(0).size());
    }
    std::string_view column_name(int col) override {
      return _column_names.at(col);
    };
    bool isnull(int col) override {
      return std::holds_alternative<std::monostate>(_rows.at(_i).at(col));
    }

    int32_t i32(int col) override {
      return std::get<int32_t>(_rows.at(_i).at(col));
    }
    int64_t i64(int col) override {
      return std::get<int64_t>(_rows.at(_i).at(col));
    }
    double f64(int col) override {
      return std::get<double>(_rows.at(_i).at(col));
    }
    std::string_view str(int col) override {
      if (const auto *s = std::get_if<std::string>(&_rows.at(_i).at(col)); s) return *s;
      return std::get<std::string_view>(_rows.at(_i).at(col));
    }
    std::span<const std::byte> blob(int col) override {
      return std::get<std::span<const std::byte>>(_rows.at(_i).at(col));
    }
  };
  /// A fixed table result. Note that getters here are more strictly type checked than regular database APIs.
  [[nodiscard]] static connection::result table(std::vector<std::vector<param>> rows, std::vector<std::string> column_names = {}) {
    return connection::result(std::make_unique<result>(std::move(rows), std::move(column_names)));
  }

  connection::result exec(const std::string &sql, std::span<const param> params) override { return _exec(sql, params); }
};

#ifdef JL_HAS_SQLITE
#include <sqlite3.h>
class sqlite final : public connection {
  std::unique_ptr<sqlite3, jl::deleter<sqlite3_close>> _db;

 public:
  explicit sqlite(sqlite3 *db) : _db(db) {}
  ~sqlite() override = default;
  sqlite(const sqlite &) = delete;
  sqlite &operator=(const sqlite &) = delete;

  static std::unique_ptr<sqlite> open(const std::string &uri) {
    sqlite3 *handle = nullptr;
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_URI;
    auto status = sqlite3_open_v2(uri.c_str(), &handle, flags, nullptr);
    auto db = std::make_unique<sqlite>(handle);
    if (db->_db == nullptr) throw jl::make_system_error(std::errc::not_enough_memory, "sqlite3_open({})", uri);
    if (status != SQLITE_OK) throw error(db->_db.get(), std::format("sqlite3_open({})", uri));

    return db;
  };

  class result final : public connection::result::impl {
    std::unique_ptr<sqlite3_stmt, jl::deleter<sqlite3_finalize>> _stmt;

   public:
    explicit result(sqlite3_stmt *stmt) : _stmt(stmt) {}

    void operator++() override {
      switch (sqlite3_step(_stmt.get())) {
        case SQLITE_DONE:
          _stmt.reset();
          break;
        case SQLITE_ROW:
          break;
        default:
          throw error(sqlite3_db_handle(_stmt.get()), "sqlite3_step");
      }
    }

   protected:
    bool operator==(connection::result::sentinel /*unused*/) override { return _stmt == nullptr; }

    int ncolumn() override { return sqlite3_column_count(_stmt.get()); }
    std::string_view column_name(int col) override { return sqlite3_column_name(_stmt.get(), col); }
    bool isnull(int col) override { return sqlite3_column_type(_stmt.get(), col) == SQLITE_NULL; }

    int32_t i32(int col) override { return sqlite3_column_int(_stmt.get(), col); }
    int64_t i64(int col) override { return sqlite3_column_int64(_stmt.get(), col); }
    double f64(int col) override { return sqlite3_column_double(_stmt.get(), col); }
    std::string_view str(int col) override {
      return {reinterpret_cast<const char *>(sqlite3_column_text(_stmt.get(), col)), nbyte(col)};
    }
    std::span<const std::byte> blob(int col) override {
      return {reinterpret_cast<const std::byte *>(sqlite3_column_blob(_stmt.get(), col)), nbyte(col)};
    }

   private:
    [[nodiscard]] size_t nbyte(int col) { return sqlite3_column_bytes(_stmt.get(), col); }
  };

  connection::result exec(const std::string &sql, std::span<const param> params) override {
    sqlite3_stmt *stmt = nullptr;
    auto status = sqlite3_prepare_v3(_db.get(), sql.data(), static_cast<int>(sql.size()), 0, &stmt, nullptr);
    if (status != SQLITE_OK) throw error(_db.get(), "sqlite_prepare");
    if (stmt == nullptr) {     // i.e. sql is empty (or only comments),
      return mock::table({});  // so fake an empty result
    }

    auto query = std::make_unique<result>(stmt);
    for (const auto &[i, p] : std::views::enumerate(params)) {
      int idx = 1 + static_cast<int>(i);
      auto status = std::visit(
          jl::overload{
              [&](std::monostate) { return sqlite3_bind_null(stmt, idx); },
              [&](int32_t i) { return sqlite3_bind_int(stmt, idx, i); },
              [&](int64_t i) { return sqlite3_bind_int64(stmt, idx, i); },
              [&](double i) { return sqlite3_bind_double(stmt, idx, i); },
              [&](std::string_view s) { return sqlite3_bind_text(stmt, idx, s.data(), static_cast<int>(s.size()), SQLITE_TRANSIENT); },
              [&](std::span<const std::byte> s) { return sqlite3_bind_blob(stmt, idx, s.data(), static_cast<int>(s.size()), SQLITE_TRANSIENT); }},
          p);
      if (status != SQLITE_OK) throw error(_db.get(), std::format("sqlite3_bind(${})", idx));
    }

    ++(*query);  // i.e. run initial sqlite3_step(...), which execute the query
    return connection::result(std::move(query));
  }

 private:
  [[nodiscard]] static std::runtime_error error(sqlite3 *db, std::string_view context) noexcept {
    return std::runtime_error(std::format("{}: {}", context, sqlite3_errmsg(db)));
  }
};
#endif /*JL_HAS_SQLITE*/

#ifdef JL_HAS_PSQL
#include <libpq-fe.h>
class psql final : public connection {
  std::unique_ptr<PGconn, jl::deleter<PQfinish>> _db;

 public:
  explicit psql(PGconn *db) : _db(db) {}
  ~psql() override = default;
  psql(const psql &) = delete;
  psql &operator=(const psql &) = delete;

  static std::unique_ptr<psql> open(const std::string &uri) {
    auto db = std::make_unique<psql>(PQconnectdb(uri.c_str()));
    if (db->_db == nullptr) throw jl::make_system_error(std::errc::not_enough_memory, "PQconnectdb({})", uri);
    if (PQstatus(db->_db.get()) != CONNECTION_OK) throw db->error("PQconnectdb({})");
    return db;
  };

  class result final : public connection::result::impl {
    std::unique_ptr<PGresult, jl::deleter<PQclear>> _result;
    std::vector<std::vector<std::byte>> _blobs;
    int _n = 0;
    int _row = 0;

   public:
    result() = default;
    explicit result(decltype(_result) result) : _result(std::move(result)), _blobs(ncolumn()), _n(PQntuples(_result.get())) {}

   protected:
    void operator++() override { ++_row; }
    bool operator==(connection::result::sentinel /*unused*/) override { return _row == _n; }

    int ncolumn() override { return PQnfields(_result.get()); }
    std::string_view column_name(int col) override { return PQfname(_result.get(), col); }
    bool isnull(int col) override { return PQgetisnull(_result.get(), _row, col); }
    int32_t i32(int col) override { return jl::unwrap(jl::from_str<int32_t>(str(col))); }
    int64_t i64(int col) override { return jl::unwrap(jl::from_str<int64_t>(str(col))); }
    double f64(int col) override { return jl::unwrap(jl::from_str<double>(str(col))); }
    std::string_view str(int col) override { return c_str(col); }
    std::span<const std::byte> blob(int col) override {
      auto s = std::string_view(c_str(col), static_cast<size_t>(PQgetlength(_result.get(), _row, col)));
      if (s.starts_with("\\x")) return _blobs[col] = jl::from_xdigits(s);
      return std::as_bytes(std::span(s));
    }

   private:
    [[nodiscard]] const char *c_str(int col) const { return PQgetvalue(_result.get(), _row, col); }
  };
  connection::result exec(const std::string &sql, std::span<const param> params) override {
    constexpr int values_as_text = 0;

    std::vector<const char *> c_strs(params.size());
    std::vector<int> sizes(params.size());
    std::vector<int> formats(params.size(), 0 /* i.e. text */);
    std::vector<std::string> tmps;
    tmps.reserve(params.size());
    for (const auto &[i, p] : std::views::enumerate(params)) {
      auto str = std::visit(
          jl::overload{
              [](std::monostate) { return std::string_view{}; },
              [&formats, i](std::string_view s) {
                formats[i] = 1; /* i.e. binary, otherwise it ignores size and requires it null-terminated */
                return s;
              },
              [](const std::string &s) { return std::string_view(s); },
              [&formats, i](std::span<const std::byte> s) {
                formats[i] = 1; /* i.e. binary */
                return std::string_view(reinterpret_cast<const char *>(s.data()), s.size());
              },
              [&tmps](auto v) { return std::string_view(tmps.emplace_back(std::format("{}", v))); }},
          p);
      c_strs[i] = str.data();
      sizes[i] = static_cast<int>(str.size());
    }

    std::unique_ptr<PGresult, jl::deleter<PQclear>> p(PQexecParams(
        _db.get(), sql.c_str(),
        static_cast<int>(params.size()), nullptr, c_strs.data(), sizes.data(), formats.data(),
        values_as_text));
    switch (PQresultStatus(p.get())) {
      case PGRES_EMPTY_QUERY:
        return connection::result(std::make_unique<result>(nullptr));
      case PGRES_COMMAND_OK:
      case PGRES_TUPLES_OK:
        return connection::result(std::make_unique<result>(std::move(p)));
      default:
        throw error("PQexec");
    }
  }

 private:
  [[nodiscard]] std::runtime_error error(std::string_view context) noexcept {
    return std::runtime_error(std::format("{}: {}", context, PQerrorMessage(_db.get())));
  }
};
#endif /*JL_HAS_PSQL*/

inline std::unique_ptr<connection> open(const std::string &uri) {
#ifdef JL_HAS_SQLITE
  if (uri.starts_with("file://")) return sqlite::open(uri);
#endif /*JL_HAS_SQLITE*/
#ifdef JL_HAS_PSQL
  if (uri.starts_with("postgres")) return psql::open(uri);
#endif /*JL_HAS_PSQL*/
  throw jl::make_system_error(std::errc::invalid_argument, "Unsupported DB: {}", uri);
}

}  // namespace jl::db
