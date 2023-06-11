#include <gtest/gtest.h>
#include <jl.h>

static inline std::string_view as_view(std::span<char> data) {
  return {data.begin(), data.end()};
}

TEST(FdMMAP, Reading) {
  jl::unique_fd fd = jl::tmpfd().unlink();
  fd.write("foo");

  jl::fd_mmap<char> map(std::move(fd));

  EXPECT_EQ('f', map[0]);
  EXPECT_EQ("foo", as_view(*map));
}

TEST(FdMMAP, ReadWrite) {
  jl::unique_fd fd = jl::tmpfd().unlink();
  fd.write("foo");

  jl::fd_mmap<char> map(std::move(fd), PROT_READ | PROT_WRITE);
  std::string_view ba = "ba";
  std::copy(ba.begin(), ba.end(), map->begin());
  map[2] = 'r';

  EXPECT_EQ("bar", as_view(*map));
}

TEST(FdMMAP, AutomaticSizeTakesOffsetIntoAccount) {
  jl::unique_fd fd = jl::tmpfd().unlink();
  fd.truncate(4096);
  ASSERT_EQ(4096, lseek(fd.fd(), 4096, SEEK_SET));
  fd.write("foo");

  jl::fd_mmap<char> map(std::move(fd), PROT_READ, MAP_SHARED, 4096);

  EXPECT_EQ("foo", as_view(*map));
}

std::string sread(int fd, size_t length, off_t offset) {
  std::string str(length, '\0');
  if (auto read = pread(fd, str.data(), length, offset); read >= 0) {
    str.resize(read);
    return str;
  }
  throw jl::errno_as_error("pread failed");
}

TEST(FdMMAP, TruncateTakesOffsetIntoAccount) {
  jl::fd_mmap<char> map(jl::tmpfd().unlink(), PROT_READ | PROT_WRITE, MAP_SHARED, 4096);

  map.truncate(4096);
  EXPECT_EQ(0, map->size());
  map.truncate(4097);
  EXPECT_EQ(1, map->size());

  EXPECT_EQ(std::string_view("\0", 1), as_view(*map));
  map[0] = 'a';
  EXPECT_EQ("a", sread(map.fd(), 1, 4096));

  map.truncate(0);
  EXPECT_EQ(0, map->size());
}

TEST(FdMMAP, RemapDoesNotAffectFile) {
  jl::fd_mmap<char> map(jl::tmpfd().unlink(), PROT_READ);
  map.remap(10);
  EXPECT_EQ(10, map->size());

  struct stat buf {};
  EXPECT_EQ(0, fstat(map.fd(), &buf));
  EXPECT_EQ(0, buf.st_size);
}
