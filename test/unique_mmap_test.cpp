#include <gtest/gtest.h>
#include <jl.h>

TEST(UniqueMMAP, MoveAndAssignmentDoesNotDoubleClose) {
  jl::unique_mmap<char> org(4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS);
  jl::unique_mmap<char> move_constructed(std::move(org));
  jl::unique_mmap<char> move_assigned = std::move(move_constructed);
}

TEST(UniqueMMAP, MappedFile) {
  jl::tmpfd tmp;
  char n = 42;
  ASSERT_EQ(1, pwrite(*tmp, &n, 1, 4095));

  jl::unique_mmap<char> map(4096, PROT_READ, MAP_SHARED, *tmp);
  EXPECT_EQ(42, map[4095]);
}

TEST(UniqueMMAP, Remap) {
  jl::unique_mmap<char> map(4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS);
  map.remap(8192, MREMAP_MAYMOVE);
}

TEST(UniqueMMAP, Integer) {
  // NOTE: mmap would have returned EINVAL if  offset were not 4KiB page aligned
  jl::unique_mmap<int32_t> map(1024, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 1024);
  EXPECT_EQ(4096, map->size_bytes());
}
