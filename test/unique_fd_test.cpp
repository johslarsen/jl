#include <gtest/gtest.h>
#include <jl.h>

#include <filesystem>

TEST(UniqueFD, MoveAndAssignmentDoesNotDoubleClose) {
  std::string filename("/tmp/unique_fd_XXXXXX");
  jl::unique_fd org(mkstemp(filename.data()));
  std::filesystem::remove(filename);

  jl::unique_fd move_constructed(std::move(org));
  jl::unique_fd move_assigned = std::move(move_constructed);
}

TEST(UniqueFD, ConstructionFromInvalidFDThrows) {
  EXPECT_THROW(jl::unique_fd(-1, "foo"), std::system_error);
}
