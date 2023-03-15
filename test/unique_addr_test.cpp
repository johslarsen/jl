#include <gtest/gtest.h>
#include <jl.h>

TEST(UniqueAddr, EmptyHostIndicatesBindAddress) {
  jl::unique_addr ipvx("", "1234");
  jl::unique_addr ipv4("", "1234", AF_INET);
  jl::unique_addr ipv6("", "1234", AF_INET6);

  EXPECT_EQ((jl::host_port{"0.0.0.0", 1234}), jl::host_port::from(ipvx));
  EXPECT_EQ((jl::host_port{"0.0.0.0", 1234}), jl::host_port::from(ipv4));
  EXPECT_EQ((jl::host_port{"::", 1234}), jl::host_port::from(ipv6));
}

TEST(UniqueAddr, ThrowsErrorIfHostIsInvalid) {
  EXPECT_THROW(jl::unique_addr("foo.example.com", "1234"), std::runtime_error);
  EXPECT_THROW(jl::unique_addr("1.2.3.4.5", "1234"), std::runtime_error);
}
