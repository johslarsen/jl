#pragma once
#include <eigen3/Eigen/Eigen>

namespace jl::eigen {

template <class M>
void constexpr for_each(const Eigen::MatrixBase<M>& m, std::invocable<Eigen::Index, Eigen::Index> auto&& f) {
  if constexpr (std::remove_cvref_t<M>::IsRowMajor) {
    for (Eigen::Index i = 0; i < m.rows(); ++i) {
      for (Eigen::Index j = 0; j < m.cols(); ++j) {
        f(i, j);
      }
    }
  } else {
    for (Eigen::Index j = 0; j < m.cols(); ++j) {
      for (Eigen::Index i = 0; i < m.rows(); ++i) {
        f(i, j);
      }
    }
  }
}
template <class M>
void constexpr for_each(M&& m, std::invocable<typename std::remove_reference_t<M>::Scalar&, Eigen::Index, Eigen::Index> auto&& f) {
  for_each(m, [&m, &f](Eigen::Index i, Eigen::Index j) { f(m(i, j), i, j); });
}
template <class M>
void constexpr for_each(M&& m, std::invocable<typename std::remove_reference_t<M>::Scalar&> auto&& f) {
  for_each(m, [&m, &f](Eigen::Index i, Eigen::Index j) { f(m(i, j)); });
}

/// 2D convolution like e.g. https://www.mathworks.com/help/matlab/ref/conv2.html
/// @return the part of the result without any zero-padded edges
template <class M, class K>
constexpr auto
conv2(const Eigen::MatrixBase<M>& m, const Eigen::MatrixBase<K>& kernel) {
  constexpr auto dim = [](Eigen::Index m, Eigen::Index kernel) {
    return m == Eigen::Dynamic || kernel == Eigen::Dynamic ? Eigen::Dynamic : (m - kernel + 1);
  };
  Eigen::Matrix<std::common_type_t<typename M::Scalar, typename K::Scalar>,
                dim(M::RowsAtCompileTime, K::RowsAtCompileTime),
                dim(M::ColsAtCompileTime, K::ColsAtCompileTime), M::Options>
      result(dim(m.rows(), kernel.rows()), dim(m.cols(), kernel.cols()));
  for_each(result, [&m, &kernel](M::Scalar& e, Eigen::Index i, Eigen::Index j) {
    auto window = m.template block<K::RowsAtCompileTime, K::ColsAtCompileTime>(i, j, kernel.rows(), kernel.cols()).array();
    e = (window * kernel.array().reverse()).sum();
  });
  return result;
}

}  // namespace jl::eigen
