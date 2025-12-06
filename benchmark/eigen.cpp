#include <benchmark/benchmark.h>
#include <jl_eigen.h>

template <Eigen::Index M, Eigen::Index K>
static void BM_conv2(benchmark::State& state) {
  Eigen::Matrix<double, M, M> m = Eigen::Matrix<double, M, M>::Random();
  Eigen::Matrix<double, K, K> kernel = Eigen::Matrix<double, K, K>::Random();

  for (auto _ : state) {
    auto result = jl::eigen::conv2(m, kernel);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK_TEMPLATE(BM_conv2, 3, 3);
BENCHMARK_TEMPLATE(BM_conv2, 10, 3);
BENCHMARK_TEMPLATE(BM_conv2, 100, 10);

template <Eigen::Index K>
static void BM_conv2_dynamic_signal(benchmark::State& state) {
  Eigen::MatrixXd m = Eigen::MatrixXd::Random(state.range(0), state.range(0));
  Eigen::Matrix<double, K, K> kernel = Eigen::Matrix<double, K, K>::Random();

  for (auto _ : state) {
    auto result = jl::eigen::conv2(m, kernel);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK_TEMPLATE(BM_conv2_dynamic_signal, 3)->ArgName("M")->ArgsProduct({{3, 10, 100, 1000}});
BENCHMARK_TEMPLATE(BM_conv2_dynamic_signal, 10)->ArgName("M")->ArgsProduct({{3, 10, 100, 1000}});

static void BM_conv2_dynamic_both(benchmark::State& state) {
  Eigen::MatrixXd m = Eigen::MatrixXd::Random(state.range(0), state.range(0));
  Eigen::MatrixXd kernel = Eigen::MatrixXd::Random(state.range(1), state.range(1));

  for (auto _ : state) {
    auto result = jl::eigen::conv2(m, kernel);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(BM_conv2_dynamic_both)->ArgNames({"M", "Kernel"})->ArgsProduct({{3, 10, 100, 1000}, {3, 10}});

BENCHMARK_MAIN();  // NOLINT
