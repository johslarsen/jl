#include <doctest/doctest.h>
#include <jl.h>

#include <list>
#include <random>

TEST_SUITE("misc") {
  TEST_CASE("one_of") {
    static_assert(jl::one_of<int, int, float>);
    static_assert(jl::one_of<float, int, float>);
    static_assert(!jl::one_of<double, int, float>);
  }
  TEST_CASE("overloaded") {
    std::vector<std::variant<float, std::string>> matches;

    using var = std::variant<std::string, float, int>;
    var n = 3.14F;

    auto handler = jl::overload{
        [&](const jl::one_of<int, float> auto &n) { matches.emplace_back(static_cast<float>(n)); return 43; },
        [&](const std::string &s) { matches.emplace_back(s);  return 42; },
    };
    std::visit(handler, var{3.14F});
    std::visit(handler, var{42});
    std::visit(handler, var{"foo"});

    CHECK(std::get<float>(matches.at(0)) == 3.14F);
    CHECK(std::get<float>(matches.at(1)) == 42.0F);
    CHECK(std::get<std::string>(matches.at(2)) == "foo");
  }
  TEST_CASE("invocable_counter") {
    jl::invocable_counter counter;
    SUBCASE("lambda") {
      for (size_t i = 0; i < 10; ++i) counter.wrap([] {})();
      CHECK(counter.total_calls() == 10);
    }
    SUBCASE("function object") {
      for (size_t i = 0; i < 10; ++i) counter.wrap(std::less{})(1, 2);
      CHECK(counter.total_calls() == 10);
    }
    SUBCASE("standalone function") {
      for (size_t i = 0; i < 10; ++i) counter.wrap (&std::time)(nullptr);
      CHECK(counter.total_calls() == 10);
    }
    SUBCASE("instance method") {
      std::string s;
      for (size_t i = 0; i < 10; ++i) counter.wrap (&std::string::size)(s);
      CHECK(counter.total_calls() == 10);
    }
  }
  TEST_CASE("sorted_append") {
    constexpr std::string_view alphabet = "abcdefghijklmnopqrstuvwxyz";
    jl::invocable_counter comparisons;

    std::string ordered;
    SUBCASE("ordered input") {
      for (auto i : std::views::iota('a', 'z' + 1)) {
        jl::sorted_append(ordered, i, comparisons.wrap(std::less()));
      }
      CHECK_MESSAGE(comparisons.total_calls() == alphabet.size() - 1, "linear search base case: O(1)");
    }
    SUBCASE("reverse ordered input") {
      for (auto i : std::views::iota('a', 'z' + 1) | std::views::reverse) {
        jl::sorted_append(ordered, i, comparisons.wrap(std::less()));
      }
      CHECK_MESSAGE(comparisons.total_calls() == ((alphabet.size() - 1) * alphabet.size()) / 2, "linear search worst case: O(n)");
    }
    SUBCASE("random ordered input") {
      auto randomized = std::string(alphabet);
      std::ranges::shuffle(randomized, std::mt19937(std::random_device{}()));
      SUBCASE("into string") {
        for (auto i : randomized) jl::sorted_append(ordered, i);
      }
      SUBCASE("into vector") {
        std::vector<char> chars;
        for (auto i : randomized) jl::sorted_append(ordered, i);
        std::ranges::copy(chars, ordered.end());
      }
      SUBCASE("into bidirectional list") {
        std::list<char> chars;
        for (auto i : randomized) jl::sorted_append(ordered, i);
        std::ranges::copy(chars, ordered.end());
      }
    }
    CHECK(ordered == alphabet);
  }
  TEST_CASE("idx_iter") {
    static_assert(std::input_or_output_iterator<jl::idx_iter<std::span<int>>>);
    static_assert(std::input_iterator<jl::idx_iter<std::span<int>>>);
    static_assert(std::forward_iterator<jl::idx_iter<std::span<int>>>);
    static_assert(std::bidirectional_iterator<jl::idx_iter<std::span<int>>>);
    static_assert(std::random_access_iterator<jl::idx_iter<std::span<int>>>);
  }
}
