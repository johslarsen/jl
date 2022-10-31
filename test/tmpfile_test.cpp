#include <gtest/gtest.h>
#include <jl.h>

TEST(UniqueFD, MoveAndAssignmentDoesNotDoubleClose) {
  jl::tmpfile org;
  jl::tmpfile move_constructed(std::move(org));
  jl::tmpfile move_assigned = std::move(move_constructed);
}
