#include <doctest/doctest.h>
#include <fcntl.h>
#include <jl_posix.h>

TEST_SUITE("unique_mmap") {
  TEST_CASE("move and assignment neither double free nor leaks") {
    // NOTE: Most leak sanitizers does not track mmaps
    jl::unique_mmap<char> org(4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS);
    jl::unique_mmap<char> move_constructed(std::move(org));
    jl::unique_mmap<char> move_assigned = std::move(move_constructed);

    move_assigned = jl::unique_mmap<char>(4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS);
  }

  TEST_CASE("mapped file") {
    jl::unique_fd tmp = jl::tmpfd().unlink();
    char n = 42;
    REQUIRE(1 == pwrite(*tmp, &n, 1, 4095));

    jl::unique_mmap<const char> map(4096, PROT_READ, MAP_SHARED, *tmp);
    CHECK(42 == map[4095]);
  }

  TEST_CASE("remap") {
    jl::unique_mmap<const char> map(4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS);
    jl::unwrap(map.remap(8192, MREMAP_MAYMOVE));
  }

  TEST_CASE("integer") {
    // NOTE: mmap would have returned EINVAL if  offset were not 4KiB page aligned
    jl::unique_mmap<int32_t> map(1024, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 1024);
    CHECK(4096 == map->size_bytes());
  }

#ifdef PR_SET_VMA
  TEST_CASE("named anonymous pages") {
    auto map = jl::unique_mmap<char>::anon(1 << 20, PROT_READ | PROT_WRITE, "NamedAnonymousPages");

    std::string smaps_path = "/proc/" + std::to_string(getpid()) + "/smaps";
    jl::unique_fd smaps_fd(open(smaps_path.c_str(), O_RDONLY | O_CLOEXEC));
    auto smaps = jl::unwrap(jl::readall(*smaps_fd, *map));
    CHECK(std::string::npos != jl::view_of(smaps).find("[anon:NamedAnonymousPages]"));
  }
#endif
}
