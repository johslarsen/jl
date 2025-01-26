#include <doctest/doctest.h>
#include <jl.h>

#include <numbers>

constexpr bool is_mixed_endian = std::endian::native != std::endian::little &&
                                 std::endian::native != std::endian::big;

static inline std::vector<std::byte> bvec(auto... bytes) {
  return {std::byte(bytes)...};
};

TEST_SUITE("bit" * doctest::skip(is_mixed_endian)) {
  TEST_CASE("endian conversion has no effect on 1byte integers") {
    CHECK(jl::be('0') == jl::le('0'));
    CHECK(jl::be(static_cast<uint8_t>(0xac)) == jl::le(static_cast<uint8_t>(0xac)));
  }

  TEST_CASE("integer big and little endian swaps on the wrong architecture") {
    if constexpr (std::endian::native == std::endian::big) {
      CHECK(0x12345678 == jl::be(0x12345678));
      CHECK(0x78563412 == jl::le(0x12345678));
    } else if constexpr (std::endian::native == std::endian::little) {
      CHECK(0x12345678 == jl::le(0x12345678));
      CHECK(0x78563412 == jl::be(0x12345678));
    }

    // And test that it they are implemented for all the integer types
    CHECK(std::byteswap(jl::be(static_cast<int16_t>(0x1122))) == jl::le(static_cast<int16_t>(0x1122)));
    CHECK(std::byteswap(jl::be(static_cast<uint16_t>(0x1122))) == jl::le(static_cast<uint16_t>(0x1122)));
    CHECK(std::byteswap(jl::be(0x11223344)) == jl::le(0x11223344));
    CHECK(std::byteswap(jl::be(0x11223344U)) == jl::le(0x11223344U));
    CHECK(std::byteswap(jl::be(0x1122334455667788L)) == jl::le(0x1122334455667788L));
    CHECK(std::byteswap(jl::be(0x1122334455667788UL)) == jl::le(0x1122334455667788UL));
    CHECK(std::byteswap(jl::be(0x1122334455667788LL)) == jl::le(0x1122334455667788LL));
    CHECK(std::byteswap(jl::be(0x1122334455667788ULL)) == jl::le(0x1122334455667788ULL));
    CHECK(std::byteswap(jl::be(0x1122334455667788Z)) == jl::le(0x1122334455667788Z));
    CHECK(std::byteswap(jl::be(0x1122334455667788UZ)) == jl::le(0x1122334455667788UZ));
  }

  TEST_CASE("floating point big and little endian swaps on the wrong architecture") {
    if constexpr (std::endian::native == std::endian::big) {
      CHECK(std::numbers::pi == jl::be(std::numbers::pi));
      CHECK(std::numbers::pi != jl::le(std::numbers::pi));
      CHECK(std::numbers::pi_v<float> == jl::be(std::numbers::pi_v<float>));
      CHECK(std::numbers::pi_v<float> != jl::le(std::numbers::pi_v<float>));
    } else if constexpr (std::endian::native == std::endian::little) {
      CHECK(std::numbers::pi == jl::le(std::numbers::pi));
      CHECK(std::numbers::pi != jl::be(std::numbers::pi));
      CHECK(std::numbers::pi_v<float> == jl::le(std::numbers::pi_v<float>));
      CHECK(std::numbers::pi_v<float> != jl::be(std::numbers::pi_v<float>));
    }
  }

  TEST_CASE("uint_from_size") {
    static_assert(std::is_same_v<jl::uint_from_size<sizeof(int8_t)>::type, uint8_t>);
    static_assert(std::is_same_v<jl::uint_from_size<sizeof(int16_t)>::type, uint16_t>);
    static_assert(std::is_same_v<jl::uint_from_size<sizeof(int32_t)>::type, uint32_t>);
    static_assert(std::is_same_v<jl::uint_from_size<sizeof(int64_t)>::type, uint64_t>);
  }

  TEST_CASE("bitcastable_to") {
    static_assert(jl::bitcastable_to<uint8_t, std::byte>);
    static_assert(jl::bitcastable_to<uint8_t, int8_t>);
    static_assert(jl::bitcastable_to<uint8_t, char>);
    static_assert(jl::bitcastable_to<uint8_t, signed char>);
    static_assert(jl::bitcastable_to<uint8_t, unsigned char>);
    static_assert(!jl::bitcastable_to<char, uint16_t>);
  }

  TEST_CASE("from_xdigits") {
    CHECK(jl::from_xdigits("") == std::vector<std::byte>{});
    CHECK(jl::from_xdigits("\\x") == std::vector<std::byte>{});

    CHECK(jl::from_xdigits("\\X0123456789abcdef") == bvec(0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef));
    CHECK(jl::from_xdigits("0xFEDCBA987654321") == bvec(0x0F, 0xED, 0xCB, 0xA9, 0x87, 0x65, 0x43, 0x21));
  }

  TEST_CASE("to_xdigits") {
    CHECK(jl::to_xdigits({}) == "");

    CHECK(jl::to_xdigits(bvec(0xde, 0xad, 0xbe, 0xef), "", "0x") == "0xdeadbeef");
    CHECK(jl::to_xdigits(bvec(0xde, 0xad, 0xbe, 0xef), " ") == "de ad be ef");
  }
}
