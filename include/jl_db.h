#pragma once

#include <jl.h>

#include <functional>

namespace jl::db {

static constexpr auto null = std::monostate{};
using param = std::variant<std::monostate, int32_t, int64_t, double, std::string, std::string_view, const char *, std::span<const std::byte>>;

/// An engine agnostic cursor into a database result
class result {
 public:
  struct sentinel {};
  class iter {
    result *_result;

   public:
    using difference_type = ptrdiff_t;
    using value_type = result;

    constexpr explicit iter(result &result) : _result(&result) {}

    constexpr iter &operator++() {
      ++(*_result->_pimpl);
      return *this;
    }
    constexpr void operator++(int) { ++(*_result->_pimpl); }

    [[nodiscard]] constexpr result &operator*() const { return *_result; }
    [[nodiscard]] constexpr bool operator==(const sentinel &s) const { return (*_result->_pimpl) == s; }
  };
  static_assert(std::input_iterator<iter>);
  static_assert(std::sentinel_for<sentinel, iter>);

  [[nodiscard]] constexpr iter begin() { return iter(*this); }
  [[nodiscard]] constexpr sentinel end() const { return {}; }

  struct impl {
    constexpr virtual ~impl() = default;

    [[nodiscard]] constexpr virtual bool operator==(sentinel) const = 0;
    constexpr virtual void operator++() = 0;

    // NOTE: most of the following methods should probably be const, but
    // functions using sqlite3_stmt takes it as a non-const pointer

    [[nodiscard]] constexpr virtual int ncolumn() = 0;
    [[nodiscard]] constexpr virtual std::string_view column_name(int col) = 0;
    [[nodiscard]] constexpr virtual int column_idx(const std::string &name) = 0;
    [[nodiscard]] constexpr virtual bool isnull(int col) = 0;

    [[nodiscard]] constexpr virtual int32_t i32(int col) = 0;
    [[nodiscard]] constexpr virtual int64_t i64(int col) = 0;
    [[nodiscard]] constexpr virtual double f64(int col) = 0;
    [[nodiscard]] constexpr virtual std::string_view str(int col) = 0;
    [[nodiscard]] constexpr virtual std::span<const std::byte> blob(int col) = 0;

   protected:
    impl() = default;
    impl(const impl &) = default;
    impl &operator=(const impl &) = default;
    impl(impl &&) = default;
    impl &operator=(impl &&) = default;
  };
  constexpr explicit result(std::unique_ptr<impl> pimpl) : _pimpl(std::move(pimpl)) {}

  class field {
    impl &_result;
    int _col;

    [[nodiscard]] constexpr auto nullable(auto f) {
      return isnull() ? std::nullopt : std::optional(std::invoke(f, this));
    }

   public:
    constexpr field(impl &result, int col) : _result(result), _col(col) {}

    // accessors mappings mapping NULL-able values as optional:

    [[nodiscard]] constexpr std::optional<int32_t> i32() { return nullable(&field::raw_i32); }
    [[nodiscard]] constexpr std::optional<int64_t> i64() { return nullable(&field::raw_i64); }
    [[nodiscard]] constexpr std::optional<double> f64() { return nullable(&field::raw_f64); }
    [[nodiscard]] constexpr std::optional<std::string_view> str() { return nullable(&field::raw_str); }
    [[nodiscard]] constexpr std::optional<std::span<const std::byte>> blob() { return nullable(&field::raw_blob); }

    // direct value accessors for fields known to be NOT NULL:

    [[nodiscard]] constexpr int32_t raw_i32() { return _result.i32(_col); }
    [[nodiscard]] constexpr int64_t raw_i64() { return _result.i64(_col); }
    [[nodiscard]] constexpr double raw_f64() { return _result.f64(_col); }
    [[nodiscard]] constexpr std::string_view raw_str() { return _result.str(_col); }
    [[nodiscard]] constexpr std::span<const std::byte> raw_blob() { return _result.blob(_col); }

    [[nodiscard]] constexpr std::string_view name() { return _result.column_name(_col); }
    [[nodiscard]] constexpr bool isnull() { return _result.isnull(_col); }
  };
  /// Access the field in a given column of the current row
  [[nodiscard]] constexpr field operator[](int column) { return {*_pimpl, column}; };
  [[nodiscard]] constexpr field operator[](const std::string &name) { return (*this)[_pimpl->column_idx(name)]; }

