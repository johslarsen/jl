#include <doctest/doctest.h>
#include <jl_posix.h>

TEST_SUITE("unique_addr") {
  TEST_CASE("empty host indicates bind address") {
    jl::unique_addr ipvx("", "1234");
    jl::unique_addr ipv4("", "1234", AF_INET);
    jl::unique_addr ipv6("", "1234", AF_INET6);

    CHECK("0.0.0.0:1234" == jl::host_port::from(ipvx).string());
    CHECK("0.0.0.0:1234" == jl::host_port::from(ipv4).string());
    CHECK("[::]:1234" == jl::host_port::from(ipv6).string());
  }

  TEST_CASE("throws error if host is invalid") {
    CHECK_THROWS_AS(jl::unique_addr("invalid.host.example.com", "1234"), std::runtime_error);
    CHECK_THROWS_AS(jl::unique_addr("1.2.3.4.5", "1234"), std::runtime_error);
    CHECK_THROWS_AS(jl::unique_addr("0.0.0.0", "invalid-port"), std::runtime_error);
  }
}
