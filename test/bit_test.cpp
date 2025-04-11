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
    static_assert(jl::be('0') == jl::le('0'));
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
    static_assert(std::byteswap(jl::be(static_cast<int16_t>(0x1122))) == jl::le(static_cast<int16_t>(0x1122)));
    static_assert(std::byteswap(jl::be(static_cast<uint16_t>(0x1122))) == jl::le(static_cast<uint16_t>(0x1122)));
    static_assert(std::byteswap(jl::be(0x11223344)) == jl::le(0x11223344));
    static_assert(std::byteswap(jl::be(0x11223344U)) == jl::le(0x11223344U));
    static_assert(std::byteswap(jl::be(0x1122334455667788L)) == jl::le(0x1122334455667788L));
    static_assert(std::byteswap(jl::be(0x1122334455667788UL)) == jl::le(0x1122334455667788UL));
    static_assert(std::byteswap(jl::be(0x1122334455667788LL)) == jl::le(0x1122334455667788LL));
    static_assert(std::byteswap(jl::be(0x1122334455667788ULL)) == jl::le(0x1122334455667788ULL));
    static_assert(std::byteswap(jl::be(0x1122334455667788Z)) == jl::le(0x1122334455667788Z));
    static_assert(std::byteswap(jl::be(0x1122334455667788UZ)) == jl::le(0x1122334455667788UZ));
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
    static_assert(jl::be(std::numbers::pi) != jl::le(std::numbers::pi));
  }

  TEST_CASE("copying from std::span<std::byte>") {
    constexpr std::array deadbeef{std::byte(0xde), std::byte(0xad), std::byte(0xbe), std::byte(0xef)};
    CHECK(jl::native<uint32_t>(std::span(deadbeef)) == jl::be(0xdeadbeef));
    CHECK(jl::native<uint16_t>(std::span(deadbeef).subspan<1, 2>()) == jl::be(static_cast<uint16_t>(0xadbe)));
    CHECK(jl::be<uint32_t>(std::span(deadbeef)) == 0xdeadbeef);
    CHECK(jl::le<uint32_t>(std::span(deadbeef)) == 0xefbeadde);
    static_assert(jl::native<uint32_t>(deadbeef) == jl::be(0xdeadbeef));

    // falling back to memcpy when object is too large for constant evaluation:
    std::ignore = jl::native<std::array<int16_t, 500>>(std::array<std::byte, 1000>{});
  }

  TEST_CASE("extract bits from ints") {
    static_assert(jl::bits<uint16_t, 4, 8>(static_cast<uint16_t>(0xfaaf)) == 0xaa);
    static_assert(jl::bits<uint16_t, 12, 8>(static_cast<uint32_t>(0xfffaafff)) == 0xaa);

    static_assert(jl::bits<uint64_t, 0, 64>(static_cast<uint64_t>(0x8000'0000'0000'0001U)) == 0x8000'0000'0000'0001U);
    static_assert(jl::bits<uint16_t, 0, 0>(static_cast<uint16_t>(0xffff)) == 0);
    static_assert(jl::bits<uint16_t, 0, 1>(static_cast<uint16_t>(0x8000)) == 0x1);
    static_assert(jl::bits<uint16_t, 15, 1>(static_cast<uint16_t>(0x0001)) == 0x1);

    // proper sign extension:
    static_assert(jl::bits<int16_t, 0, 16>(static_cast<uint32_t>(0x00020000)) == 2);
    static_assert(jl::bits<int16_t, 14, 16>(static_cast<uint32_t>(0x00020000)) == INT16_MIN);
    static_assert(jl::bits<int16_t, 14, 2>(static_cast<uint32_t>(0x00020000)) == -2);
    static_assert(jl::bits<int16_t, 13, 2>(static_cast<uint32_t>(0x00020000)) == 1);

    // static_assert(jl::bits<uint8_t, 0, 9>(-1)); // Count > sizeof(uint8_t)
    // static_assert(jl::bits<uint8_t, 57, 8>(static_cast<uint64_t>(-1))); // Count + Offset > sizeof(uint64_t)
    // static_assert(jl::bits<uint16_t, 0,0>(static_cast<uint8_t>(-1)); // sizeof(uint16_t) > sizeof(uint8_t)
  }

  TEST_CASE("extract bits from std::span<const byte>") {
    constexpr std::array deadbeef{std::byte(0xde), std::byte(0xad), std::byte(0xbe), std::byte(0xef)};
    static_assert(jl::be_bits<uint16_t, 12, 8>(std::span(deadbeef)) == 0xdb);
    CHECK(jl::le_bits<uint16_t, 12, 8>(std::span(deadbeef)) == 0xea);

    // jl::bits<uint16_t, 17, 16>(std::span(deadbeef)); // Count + Offset > Extent
    // jl::bits<uint16_t, 15, 16>(std::span(deadbeef)); // no uint16_t byte-aligned window covering the bits
  }

  TEST_CASE("uint_from_size") {
    static_assert(std::is_same_v<jl::uint_from_size<sizeof(int8_t)>::type, uint8_t>);
    static_assert(std::is_same_v<jl::uint_from_size<sizeof(int16_t)>::type, uint16_t>);
    static_assert(std::is_same_v<jl::uint_from_size<sizeof(int32_t)>::type, uint32_t>);
    static_assert(std::is_same_v<jl::uint_from_size<sizeof(int64_t)>::type, uint64_t>);
    static_assert(std::is_same_v<jl::uint_from_size<sizeof(jl::int128)>::type, jl::uint128>);
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

  TEST_CASE("bitswap") {
    static_assert(jl::bitswap(std::byte(0xa1)) == std::byte(0x85));
    static_assert(jl::bitswap(0xdeadbeef) == 0xf77db57b);
  }

  TEST_CASE("crc::compute produce the reference check sums for the standard 123456789 string") {
    // extra examples to test both reflected and not for 8/16/32/64-bit CRCs
    struct crc8_autosar : jl::crc<uint8_t, 0x2f, 0xff, false, 0xff> {};
    struct crc8_bluetooth : jl::crc<uint8_t, 0xa7, 0x00, true, 0x00> {};
    struct crc16_gsm : jl::crc<uint16_t, 0x1021, 0x0000, false, 0xffff> {};
    struct crc32_cksum : jl::crc<uint32_t, 0x04c1'1db7, 0x0, false, 0xffff'ffff> {};
    struct crc64_nvme : jl::crc<uint64_t, 0xad93'd235'94c9'35a9, 0x0, true, 0x0> {};
    struct crc64_we : jl::crc<uint64_t, 0x42f0'e1eb'a9ea'3693, 0xffff'ffff'ffff'ffff, false, 0xffff'ffff'ffff'ffff> {};

    constexpr std::array one_through_nine{
        std::byte('1'),
        std::byte('2'),
        std::byte('3'),
        std::byte('4'),
        std::byte('5'),
        std::byte('6'),
        std::byte('7'),
        std::byte('8'),
        std::byte('9'),
    };

    // check values found in https://reveng.sourceforge.io/crc-catalogue/
    CHECK(crc8_autosar::compute("123456789") == 0xdf);
    CHECK(crc8_bluetooth::compute("123456789") == 0x26);
    CHECK(crc16_gsm::compute("123456789") == 0xCE3C);
    CHECK(jl::crc16_ccitt::compute("123456789") == 0x2189);
    static_assert(jl::crc16_ccitt::compute(std::span(one_through_nine)) == 0x2189);
    CHECK(jl::crc32c::compute("123456789") == 0xe3069283);
    CHECK(crc32_cksum::compute("123456789") == 0x765e7680);
    CHECK(crc64_nvme::compute("123456789") == 0xe9c6d914c4b8d9ca);
    CHECK(crc64_we::compute("123456789") == 0x62ec59e3f1a4f00a);
  }
}
