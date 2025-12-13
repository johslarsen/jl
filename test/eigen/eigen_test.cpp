#include <doctest/doctest.h>
#include <jl_eigen.h>

#include <cfloat>

static void expect_near(const auto& a, const auto& b, double epsilon = DBL_EPSILON) {
  CHECK(std::make_pair(a.rows(), a.cols()) == std::make_pair(b.rows(), b.cols()));
  CHECK_MESSAGE((b - a).array().abs().maxCoeff() <= epsilon, (b - a));
}

TEST_SUITE("eigen") {
  TEST_CASE("for_each") {
    SUBCASE("mutable reference") {
      Eigen::Matrix<Eigen::Index, 5, 2, Eigen::ColMajor> col_major;
      Eigen::Matrix<Eigen::Index, 5, 2, Eigen::RowMajor> row_major;

      jl::eigen::for_each(col_major, [](Eigen::Index& e, Eigen::Index i, Eigen::Index j) {
        e = i << 4 | j;
      });
      jl::eigen::for_each(row_major, [](Eigen::Index& e, Eigen::Index i, Eigen::Index j) {
        e = i << 4 | j;
      });

      CHECK(col_major(4, 1) == 0x41);
      CHECK(row_major(4, 1) == 0x41);
    }
    SUBCASE("const reference") {
      const Eigen::Matrix3d& m = Eigen::Matrix3d::Identity();
      Eigen::Index sum = 0;
      jl::eigen::for_each(m, [&sum](const Eigen::Index& e) { sum += e; });
      CHECK(sum == 3);
    }
  }
  TEST_CASE("conv2") {
    SUBCASE("scalar kernel") {
      auto upto5 = Eigen::Vector<double, 5>::LinSpaced(1, 5);
      Eigen::Matrix<double, 5, 5> m = upto5 * upto5.transpose();
      expect_near(jl::eigen::conv2(m, Eigen::Matrix<double, 1, 1>(M_PI)), M_PI * m);
    }
    SUBCASE("odd square kernel") {
      auto upto5 = Eigen::Vector<double, 5>::LinSpaced(1, 5);
      Eigen::Matrix<double, 5, 5> m = upto5 * upto5.transpose();
      Eigen::Matrix3d kernel{
          {1, 2, 3},
          {4, 5, 6},
          {7, 8, 9},
      };
      // octave> conv2([1:5]' * [1:5],[1 2 3; 4 5 6; 7 8 9], "valid")
      expect_near(Eigen::Matrix<double, 3, 3>{
                      {132, 204, 276},
                      {216, 333, 450},
                      {300, 462, 624},
                  },
                  jl::eigen::conv2(m, kernel));
    }
    SUBCASE("rectangular kernel") {
      Eigen::Matrix<double, 5, 4> m = Eigen::Vector<double, 5>::LinSpaced(1, 5) * Eigen::Vector<double, 4>::LinSpaced(1, 4).transpose();
      Eigen::Matrix<double, 2, 3> kernel{
          {1, 2, 3},
          {4, 5, 6},
      };
      // octave> conv2([1:5]' * [1:4],[1 2 3; 4 5 6], "valid")
      expect_near(Eigen::Matrix<double, 4, 2>{
                      {48, 75},
                      {86, 134},
                      {124, 193},
                      {162, 252},

                  },
                  jl::eigen::conv2(m, kernel));
    }
  }
}
