#pragma once
#include <eigen3/Eigen/Eigen>

namespace jl::eigen {

template <class M>
void constexpr for_each(const Eigen::MatrixBase<M>& m, std::invocable<Eigen::Index, Eigen::Index> auto&& f) {
  if constexpr (std::remove_cvref_t<M>::IsRowMajor) {
    for (Eigen::Index j = 0; j < m.rows(); ++j) {
      for (Eigen::Index i = 0; i < m.cols(); ++i) {
        f(j, i);
      }
    }
  } else {
    for (Eigen::Index i = 0; i < m.cols(); ++i) {
      for (Eigen::Index j = 0; j < m.rows(); ++j) {
        f(j, i);
      }
    }
  }
}
template <class M>
void constexpr for_each(M&& m, std::invocable<typename std::remove_reference_t<M>::Scalar&, Eigen::Index, Eigen::Index> auto&& f) {
  for_each(m, [&m, &f](Eigen::Index j, Eigen::Index i) { f(m(j, i), j, i); });
}
template <class M>
void constexpr for_each(M&& m, std::invocable<typename std::remove_reference_t<M>::Scalar&> auto&& f) {
  for_each(m, [&m, &f](Eigen::Index j, Eigen::Index i) { f(m(j, i)); });
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
  for_each(result, [&m, &kernel](M::Scalar& e, Eigen::Index j, Eigen::Index i) {
    auto window = m.template block<K::RowsAtCompileTime, K::ColsAtCompileTime>(j, i, kernel.rows(), kernel.cols()).array();
    e = (window * kernel.array().reverse()).sum();
  });
  return result;
}

}  // namespace jl::eigen