  [[nodiscard]] constexpr int ncolumn() { return _pimpl->ncolumn(); }
  [[nodiscard]] constexpr bool empty() { return *_pimpl == end(); }

 private:
  std::unique_ptr<impl> _pimpl;
};
static_assert(std::ranges::input_range<result>);

/// An engine agnostic database wrapper around a database connection, basic usage:
///
///     auto db = jl::db::open("file:///db.sqlite3?mode=memory");
///     for (auto& row : db->exec("SELECT NULL;")) {
///       row[0].i32().value_or(42);
///     }
struct connection {
  virtual ~connection() = default;
  connection(const connection &) = delete;
  connection &operator=(const connection &) = delete;

  /// Execute a prepared SQL statement like:
  ///     exec("SELECT $1, $2", "foo", 42);
  constexpr result exec(const std::string &sql, auto... params) {
    return execv(sql, std::initializer_list<param>{params...});
  }
  /// WARN: make sure std::string_view, const char*, and std::span in params do not dangle
  constexpr virtual result execv(const std::string &sql, std::span<const param> params) = 0;

  connection() = default;
};

[[nodiscard]] constexpr inline int column_idx(const std::vector<std::string> &columns, std::string_view name) {
  if (auto i = std::ranges::find(columns, name); i != columns.end()) return static_cast<int>(i - columns.begin());
  throw std::runtime_error(std::format("missing {} column", name));
};

/// A database that mocks or intercepts results
class mock final : public connection {
  std::move_only_function<db::result(const std::string &, std::span<const param>)> _exec;

 public:
  constexpr explicit mock(decltype(_exec) exec) : _exec(std::move(exec)) {}
  ~mock() override = default;
  mock(const mock &) = delete;
  mock &operator=(const mock &) = delete;

  class result final : public db::result::impl {
    std::vector<std::vector<param>> _rows;
    std::vector<std::string> _column_names;
    size_t _i = 0;

    [[nodiscard]] constexpr const auto &at(size_t row, int col) {
      return _rows.at(row).at(col);
    }

   public:
    constexpr explicit result(std::vector<std::vector<param>> rows, std::vector<std::string> column_names = {})
        : _rows(std::move(rows)), _column_names(std::move(column_names)) {
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

    constexpr bool operator==(db::result::sentinel /*unused*/) const override {
      return _rows.size() == _i;
    }
    constexpr void operator++() override {
      assert(_i < _rows.size());
      ++_i;
    }

    constexpr int ncolumn() override {
      return static_cast<int>(_rows.at(0).size());
    }
    constexpr std::string_view column_name(int col) override { return _column_names.at(col); };
    constexpr int column_idx(const std::string &name) override { return db::column_idx(_column_names, name); }

    constexpr bool isnull(int col) override {
      return std::holds_alternative<std::monostate>(_rows.at(_i).at(col));
    }

    constexpr int32_t i32(int col) override { return std::get<int32_t>(at(_i, col)); }
    constexpr int64_t i64(int col) override { return std::get<int64_t>(at(_i, col)); }
    constexpr double f64(int col) override { return std::get<double>(at(_i, col)); }
    constexpr std::string_view str(int col) override {
      if (const auto *s = std::get_if<std::string>(&at(_i, col)); s) return *s;
      if (const auto *s = std::get_if<std::string_view>(&at(_i, col)); s) return *s;
      return std::get<const char *>(at(_i, col));
    }
    constexpr std::span<const std::byte> blob(int col) override {
      return std::get<std::span<const std::byte>>(at(_i, col));
    }
  };
  /// A fixed table result. Note that getters here are more strictly type checked than regular database APIs.
  [[nodiscard]] constexpr static db::result table(std::vector<std::vector<param>> rows, std::vector<std::string> column_names = {}) {
    return db::result(std::make_unique<result>(std::move(rows), std::move(column_names)));
  }

  constexpr db::result execv(const std::string &sql, std::span<const param> params) override {
    return _exec(sql, params);
  }
};

#ifdef JL_HAS_SQLITE
#include <sqlite3.h>
/// A wrapper around the SQLite3 C library
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
    if (db->_db == nullptr) throw jl::error(std::errc::not_enough_memory, "sqlite3_open({})", uri);
    if (status != SQLITE_OK) throw error(db->_db.get(), std::format("sqlite3_open({})", uri));

    return db;
  };

  class result final : public db::result::impl {
    std::unique_ptr<sqlite3_stmt, jl::deleter<sqlite3_finalize>> _stmt;
    std::vector<std::string> _column_names;

