#include <gtest/gtest.h>
#include <jl.h>

TEST(TmpFD, MoveAndAssignmentDoesNotDoubleClose) {
  jl::tmpfd org;
  jl::tmpfd move_constructed(std::move(org));
  jl::tmpfd move_assigned = std::move(move_constructed);
}
