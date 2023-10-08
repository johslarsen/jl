#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include <cstdlib>

int main(int argc, char **argv) {
  doctest::Context context;

  // NOLINTBEGIN(concurrency-mt-unsafe)
  if (auto tc = std::getenv("TEST_CASE"); tc != nullptr) context.setOption("test-case", tc);
  if (auto ts = std::getenv("TEST_SUITE"); ts != nullptr) context.setOption("test-suite", ts);
  // NOLINTEND(concurrency-mt-unsafe)

  context.applyCommandLine(argc, argv);
  return context.run();
}
