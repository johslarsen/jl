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

// mdspan is still missing as of GCC libstdc++ 15.2.1
#ifdef _LIBCPP_VERSION
#include <mdspan>
namespace jl::eigen {

constexpr size_t as_extent(Eigen::Index i) {
  return i == Eigen::Dynamic ? std::dynamic_extent : i;
}

template <class Dense>
  requires std::derived_from<std::remove_cvref_t<Dense>, Eigen::PlainObjectBase<std::remove_cvref_t<Dense>>>
constexpr auto span_of(Dense& m) {
  using element_type = std::conditional_t<std::is_const_v<Dense>, std::add_const_t<typename Dense::Scalar>, typename Dense::Scalar>;
  using extents = std::extents<Eigen::Index, as_extent(Dense::RowsAtCompileTime), as_extent(Dense::ColsAtCompileTime)>;
  if constexpr (Dense::IsRowMajor) {
    return std::mdspan<element_type, extents, std::layout_right>(m.data(), m.rows(), m.cols());
  } else {
    return std::mdspan<element_type, extents, std::layout_left>(m.data(), m.rows(), m.cols());
  }
}

}  // namespace jl::eigen

namespace jl::md {

template <class Extents>
using idx = std::array<typename Extents::index_type, Extents::rank()>;

template <class Extents, class... Is>
void constexpr row_major_each(const Extents& e, std::invocable<idx<Extents>> auto&& f, Is... outer) {
  if constexpr (sizeof...(Is) == Extents::rank()) {
    f(std::array{outer...});
  } else {
    for (typename Extents::index_type i = 0; i < e.extent(sizeof...(Is)); ++i) {
      row_major_each(e, f, outer..., i);
    }
  }
}
template <class Extents, class... Is>
void constexpr col_major_each(const Extents& e, std::invocable<idx<Extents>> auto&& f, Is... outer) {
  if constexpr (sizeof...(Is) == Extents::rank()) {
    std::array idx{outer...};
    std::ranges::reverse(idx);
    f(idx);
  } else {
    for (typename Extents::index_type i = 0; i < e.extent(Extents::rank() - 1 - sizeof...(Is)); ++i) {
      col_major_each(e, f, outer..., i);
    }
  }
}

template <class MDSpan>
void constexpr for_each(const MDSpan& m, std::invocable<idx<typename MDSpan::extents_type>> auto&& f) {
  if constexpr (std::same_as<typename MDSpan::layout_type, std::layout_right>) {
    row_major_each(m.extents(), f);
  } else if constexpr (std::same_as<typename MDSpan::layout_type, std::layout_left>) {
    col_major_each(m.extents(), f);
  } else {
    static_assert(false, "unsupported LayoutMapping");
  }
}
template <class MDSpan>
void constexpr for_each(const MDSpan& m, std::invocable<typename std::remove_reference_t<MDSpan>::element_type&, idx<typename MDSpan::extents_type>> auto&& f) {
  for_each(m, [&m, &f](idx<typename MDSpan::extents_type> i) { f(m[i], i); });
}
template <class MDSpan>
void constexpr for_each(const MDSpan& m, std::invocable<typename std::remove_reference_t<MDSpan>::element_type&> auto&& f) {
  for_each(m, [&m, &f](idx<typename MDSpan::extents_type> i) { f(m[i]); });
}

}  // namespace jl::md
#endif
