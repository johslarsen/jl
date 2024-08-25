#include <doctest/doctest.h>
#include <jl.h>

TEST_SUITE("RingIndex") {
  TEST_CASE_TEMPLATE("maximum capacity", T, uint8_t, std::atomic<uint8_t>) {
    jl::RingIndex<T, 128> fifo;
    using i_n = std::pair<uint8_t, uint8_t>;

    CHECK_MESSAGE(fifo.size() == 0, "w=0, r=0");
    CHECK(fifo.write_free() == i_n(0, 128));
    CHECK(fifo.read_filled() == i_n(0, 0));
    fifo.store_write(128);
    CHECK_MESSAGE(fifo.size() == 128, "w=128, r=000");
    CHECK(fifo.write_free() == i_n(128, 0));
    CHECK(fifo.read_filled() == i_n(0, 128));
    fifo.store_read(128);
    CHECK_MESSAGE(fifo.size() == 0, "w=128, r=128");
    CHECK(fifo.write_free() == i_n(128, 128));
    CHECK(fifo.read_filled() == i_n(128, 0));
    fifo.store_write(255);
    CHECK_MESSAGE(fifo.size() == 127, "w=255, r=128");
    CHECK(fifo.write_free() == i_n(255, 1));
    CHECK(fifo.read_filled() == i_n(128, 127));
    fifo.store_read(255);
    CHECK_MESSAGE(fifo.size() == 0, "w=255, r=255");
    CHECK(fifo.write_free() == i_n(255, 128));
    CHECK(fifo.read_filled() == i_n(255, 0));
    fifo.store_write(257);
    CHECK_MESSAGE(fifo.size() == 2, "w=1, r=255");
    CHECK(fifo.write_free() == i_n(1, 126));
    CHECK(fifo.read_filled() == i_n(255, 2));
    fifo.store_read(256);
    CHECK_MESSAGE(fifo.size() == 1, "w=1, r=0");
    CHECK(fifo.write_free() == i_n(1, 127));
    CHECK(fifo.read_filled() == i_n(0, 1));
    fifo.store_read(257);
    CHECK_MESSAGE(fifo.size() == 0, "w=1, r=1");
    CHECK(fifo.write_free() == i_n(1, 128));
    CHECK(fifo.read_filled() == i_n(1, 0));
  }
}

struct uncopyable {
  int n;

  explicit uncopyable(int n = 0) : n(n) {}
  ~uncopyable() = default;
  uncopyable(const uncopyable&) = delete;
  uncopyable& operator=(const uncopyable&) = delete;

  // make it testable whether it was moved-from
  uncopyable(uncopyable&& other) noexcept : n(std::exchange(other.n, -1)) {}
  uncopyable& operator=(uncopyable&& other) noexcept {
    n = std::exchange(other.n, -1);
    return *this;
  }
};

TEST_SUITE("Ring") {
  TEST_CASE("move-only type") {
    jl::Ring<uncopyable, 4> ring;

    CHECK(ring.pop() == std::nullopt);
    CHECK(ring.push(uncopyable{1}));
    CHECK(ring.push(uncopyable{2}));
    CHECK(ring.push(uncopyable{3}));

    uncopyable moved_from(4);
    CHECK(ring.push_from(moved_from));
    CHECK(moved_from.n == -1);  // i.e. moved from

    uncopyable kept_as_is(5);
    CHECK(!ring.push_from(kept_as_is));
    CHECK(kept_as_is.n == 5);

    CHECK(ring.pop().value().n == 1);
    CHECK(ring.pop().value().n == 2);
    CHECK(ring.pop().value().n == 3);
    CHECK(ring.pop().value().n == 4);
    CHECK(ring.pop() == std::nullopt);
  }
}
