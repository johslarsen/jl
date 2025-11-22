#include <doctest/doctest.h>
#include <jl.h>

template <class T>
auto futures_of(auto&& promises) {
  std::vector<std::future<T>> futures;
  futures.reserve(promises.size());
  for (auto& promise : promises) futures.emplace_back(promise.get_future());
  return futures;
}

TEST_SUITE("synchronization") {
  TEST_CASE("jl::unwrap<std::future<...>>") {
    std::promise<std::unique_ptr<int>> promise;  // move-only type
    SUBCASE("before ready") {
      CHECK_THROWS(std::ignore = jl::unwrap(promise.get_future()));
    }
    SUBCASE("when ready") {
      promise.set_value(nullptr);
      CHECK(jl::unwrap(promise.get_future()) == nullptr);
    }
  }

  TEST_CASE("jl::any_ready_of") {
    SUBCASE("basics") {
      std::vector<std::promise<int>> promises(2);
      auto futures = futures_of<int>(promises);

      CHECK(jl::any_ready_of(futures) == futures.end());

      promises[1].set_value(42);
      CHECK(jl::any_ready_of(futures) == futures.begin() + 1);

      auto ready = jl::any_ready_of(futures);
      CHECK_MESSAGE(ready == futures.begin() + 1, "did not get it, so should still be same ready");
      CHECK(ready->get() == 42);

      CHECK_MESSAGE(jl::any_ready_of(futures) == futures.end(), "got it now, so no ready left");
    }
    SUBCASE("empty range") {
      std::vector<std::future<int>> no_futures;
      CHECK(jl::any_ready_of(no_futures) == no_futures.end());
    }
  }
}