   public:
    explicit result(sqlite3_stmt *stmt) : _stmt(stmt) {}

    void operator++() override {
      if (auto status = sqlite3_step(_stmt.get()); status == SQLITE_DONE) {
        _stmt.reset();
      } else if (status == SQLITE_ROW) {  // i.e. now _stmt points to a new row
      } else {
        throw error(sqlite3_db_handle(_stmt.get()), "sqlite3_step");
      }
    }

   protected:
    bool operator==(db::result::sentinel /*unused*/) const override {
      return _stmt == nullptr;
    }

    int ncolumn() override { return sqlite3_column_count(_stmt.get()); }
    std::string_view column_name(int col) override { return sqlite3_column_name(_stmt.get(), col); }
    int column_idx(const std::string &name) override {
      if (_column_names.empty()) {
        for (int i = 0; i < ncolumn(); ++i) _column_names.emplace_back(column_name(i));
      }
      return db::column_idx(_column_names, name);
    }
    bool isnull(int col) override {
      return sqlite3_column_type(_stmt.get(), col) == SQLITE_NULL;
    }

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

  db::result execv(const std::string &sql, std::span<const param> params) override {
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
    return db::result(std::move(query));
  }

 private:
  [[nodiscard]] static std::runtime_error error(sqlite3 *db, std::string_view context) noexcept {
    return std::runtime_error(std::format("{}: {}", context, sqlite3_errmsg(db)));
  }
};
#endif /*JL_HAS_SQLITE*/

#ifdef JL_HAS_PSQL
#include <libpq-fe.h>
/// A wrapper around the libpq PostgreSQL C library
class psql final : public connection {
  std::unique_ptr<PGconn, jl::deleter<PQfinish>> _db;

 public:
  explicit psql(PGconn *db) : _db(db) {}
  ~psql() override = default;
  psql(const psql &) = delete;
  psql &operator=(const psql &) = delete;

  static std::unique_ptr<psql> open(const std::string &uri) {
    auto db = std::make_unique<psql>(PQconnectdb(uri.c_str()));
    if (db->_db == nullptr) throw jl::error(std::errc::not_enough_memory, "PQconnectdb({})", uri);
    if (PQstatus(db->_db.get()) != CONNECTION_OK) throw db->error("PQconnectdb({})");
    return db;
  };

  class result final : public db::result::impl {
    std::unique_ptr<PGresult, jl::deleter<PQclear>> _result;
    std::vector<std::vector<std::byte>> _blobs;
    int _n = 0;
    int _row = 0;

   public:
    result() = default;
    explicit result(decltype(_result) result)
        : _result(std::move(result)), _blobs(ncolumn()), _n(PQntuples(_result.get())) {}

   protected:
    void operator++() override { ++_row; }
    bool operator==(db::result::sentinel /*unused*/) const override {
      return _row == _n;
    }

    int ncolumn() override { return PQnfields(_result.get()); }
    std::string_view column_name(int col) override { return PQfname(_result.get(), col); }
    int column_idx(const std::string &name) override {
      if (auto i = PQfnumber(_result.get(), name.c_str()); i >= 0) return i;
      throw std::runtime_error(std::format("missing {} column", name));
    }

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
  db::result execv(const std::string &sql, std::span<const param> params) override {
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
                formats[i] = 1;  // i.e. binary, otherwise PQexecParams ignores size and requires it null-terminated
                return s;
              },
              [](const std::string &s) { return std::string_view(s); },
              [&formats, i](std::span<const std::byte> s) {
                formats[i] = 1;  // i.e. binary
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
        return db::result(std::make_unique<result>(nullptr));
      case PGRES_COMMAND_OK:
      case PGRES_TUPLES_OK:
        return db::result(std::make_unique<result>(std::move(p)));
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

/// Connect to a database at uri using one of these schemes:
/// * SQLite3:    file://
/// * PostgreSQL: postgres:// , postrgresql://
inline std::unique_ptr<connection> open(const std::string &uri) {
#ifdef JL_HAS_SQLITE
  if (uri.starts_with("file://")) return sqlite::open(uri);
#endif /*JL_HAS_SQLITE*/
#ifdef JL_HAS_PSQL
  if (uri.starts_with("postgres")) return psql::open(uri);
#endif /*JL_HAS_PSQL*/
  throw jl::error(std::errc::invalid_argument, "Unsupported DB: {}", uri);
}

}  // namespace jl::db
