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
        [&](const jl::one_of<int, float> auto& n) { matches.emplace_back(static_cast<float>(n)); return 43; },
        [&](const std::string& s) { matches.emplace_back(s);  return 42; },
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

  TEST_CASE("insert_unique") {
    std::string set;
    auto b = jl::insert_unique(set, std::ranges::lower_bound(set, 'b'), 'b');
    CHECK(set == "b");
    REQUIRE(b != set.end());
    CHECK(*b == 'b');

    auto bb = jl::insert_unique(set, b, 'b');
    CHECK(bb == set.end());
    CHECK(set == "b");

    jl::insert_unique(set, std::ranges::lower_bound(set, 'a'), 'a');
    jl::insert_unique(set, std::ranges::lower_bound(set, 'c'), 'c');
    CHECK(set == "abc");

    CHECK(*std::ranges::lower_bound(set, 'b') == 'b');
    bb = jl::insert_unique(set, std::ranges::lower_bound(set, 'b'), 'b');
    CHECK(bb == set.end());
    CHECK(set == "abc");
  }

  TEST_CASE("sorted_append/inserted") {
    constexpr std::string_view alphabet = "abcdefghijklmnopqrstuvwxyz";
    jl::invocable_counter append_comparisons;

    std::string appended, inserted;
    SUBCASE("ordered input") {
      for (auto i : std::views::iota('a', 'z' + 1)) {
        jl::sorted_append(appended, i, append_comparisons.wrap(std::less()));
        jl::sorted_insert(inserted, i);
      }
      CHECK(appended == alphabet);
      CHECK_MESSAGE(append_comparisons.total_calls() == alphabet.size() - 1, "linear search base case: O(1)");
    }
    SUBCASE("reverse ordered input") {
      for (auto i : std::views::iota('a', 'z' + 1) | std::views::reverse) {
        jl::sorted_append(appended, i, append_comparisons.wrap(std::less()));
        jl::sorted_insert(inserted, i);
      }
      CHECK(appended == alphabet);
      CHECK_MESSAGE(append_comparisons.total_calls() == ((alphabet.size() - 1) * alphabet.size()) / 2, "linear search worst case: O(n)");
    }
    SUBCASE("random ordered input") {
      auto randomized = std::string(alphabet);
      std::ranges::shuffle(randomized, std::mt19937(std::random_device{}()));
      SUBCASE("into string") {
        for (auto i : randomized) {
          jl::sorted_append(appended, i);
          jl::sorted_insert(inserted, i);
        }
        CHECK(appended == alphabet);
      }
      SUBCASE("into vector") {
        std::vector<char> appended_chars, inserted_chars;
        for (auto i : randomized) {
          jl::sorted_append(appended_chars, i);
          jl::sorted_insert(inserted_chars, i);
        }
        CHECK(jl::view_of(std::as_bytes(std::span(appended_chars))) == alphabet);
        CHECK(jl::view_of(std::as_bytes(std::span(inserted_chars))) == alphabet);
      }
      SUBCASE("into bidirectional list") {
        std::list<char> appended_chars;
        for (auto i : randomized) {
          jl::sorted_append(appended_chars, i);
        }
        std::ranges::copy(appended_chars, std::back_inserter(appended));
        CHECK(appended == alphabet);
      }
    }
  }
  TEST_CASE("idx_iter") {
    static_assert(std::input_or_output_iterator<jl::idx_iter<std::span<int>>>);
    static_assert(std::input_iterator<jl::idx_iter<std::span<int>>>);
    static_assert(std::forward_iterator<jl::idx_iter<std::span<int>>>);
    static_assert(std::bidirectional_iterator<jl::idx_iter<std::span<int>>>);
    static_assert(std::random_access_iterator<jl::idx_iter<std::span<int>>>);
  }

  TEST_CASE("urandom") {
    auto fixed_seeded = jl::urandom<uint32_t>(42, std::mt19937(42));
    auto random_seeded = jl::urandom(42);
    CHECK(fixed_seeded.size() == 42);
    CHECK(random_seeded.size() == 42);

    CHECK(jl::to_xdigits(std::as_bytes(std::span(fixed_seeded))) == "66dce15fb33deacb5c0362f30e95f52e6af463bb47d499c7bcae4199142ccb9866d6f02779182272d241");
    CHECK(fixed_seeded != random_seeded);
  }
}
