#include <gtest/gtest.h>
#include <jl.h>

TEST(Bit, EndianConversionHasNoEffectOn1ByteIntegers) {
  EXPECT_EQ(jl::be('0'), jl::le('0'));
  EXPECT_EQ(jl::be(static_cast<uint8_t>(0xac)), jl::le(static_cast<uint8_t>(0xac)));
}

TEST(Bit, IntegerBigAndLittleEndianSwapsOnTheWrongArchitecture) {
  if constexpr (std::endian::native == std::endian::big) {
    EXPECT_EQ(0x12345678, jl::be(0x12345678));
    EXPECT_EQ(0x78563412, jl::le(0x12345678));
  } else if constexpr (std::endian::native == std::endian::little) {
    EXPECT_EQ(0x12345678, jl::le(0x12345678));
    EXPECT_EQ(0x78563412, jl::be(0x12345678));
  } else {
    GTEST_SKIP() << "jl::be/le does not work on architectures with obscure endianness";
  }

  // And test that it they are implemented for all the integer types
  EXPECT_EQ(std::byteswap(jl::be(static_cast<int16_t>(0x1122))), jl::le(static_cast<int16_t>(0x1122)));
  EXPECT_EQ(std::byteswap(jl::be(static_cast<uint16_t>(0x1122))), jl::le(static_cast<uint16_t>(0x1122)));
  EXPECT_EQ(std::byteswap(jl::be(0x11223344)), jl::le(0x11223344));
  EXPECT_EQ(std::byteswap(jl::be(0x11223344U)), jl::le(0x11223344U));
  EXPECT_EQ(std::byteswap(jl::be(0x1122334455667788L)), jl::le(0x1122334455667788L));
  EXPECT_EQ(std::byteswap(jl::be(0x1122334455667788UL)), jl::le(0x1122334455667788UL));
  EXPECT_EQ(std::byteswap(jl::be(0x1122334455667788LL)), jl::le(0x1122334455667788LL));
  EXPECT_EQ(std::byteswap(jl::be(0x1122334455667788ULL)), jl::le(0x1122334455667788ULL));
  EXPECT_EQ(std::byteswap(jl::be(0x1122334455667788Z)), jl::le(0x1122334455667788Z));
  EXPECT_EQ(std::byteswap(jl::be(0x1122334455667788UZ)), jl::le(0x1122334455667788UZ));
}
