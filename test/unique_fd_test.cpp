#include <gtest/gtest.h>
#include <jl.h>
#include <sys/fcntl.h>

#include <filesystem>

TEST(UniqueFD, MoveAndAssignmentNeitherDoubleCloseNorLeaks) {
  std::string filename("/tmp/unique_fd_XXXXXX");
  jl::unique_fd org(mkstemp(filename.data()));
  std::filesystem::remove(filename);

  EXPECT_EQ(3, org.write("foo"));
  jl::unique_fd move_constructed(std::move(org));
  EXPECT_EQ(3, move_constructed.write("bar"));
  jl::unique_fd move_assigned = std::move(move_constructed);
  EXPECT_EQ(3, move_assigned.write("baz"));

  move_assigned = jl::unique_fd(fcntl(1, F_DUPFD_CLOEXEC, 0));
}

TEST(UniqueFD, ConstructionFromInvalidFDThrows) {
  EXPECT_THROW(jl::unique_fd(-1, "foo"), std::system_error);
}

TEST(UniqueFD, Pipes) {
  auto [in, out] = jl::unique_fd::pipes();

  EXPECT_EQ(3, out.write(std::string_view("foo")));

  std::string buffer = "xxxx";
  EXPECT_EQ("foo", in.read(buffer));
}

TEST(TmpFD, MoveAndAssignmentNeitherDoubleCloseNorLeaks) {
  jl::tmpfd org;
  jl::tmpfd move_constructed(std::move(org));
  jl::tmpfd move_assigned = std::move(move_constructed);
  move_assigned = jl::tmpfd();
}

TEST(TmpFD, ReadAndWriteWorksWithVariousInputs) {
  jl::unique_fd fd = jl::tmpfd().unlink();
  std::vector<char> char_vector = {'f', 'o', 'o'};
  std::string string = "bar";
  std::vector<int> int_vector = {1, 2, 3};

  EXPECT_EQ(3, fd.write(std::span<char>(char_vector)));
  EXPECT_EQ(3, fd.write(string));
  EXPECT_EQ(3, fd.write(int_vector));

  EXPECT_EQ(0, jl::check_rw_error(lseek(*fd, 0, SEEK_SET), "lseek failed"));
  auto foo = fd.read(char_vector);
  EXPECT_EQ("foo", std::string_view(foo.begin(), foo.end()));
  EXPECT_EQ("bar", fd.read(string));
  auto int123 = fd.read(std::span<int>(int_vector));
  EXPECT_EQ((std::vector<int>{1, 2, 3}), std::vector<int>(int123.begin(), int123.end()));
}

TEST(FD, SpliceallWithPipes) {
  auto [from, out] = jl::unique_fd::pipes();
  auto [in, to] = jl::unique_fd::pipes();

  ASSERT_EQ(3, out.write("foo"));
  EXPECT_EQ(3, jl::spliceall({*from}, {*to}, 3));

  std::string buffer("???");
  EXPECT_EQ("foo", in.read(buffer));
}

TEST(FD, SpliceallWithFile) {
  auto [in, out] = jl::unique_fd::pipes();
  auto fd = jl::tmpfd().unlink();

  EXPECT_EQ(0, jl::spliceall({*fd, 0}, {*out}, 3));

  ASSERT_EQ(3, out.write("foo"));
  EXPECT_EQ(3, jl::spliceall({*in}, {*fd, 0}, 3));
  EXPECT_EQ(0, jl::check_rw_error(lseek(*fd, 0, SEEK_CUR), "lseek failed"))
      << "splice at a specific offset was not supposed to change the fd position";

  EXPECT_EQ(3, jl::spliceall({*fd}, {*out}, 3));
  EXPECT_EQ(3, jl::check_rw_error(lseek(*fd, 0, SEEK_CUR), "lseek failed"))
      << "splice from/to the fd current position was supposed to change fd position";

  std::string buffer = "???";
  EXPECT_EQ("foo", in.read(buffer));
}

TEST(FD, Sendfile) {
  auto [in, out] = jl::unique_fd::pipes();
  auto fd = jl::tmpfd().unlink();

  EXPECT_EQ(0, jl::sendfileall({*fd, 0}, *out, 3));

  ASSERT_EQ(3, fd.write("foo"));
  jl::check_rw_error(lseek(*fd, 0, SEEK_SET), "lseek failed");
  EXPECT_EQ(3, jl::sendfileall({*fd, 0}, *out, 3));
  EXPECT_EQ(0, jl::check_rw_error(lseek(*fd, 0, SEEK_CUR), "lseek failed"))
      << "sendfile at a specific offset was not supposed to change the fd position";

  EXPECT_EQ(3, jl::sendfileall({*fd}, *out, 3));
  EXPECT_EQ(3, jl::check_rw_error(lseek(*fd, 0, SEEK_CUR), "lseek failed"))
      << "sendfile from/to the fd current position was supposed to change fd position";

  std::string buffer = "??????";
  EXPECT_EQ("foofoo", in.read(buffer));
}
