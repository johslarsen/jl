#include <doctest/doctest.h>
#include <jl.h>

#include <numbers>

static_assert(std::is_same_v<jl::uint_from_size<sizeof(int8_t)>::type, uint8_t>);
static_assert(std::is_same_v<jl::uint_from_size<sizeof(int16_t)>::type, uint16_t>);
static_assert(std::is_same_v<jl::uint_from_size<sizeof(int32_t)>::type, uint32_t>);
static_assert(std::is_same_v<jl::uint_from_size<sizeof(int64_t)>::type, uint64_t>);

constexpr bool is_mixed_endian = std::endian::native != std::endian::little &&
                                 std::endian::native != std::endian::big;

TEST_SUITE("Bit" * doctest::skip(is_mixed_endian)) {
  TEST_CASE("EndianConversionHasNoEffectOn1ByteIntegers") {
    CHECK(jl::be('0') == jl::le('0'));
    CHECK(jl::be(static_cast<uint8_t>(0xac)) == jl::le(static_cast<uint8_t>(0xac)));
  }

  TEST_CASE("IntegerBigAndLittleEndianSwapsOnTheWrongArchitecture") {
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

  TEST_CASE("FloatingPointBigAndLittleEndianSwapsOnTheWrongArchitecture") {
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
}
