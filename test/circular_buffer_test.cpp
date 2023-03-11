#include <gtest/gtest.h>
#include <jl.h>

TEST(CircularBuffer, StaticAsserts) {
  // jl::CircularBuffer<4 << 10, int> integer_overflow_would_be_ub;
  // jl::CircularBuffer<(4 << 10) + 1> not_power_of_2_capacity;
  // jl::CircularBuffer<1U<<15, uint16_t> index_type_is_too_small>;
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
  ssize_t written = writeable.size();
  std::copy(str.begin(), str.begin() + written, writeable.begin());
  buf.commit_written(std::move(writeable));
  return written;
}

TEST(CircularBuffer, ReadWriteSpansAcrossWrapAround) {
  jl::CircularBuffer<char, 4 << 10> buf;
  EXPECT_EQ((4 << 10) - 1, advance(buf, (4 << 10) - 1));

  EXPECT_EQ(2, write_string(buf, "42"));
  auto readable = buf.peek_front(2);

  EXPECT_EQ("42", std::string(readable.data(), readable.size()));
}

TEST(CircularBuffer, ReadWriteSpansAcrossIndexTypeOverflow) {
  jl::CircularBuffer<char, 4 << 10, uint16_t> buf;
  EXPECT_EQ(UINT16_MAX, advance(buf, UINT16_MAX));

  EXPECT_EQ(2, write_string(buf, "42"));
  auto readable = buf.peek_front(2);

  EXPECT_EQ("42", std::string(readable.data(), readable.size()));
}

TEST(CircularBuffer, MultiByteValueType) {
  jl::CircularBuffer<int32_t, 1<<10> buf;
  std::vector<int32_t> values{1,2};
  EXPECT_EQ(2U, buf.push_back(values));
  EXPECT_EQ(values, buf.pop_front(2));

  EXPECT_EQ((1 << 10) - 1, advance(buf, (1 << 10) - 1));

  EXPECT_EQ(2U, buf.push_back(values));
  EXPECT_EQ(values, buf.pop_front(2));
}

TEST(CircularBuffer, PeekBackClampedToFreeSpaceLeft) {
  jl::CircularBuffer<char, 4 << 10> buf;
  EXPECT_EQ(4 << 10, buf.peek_back(std::numeric_limits<size_t>::max()).size())
      << "Empty buffer have the full capacity available";

  buf.commit_written(buf.peek_back(4 << 10));
  EXPECT_EQ(0, buf.peek_back(std::numeric_limits<size_t>::max()).size())
      << "Full buffer have no capacity available";

  buf.commit_read(buf.peek_front(1));
  EXPECT_EQ(1, buf.peek_back(std::numeric_limits<size_t>::max()).size())
      << "When bytes have been read new space is available";
}

TEST(CircularBuffer, PeekFrontClampedToUsedSpace) {
  jl::CircularBuffer<char, 4 << 10> buf;
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

TEST(CircularBuffer, PushBackAndPopFront) {
  jl::CircularBuffer<char, 4 << 10> buf;

  EXPECT_EQ(std::vector<char>(), buf.pop_front(1))
      << "No bytes to read from empty buffer";

  std::vector<char> to_write{1, 2, 3};
  EXPECT_EQ(3, buf.push_back(to_write));
  EXPECT_EQ(to_write, buf.pop_front(to_write.size() + 1))
      << "Written bytes read back";

  buf.commit_written(buf.peek_back(std::numeric_limits<size_t>::max()));
  EXPECT_EQ(0, buf.push_back(to_write))
      << "No space available in full buffer";
}
