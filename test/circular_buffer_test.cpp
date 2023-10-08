#include <doctest/doctest.h>
#include <jl.h>

#include <future>
#include <latch>
#include <random>
#include <thread>

TEST_SUITE("CircularBuffer") {
  TEST_CASE("static asserts") {
    // jl::CircularBuffer<4 << 10, int> integer_overflow_would_be_ub;
    // jl::CircularBuffer<(4 << 10) + 1> not_power_of_2_capacity;
    // jl::CircularBuffer<1U<<15, uint16_t> index_type_is_too_small>;
  }

  static inline size_t advance(auto& buf, size_t max) {
    size_t written = 0;
    while (written < max) {
      auto writeable = buf.peek_back(max - written);
      if (writeable.empty()) return written;

      auto available = writeable.size();
      written += buf.commit_written(std::move(writeable));
      CHECK(available == buf.commit_read(buf.peek_front(available)));
    }
    return written;
  }

  static inline size_t write_string(auto& buf, const std::string& str) {
    auto writeable = buf.peek_back(str.size());
    std::copy(str.begin(), str.begin() + writeable.size(), writeable.begin());
    return buf.commit_written(std::move(writeable));
  }

  TEST_CASE("read write spans across wrap around") {
    jl::CircularBuffer<char, 4 << 10> buf;
    CHECK((4 << 10) - 1 == advance(buf, (4 << 10) - 1));

    CHECK(2 == write_string(buf, "42"));
    auto readable = buf.peek_front(2);

    CHECK("42" == std::string(readable.data(), readable.size()));
  }

  TEST_CASE("read write spans across index type overflow") {
    jl::CircularBuffer<char, 4 << 10, uint16_t> buf;
    CHECK(UINT16_MAX == advance(buf, UINT16_MAX));

    CHECK(2 == write_string(buf, "42"));
    auto readable = buf.peek_front(2);

    CHECK("42" == std::string(readable.data(), readable.size()));
  }

  TEST_CASE("multi byte value type") {
    jl::CircularBuffer<int32_t, 1 << 10> buf;
    std::vector<int32_t> values{1, 2};
    CHECK(2U == buf.push_back(values));
    CHECK(values == buf.pop_front(2));

    CHECK((1 << 10) - 1 == advance(buf, (1 << 10) - 1));

    CHECK(2U == buf.push_back(values));
    CHECK(values == buf.pop_front(2));
  }

  TEST_CASE("peek back clamped to free space left") {
    jl::CircularBuffer<char, 4 << 10> buf;
    CHECK_MESSAGE((4 << 10) == buf.peek_back(std::numeric_limits<size_t>::max()).size(), "Empty buffer have the full capacity available");

    REQUIRE((4 << 10) == buf.commit_written(buf.peek_back(4 << 10)));
    CHECK_MESSAGE(0 == buf.peek_back(std::numeric_limits<size_t>::max()).size(), "Full buffer have no capacity available");

    REQUIRE(1 == buf.commit_read(buf.peek_front(1)));
    CHECK_MESSAGE(1 == buf.peek_back(std::numeric_limits<size_t>::max()).size(), "When bytes have been read new space is available");
  }

  TEST_CASE("peek front clamped to used space") {
    jl::CircularBuffer<char, 4 << 10> buf;
    CHECK_MESSAGE(0 == buf.peek_front(std::numeric_limits<size_t>::max()).size(), "Empty buffer have no bytes to read");

    REQUIRE(16 == buf.commit_written(buf.peek_back(16)));
    CHECK_MESSAGE(16 == buf.peek_front(std::numeric_limits<size_t>::max()).size(), "When bytes have been written those bytes can be read");

    REQUIRE(8 == buf.commit_read(buf.peek_front(8)));
    CHECK_MESSAGE(8 == buf.peek_front(std::numeric_limits<size_t>::max()).size(), "When some bytes have been read some are left");

    REQUIRE((4 << 10) - 8 == buf.commit_written(buf.peek_back((4 << 10) - 8)));
    CHECK_MESSAGE((4 << 10) == buf.peek_front(std::numeric_limits<size_t>::max()).size(), "When the buffer is full the whole capacity is readable");
  }

  TEST_CASE("push back and pop front") {
    jl::CircularBuffer<char, 4 << 10> buf;

    CHECK_MESSAGE(std::vector<char>() == buf.pop_front(1), "No bytes to read from empty buffer");

    std::vector<char> to_write{1, 2, 3};
    CHECK(3 == buf.push_back(to_write));
    CHECK_MESSAGE(to_write == buf.pop_front(to_write.size() + 1), "Written bytes read back");

    buf.commit_written(buf.peek_back(std::numeric_limits<size_t>::max()));
    CHECK_MESSAGE(0 == buf.push_back(to_write), "No space available in full buffer");
  }

  TEST_CASE("being written to and read from in separate threads") {
    jl::CircularBuffer<int, 1 << 10, std::atomic<uint32_t>> buf;
    uint64_t writer_sum = 0, writer_hash = 0;

    std::latch ready(2);
    std::atomic<bool> still_writing = true;
    std::jthread writer([&]() {
      ready.arrive_and_wait();
      for (int i = 0; i <= 1'000'000; /* incremented in inner loop */) {
        std::span<int> writeable = buf.peek_back(1 + i % 100);  // write in random-ish sized chunks;
        for (size_t off = 0; off < writeable.size(); ++off, ++i) {
          if (i > 1'000'000) {
            writeable = writeable.subspan(0, off);
            break;
          }

          writeable[off] = i;
          writer_sum += i;
          writer_hash += writer_sum;
        }
        buf.commit_written(std::move(writeable));
      }
      still_writing = false;
    });
    ready.arrive_and_wait();

    uint64_t reader_sum = 0, reader_hash = 0;
    while (still_writing || !buf.empty()) {
      std::span<const int> readable = buf.peek_front(100);
      for (const auto& n : readable) {
        reader_sum += n;
        reader_hash += reader_sum;
      }
      buf.commit_read(std::move(readable));
    }

    CHECK(500'000'500'000 == writer_sum);
    CHECK(500'000'500'000 == reader_sum);
    CHECK_MESSAGE(writer_hash == reader_hash, "Elements are read in the order they were written");
  }
}
