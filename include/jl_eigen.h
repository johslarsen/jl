#pragma once
#include <eigen3/Eigen/Eigen>

namespace jl::eigen {

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
                dim(M::ColsAtCompileTime, K::ColsAtCompileTime)>
      result(dim(m.rows(), kernel.rows()), dim(m.cols(), kernel.cols()));
  for (Eigen::Index i = 0; i < result.rows(); ++i) {
    for (Eigen::Index j = 0; j < result.cols(); ++j) {
      auto window = m.template block<K::RowsAtCompileTime, K::ColsAtCompileTime>(i, j, kernel.rows(), kernel.cols()).array();
      result(i, j) = (window * kernel.array().reverse()).sum();
    }
  }
  return result;
}

}  // namespace jl::eigen
