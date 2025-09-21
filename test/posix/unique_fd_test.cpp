#include <doctest/doctest.h>
#include <jl_posix.h>

#include <filesystem>

TEST_SUITE("unique_fd") {
  TEST_CASE("move and assignment neither double close nor leaks") {
    std::string filename("/tmp/unique_fd_XXXXXX");
    auto org = jl::unwrap(jl::unique_fd::from(mkstemp(filename.data())));
    std::filesystem::remove(filename);

    CHECK(3 == jl::write(*org, "foo"));
    jl::unique_fd move_constructed(std::move(org));
    CHECK(3 == jl::write(*move_constructed, "bar"));
    jl::unique_fd move_assigned = std::move(move_constructed);
    CHECK(3 == jl::write(*move_assigned, "baz"));

    move_assigned = jl::unwrap(jl::unique_fd::from(fcntl(1, F_DUPFD_CLOEXEC, 0)));
  }

  TEST_CASE("open") {
    CHECK(jl::unwrap(jl::unique_fd::open("/dev/null", O_WRONLY)).fd() >= 0);
    CHECK("open(/DO_NOT_CREATE, 0x0)" == jl::unique_fd::open("/DO_NOT_CREATE", O_RDONLY).error().msg());
  }

  TEST_CASE("pipes") {
    auto [in, out] = jl::unwrap(jl::unique_fd::pipes());

    SUBCASE("basic") {
      CHECK(3 == jl::write(*out, std::string_view("foo")));

      std::string buffer = "xxxx";
      CHECK("foo" == jl::read(*in, buffer));
    }

    SUBCASE("spliceall with pipes") {
      auto [from, to] = jl::unwrap(jl::unique_fd::pipes());

      REQUIRE(3 == jl::write(*to, "foo"));
      CHECK(3 == jl::unwrap(jl::spliceall({*from}, {*out}, 3)));

      std::string buffer("???");
      CHECK("foo" == jl::read(*in, buffer));
    }

    SUBCASE("spliceall with file") {
      auto fd = jl::unwrap(jl::tmpfd::unlinked());

      CHECK(0 == jl::unwrap(jl::spliceall({*fd, 0}, {*out}, 3)));

      REQUIRE(3 == jl::write(*out, "foo"));
      CHECK(3 == jl::unwrap(jl::spliceall({*in}, {*fd, 0}, 3)));
      CHECK_MESSAGE(0 == jl::unwrap(jl::ok_or_errno(lseek(*fd, 0, SEEK_CUR))),
                    "splice at a specific offset was not supposed to change the fd position");

      CHECK(3 == jl::unwrap(jl::spliceall({*fd}, {*out}, 3)));
      CHECK_MESSAGE(3 == jl::unwrap(jl::ok_or_errno(lseek(*fd, 0, SEEK_CUR))),
                    "splice from/to the fd current position was supposed to change fd position");

      std::string buffer = "???";
      CHECK("foo" == jl::read(*in, buffer));
    }

    SUBCASE("sendfile") {
      auto fd = jl::unwrap(jl::tmpfd::unlinked());

      CHECK(0 == jl::unwrap(jl::sendfileall(*out, {*fd, 0}, 3)));

      REQUIRE(3 == jl::write(*fd, "foo"));
      std::ignore = jl::unwrap(jl::ok_or_errno(lseek(*fd, 0, SEEK_SET)));
      CHECK(3 == jl::unwrap(jl::sendfileall(*out, {*fd, 0}, 3)));
      CHECK_MESSAGE(0 == jl::unwrap(jl::ok_or_errno(lseek(*fd, 0, SEEK_CUR))),
                    "sendfile at a specific offset was not supposed to change the fd position");

      CHECK(3 == jl::unwrap(jl::sendfileall(*out, {*fd}, 3)));
      CHECK_MESSAGE(3 == jl::unwrap(jl::ok_or_errno(lseek(*fd, 0, SEEK_CUR))),
                    "sendfile from/to the fd current position was supposed to change fd position");

      std::string buffer = "??????";
      CHECK("foofoo" == jl::read(*in, buffer));
    }
  }
}

TEST_SUITE("tmp_fd") {
  TEST_CASE("read and write works with various inputs") {
    auto fd = jl::unwrap(jl::tmpfd::unlinked());
    std::vector<char> char_vector = {'f', 'o', 'o'};
    std::string string = "bar";
    std::vector<int> int_vector = {1, 2, 3};

    CHECK(3 == jl::write(*fd, std::span<char>(char_vector)));
    CHECK(3 == jl::write(*fd, string));
    CHECK(3 == jl::write(*fd, int_vector));

    CHECK(0 == jl::unwrap(jl::ok_or_errno(lseek(*fd, 0, SEEK_SET))));
    auto foo = jl::read(*fd, char_vector);
    CHECK("foo" == jl::view_of(foo));
    CHECK("bar" == jl::read(*fd, string));
    auto int123 = jl::read(*fd, std::span<int>(int_vector));
    CHECK((std::vector<int>{1, 2, 3}) == std::vector<int>(int123.begin(), int123.end()));
  }

  TEST_CASE("move and assignment neither double close nor leaks") {
    auto org = jl::unwrap(jl::tmpfd::open());
    jl::tmpfd move_constructed(std::move(org));
    jl::tmpfd move_assigned = std::move(move_constructed);
    move_assigned = jl::unwrap(jl::tmpfd::open());
  }
}

TEST_SUITE("iovecs") {
  TEST_CASE("as_iovecs") {
    std::vector<std::string> strings{"foo", "barbaz"};
    auto iovecs = jl::as_iovecs(strings);
    auto spans = jl::as_spans<const char>(iovecs);
    REQUIRE(2 == spans.size());
    CHECK("foo" == jl::view_of(spans[0]));
    CHECK("barbaz" == jl::view_of(spans[1]));
  }

  TEST_CASE("copy") {
    std::vector<std::string> strings = {"foobar", "baz"};
    auto iovecs = jl::as_iovecs(strings);
    std::string dest = "0123456789";

    auto fo = jl::copy(strings, std::span(dest).subspan(0, 2));
    CHECK("fo" == jl::view_of(fo));
    CHECK("fo23456789" == dest);

    auto foobar = jl::copy(iovecs, std::span(dest).subspan(0, 6));
    CHECK("foobar" == jl::view_of(foobar));

    auto foobarba = jl::copy(jl::as_spans<const char>(iovecs), std::span(dest).subspan(0, 8));
    CHECK("foobarba" == jl::view_of(foobarba));

    auto whole_input = jl::copy(strings, std::span(dest));
    CHECK("foobarbaz" == jl::view_of(whole_input));
    CHECK("foobarbaz9" == dest);
  }
}
