#include <gtest/gtest.h>
#include <jl.h>

TEST(RingBuffer, StaticAsserts) {
  // jl::RingBuffer<4 << 10, int> integer_overflow_would_be_ub;
  // jl::RingBuffer<(4 << 10) + 1> not_power_of_2_capacity;
  // jl::RingBuffer<1U<<15, uint16_t> index_type_is_too_small>;
}

template <typename T>
static inline size_t advance(T& buf, size_t max) {
  size_t written = 0;
  while (written < max) {
    auto writeable = buf.peek_back(max - written);
    if (writeable.empty()) return written;

    auto available = writeable.size();
    buf.commit_written(std::move(writeable));
    buf.commit_read(buf.peek_front(available));
    written += available;
  }
  return written;
}

template <typename T>
static inline size_t write_string(T& buf, const std::string& str) {
  auto writeable = buf.peek_back(str.size());
  size_t written = writeable.size();
  const auto* as_u8 = std::bit_cast<uint8_t*>(str.data());
  std::copy(as_u8, as_u8 + written, writeable.begin());
  buf.commit_written(std::move(writeable));
  return written;
}

TEST(RingBuffer, ReadWriteSpansAcrossWrapAround) {
  jl::RingBuffer<4 << 10> buf;
  EXPECT_EQ((4 << 10) - 1, advance(buf, (4 << 10) - 1));

  EXPECT_EQ(2, write_string(buf, "42"));
  auto readable = buf.peek_front(2);

  EXPECT_EQ("42", std::string(std::bit_cast<char*>(readable.data()), readable.size()));
}

TEST(RingBuffer, ReadWriteSpansAcrossIndexTypeOverflow) {
  jl::RingBuffer<4 << 10, uint16_t> buf;
  EXPECT_EQ(UINT16_MAX, advance(buf, UINT16_MAX));

  EXPECT_EQ(2, write_string(buf, "42"));
  auto readable = buf.peek_front(2);

  EXPECT_EQ("42", std::string(std::bit_cast<char*>(readable.data()), readable.size()));
}

TEST(RingBuffer, PeekBackClampedToFreeSpaceLeft) {
  jl::RingBuffer<4 << 10> buf;
  EXPECT_EQ(4 << 10, buf.peek_back(std::numeric_limits<size_t>::max()).size())
      << "Empty buffer have the full capacity available";

  buf.commit_written(buf.peek_back(4 << 10));
  EXPECT_EQ(0, buf.peek_back(std::numeric_limits<size_t>::max()).size())
      << "Full buffer have no capacity available";

  buf.commit_read(buf.peek_front(1));
  EXPECT_EQ(1, buf.peek_back(std::numeric_limits<size_t>::max()).size())
      << "When bytes have been read new space is available";
}

TEST(RingBuffer, PeekFrontClampedToUsedSpace) {
  jl::RingBuffer<4 << 10> buf;
  EXPECT_EQ(0, buf.peek_front(std::numeric_limits<size_t>::max()).size())
      << "Empty buffer have no bytes to read";

  buf.commit_written(buf.peek_back(16));
  EXPECT_EQ(16, buf.peek_front(std::numeric_limits<size_t>::max()).size())
      << "When bytes have been written those bytes can be read";

  buf.commit_read(buf.peek_front(8));
  EXPECT_EQ(8, buf.peek_front(std::numeric_limits<size_t>::max()).size())
      << "When some bytes have been read some are left";

  buf.commit_written(buf.peek_back((4 << 10) - 8));
  EXPECT_EQ(4 << 10, buf.peek_front(std::numeric_limits<size_t>::max()).size())
      << "When the buffer is full the whole capacity is readable";
}

TEST(RingBuffer, PushBackAndPopFront) {
  jl::RingBuffer<4 << 10> buf;

  EXPECT_EQ(std::vector<uint8_t>(), buf.pop_front(1))
      << "No bytes to read from empty buffer";

  std::vector<uint8_t> to_write{1, 2, 3};
  EXPECT_EQ(3, buf.push_back(to_write));
  EXPECT_EQ(to_write, buf.pop_front(to_write.size() + 1))
      << "Written bytes read back";

  buf.commit_written(buf.peek_back(std::numeric_limits<size_t>::max()));
  EXPECT_EQ(0, buf.push_back(to_write))
      << "No space available in full buffer";
}
