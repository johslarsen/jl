#include <fcntl.h>
#include <gtest/gtest.h>
#include <jl.h>

TEST(UniqueMMAP, MoveAndAssignmentNeitherDoubleFreeNorLeaks) {
  // NOTE: Most leak sanitizers does not track mmaps
  jl::unique_mmap<char> org(4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS);
  jl::unique_mmap<char> move_constructed(std::move(org));
  jl::unique_mmap<char> move_assigned = std::move(move_constructed);

  move_assigned = jl::unique_mmap<char>(4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS);
}

TEST(UniqueMMAP, MappedFile) {
  jl::unique_fd tmp = jl::tmpfd().unlink();
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

TEST(UniqueMMAP, NamedAnonymousPages) {
  auto map = jl::unique_mmap<char>::anon(1 << 20, PROT_READ | PROT_WRITE, "NamedAnonymousPages");

  std::string smaps_path = "/proc/" + std::to_string(getpid()) + "/smaps";
  jl::unique_fd smaps_fd(open(smaps_path.c_str(), O_RDONLY | O_CLOEXEC));
  auto smaps = jl::readall(*smaps_fd, *map);
  EXPECT_NE(std::string::npos, std::string_view(smaps.begin(), smaps.end()).find("[anon:NamedAnonymousPages]"));
}
