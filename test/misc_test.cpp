#include <doctest/doctest.h>
#include <jl.h>

TEST_SUITE("misc") {
  TEST_CASE("any_of") {
    static_assert(jl::any_of<int, int, float>);
    static_assert(jl::any_of<float, int, float>);
    static_assert(!jl::any_of<double, int, float>);
  }
  TEST_CASE("overloaded") {
    std::vector<std::variant<float, std::string>> matches;

    using var = std::variant<std::string, float, int>;
    var n = 3.14F;

    auto handler = jl::overload{
        [&](const jl::any_of<int, float> auto &n) { matches.emplace_back(static_cast<float>(n)); return 43; },
        [&](const std::string &s) { matches.emplace_back(s);  return 42; },
    };
    std::visit(handler, var{3.14F});
    std::visit(handler, var{42});
    std::visit(handler, var{"foo"});

    CHECK(std::get<float>(matches.at(0)) == 3.14F);
    CHECK(std::get<float>(matches.at(1)) == 42.0F);
    CHECK(std::get<std::string>(matches.at(2)) == "foo");
  }
  TEST_CASE("idx_iter") {
    static_assert(std::input_or_output_iterator<jl::idx_iter<std::span<int>>>);
    static_assert(std::input_iterator<jl::idx_iter<std::span<int>>>);
    static_assert(std::forward_iterator<jl::idx_iter<std::span<int>>>);
    static_assert(std::bidirectional_iterator<jl::idx_iter<std::span<int>>>);
    static_assert(std::random_access_iterator<jl::idx_iter<std::span<int>>>);
  }
}
