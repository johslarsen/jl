#include <doctest/doctest.h>
#include <jl.h>
#include <sys/fcntl.h>

#include <filesystem>

TEST_SUITE("unique_fd") {
  TEST_CASE("move and assignment neither double close nor leaks") {
    std::string filename("/tmp/unique_fd_XXXXXX");
    jl::unique_fd org(mkstemp(filename.data()));
    std::filesystem::remove(filename);

    CHECK(3 == jl::write(*org, "foo"));
    jl::unique_fd move_constructed(std::move(org));
    CHECK(3 == jl::write(*move_constructed, "bar"));
    jl::unique_fd move_assigned = std::move(move_constructed);
    CHECK(3 == jl::write(*move_assigned, "baz"));

    move_assigned = jl::unique_fd(fcntl(1, F_DUPFD_CLOEXEC, 0));
  }

  TEST_CASE("construction from invalid fdthrows") {
    CHECK_THROWS_AS(jl::unique_fd(-1, "foo"), std::system_error);
  }

  TEST_CASE("pipes") {
    auto [in, out] = jl::unique_fd::pipes();

    SUBCASE("basic") {
      CHECK(3 == jl::write(*out, std::string_view("foo")));

      std::string buffer = "xxxx";
      CHECK("foo" == jl::read(*in, buffer));
    }

    SUBCASE("spliceall with pipes") {
      auto [from, to] = jl::unique_fd::pipes();

      REQUIRE(3 == jl::write(*to, "foo"));
      CHECK(3 == jl::spliceall({*from}, {*out}, 3));

      std::string buffer("???");
      CHECK("foo" == jl::read(*in, buffer));
    }

    SUBCASE("spliceall with file") {
      auto fd = jl::tmpfd().unlink();

      CHECK(0 == jl::spliceall({*fd, 0}, {*out}, 3));

      REQUIRE(3 == jl::write(*out, "foo"));
      CHECK(3 == jl::spliceall({*in}, {*fd, 0}, 3));
      CHECK_MESSAGE(0 == jl::check_rw_error(lseek(*fd, 0, SEEK_CUR), "lseek failed"),
                    "splice at a specific offset was not supposed to change the fd position");

      CHECK(3 == jl::spliceall({*fd}, {*out}, 3));
      CHECK_MESSAGE(3 == jl::check_rw_error(lseek(*fd, 0, SEEK_CUR), "lseek failed"),
                    "splice from/to the fd current position was supposed to change fd position");

      std::string buffer = "???";
      CHECK("foo" == jl::read(*in, buffer));
    }

    SUBCASE("sendfile") {
      auto fd = jl::tmpfd().unlink();

      CHECK(0 == jl::sendfileall(*out, {*fd, 0}, 3));

      REQUIRE(3 == jl::write(*fd, "foo"));
      jl::check_rw_error(lseek(*fd, 0, SEEK_SET), "lseek failed");
      CHECK(3 == jl::sendfileall(*out, {*fd, 0}, 3));
      CHECK_MESSAGE(0 == jl::check_rw_error(lseek(*fd, 0, SEEK_CUR), "lseek failed"),
                    "sendfile at a specific offset was not supposed to change the fd position");

      CHECK(3 == jl::sendfileall(*out, {*fd}, 3));
      CHECK_MESSAGE(3 == jl::check_rw_error(lseek(*fd, 0, SEEK_CUR), "lseek failed"),
                    "sendfile from/to the fd current position was supposed to change fd position");

      std::string buffer = "??????";
      CHECK("foofoo" == jl::read(*in, buffer));
    }
  }
}

TEST_SUITE("tmp_fd") {
  TEST_CASE("read and write works with various inputs") {
    jl::unique_fd fd = jl::tmpfd().unlink();
    std::vector<char> char_vector = {'f', 'o', 'o'};
    std::string string = "bar";
    std::vector<int> int_vector = {1, 2, 3};

    CHECK(3 == jl::write(*fd, std::span<char>(char_vector)));
    CHECK(3 == jl::write(*fd, string));
    CHECK(3 == jl::write(*fd, int_vector));

    CHECK(0 == jl::check_rw_error(lseek(*fd, 0, SEEK_SET), "lseek failed"));
    auto foo = jl::read(*fd, char_vector);
    CHECK("foo" == std::string_view(foo.begin(), foo.end()));
    CHECK("bar" == jl::read(*fd, string));
    auto int123 = jl::read(*fd, std::span<int>(int_vector));
    CHECK((std::vector<int>{1, 2, 3}) == std::vector<int>(int123.begin(), int123.end()));
  }

  TEST_CASE("move and assignment neither double close nor leaks") {
    jl::tmpfd org;
    jl::tmpfd move_constructed(std::move(org));
    jl::tmpfd move_assigned = std::move(move_constructed);
    move_assigned = jl::tmpfd();
  }
}
