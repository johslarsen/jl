#include <gtest/gtest.h>
#include <jl.h>

static inline std::string_view as_view(std::span<char> data) {
  return {data.begin(), data.end()};
}

TEST(FdMMAP, Reading) {
  jl::tmpfd fd;
  fd.write("foo");

  jl::fd_mmap<char> map(std::move(fd));

  EXPECT_EQ('f', map[0]);
  EXPECT_EQ("foo", as_view(*map));
}

TEST(FdMMAP, ReadWrite) {
  jl::tmpfd fd;
  fd.write("foo");

  jl::fd_mmap<char> map(std::move(fd), PROT_READ | PROT_WRITE);
  std::string_view ba = "ba";
  std::copy(ba.begin(), ba.end(), (*map).begin());
  map[2] = 'r';

  EXPECT_EQ("bar", as_view(*map));
}

TEST(FdMMAP, AutomaticSizeTakesOffsetIntoAccount) {
  jl::tmpfd fd;
  ASSERT_EQ(0, ftruncate(fd.fd(), 4096));
  ASSERT_EQ(4096, lseek(fd.fd(), 4096, SEEK_SET));
  fd.write("foo");

  jl::fd_mmap<char> map(std::move(fd), PROT_READ, MAP_PRIVATE, 4096);

  EXPECT_EQ("foo", as_view(*map));
}
