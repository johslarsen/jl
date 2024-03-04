#include <doctest/doctest.h>
#include <jl.h>

TEST_SUITE("fd_mmap") {
  TEST_CASE("basic") {
    jl::unique_fd fd = jl::tmpfd().unlink();
    CHECK(3 == jl::write(*fd, "foo"));

    SUBCASE("read") {
      jl::fd_mmap<char> map(std::move(fd));
      CHECK('f' == map[0]);
      CHECK("foo" == jl::view_of(*map));
    }
    SUBCASE("write") {
      jl::fd_mmap<char> map(std::move(fd), PROT_READ | PROT_WRITE);
      std::string_view ba = "ba";
      std::copy(ba.begin(), ba.end(), map->begin());
      map[2] = 'r';

      CHECK("bar" == jl::view_of(*map));
    }
  }

  TEST_CASE("automatic size takes offset into account") {
    jl::unique_fd fd = jl::tmpfd().unlink();
    jl::truncate(*fd, 4096);
    CHECK(3 == pwrite(*fd, "foo", 3, 4096));

    jl::fd_mmap<char> map(std::move(fd), PROT_READ, MAP_SHARED, 4096);

    CHECK("foo" == jl::view_of(*map));
  }

  TEST_CASE("mapping beyond EOF is usable as is after file grows") {
    jl::fd_mmap<const char> map(jl::tmpfd().unlink(), PROT_READ, MAP_SHARED, 0, 3);
    CHECK(0 == jl::stat(map.fd()).st_size);
    CHECK(3 == jl::write(map.fd(), "foo"));
    CHECK("foo" == jl::view_of(*map));
  }

  std::string sread(int fd, size_t length, off_t offset) {
    std::string str(length, '\0');
    if (auto read = pread(fd, str.data(), length, offset); read >= 0) {
      str.resize(read);
      return str;
    }
    throw jl::errno_as_error("pread failed");
  }

  TEST_CASE("truncate takes offset into account") {
    jl::fd_mmap<char> map(jl::tmpfd().unlink(), PROT_READ | PROT_WRITE, MAP_SHARED, 4096);

    map.truncate(4096);
    CHECK(0 == map->size());
    map.truncate(4097);
    CHECK(1 == map->size());

    CHECK(std::string_view("\0", 1) == jl::view_of(*map));
    map[0] = 'a';
    CHECK("a" == sread(map.fd(), 1, 4096));

    map.truncate(0);
    CHECK(0 == map->size());
  }

  TEST_CASE("remap does not affect file") {
    jl::fd_mmap<const char> map(jl::tmpfd().unlink(), PROT_READ);
    map.remap(10);
    CHECK(10 == map->size());

    struct stat buf {};
    CHECK(0 == fstat(map.fd(), &buf));
    CHECK(0 == buf.st_size);
  }

  TEST_CASE("file descriptor is usable after unmap") {
    jl::fd_mmap<char> map(jl::tmpfd().unlink(), PROT_WRITE);
    map.truncate(3);

    map[0] = 'f';
    map[1] = 'o';
    map[2] = 'o';

    auto fd = std::move(map).unmap();
    std::string buf(3, '\0');

    CHECK("foo" == jl::read(*fd, buf));
  }
}
