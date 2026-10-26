#include "ptrack/motion.h"

#include <doctest/doctest.h>

#include <Eigen/Dense>

#include <cmath>

using namespace ptrack;

TEST_CASE("expm of a rotation generator is a rotation") {
    Mat3 a = Mat3::Zero();
    a(1, 0) = 0.7;
    a(0, 1) = -0.7;
    const Mat3 r = expm(a);
    CHECK(r(0, 0) == doctest::Approx(std::cos(0.7)).epsilon(1e-12));
    CHECK(r(1, 0) == doctest::Approx(std::sin(0.7)).epsilon(1e-12));
    CHECK(r(2, 2) == doctest::Approx(1.0));
}

TEST_CASE("expm handles large arguments via scaling and squaring") {
    Mat3 a = Mat3::Zero();
    a(0, 0) = 3.0;
    CHECK(expm(a)(0, 0) == doctest::Approx(std::exp(3.0)).epsilon(1e-10));
}

TEST_CASE("homographyFromCorners maps the four corners exactly") {
    const std::array<Vec2, 4> src = {Vec2(0, 100), Vec2(200, 100), Vec2(200, 0), Vec2(0, 0)};
    const std::array<Vec2, 4> dst = {Vec2(12, 130), Vec2(230, 110), Vec2(205, 9), Vec2(20, -4)};
    const Mat3 h = homographyFromCorners(src, dst);
    for (int i = 0; i < 4; ++i) CHECK((applyH(h, src[i]) - dst[i]).norm() < 1e-9);
}

TEST_CASE("toLevel commutes with scaling points") {
    Mat3 h;
    h << 1.1, 0.05, 12.0, -0.03, 0.95, -7.0, 1e-4, -2e-4, 1.0;
    const Vec2 p(321.0, 123.0);
    const double s = 0.25;
    const Vec2 viaLevel = applyH(toLevel(h, s), p * s) / s;
    CHECK((viaLevel - applyH(h, p)).norm() < 1e-9);
}

TEST_CASE("constant-velocity prediction continues a translation") {
    Mat3 h1 = Mat3::Identity(), h2 = Mat3::Identity();
    h1(0, 2) = 3.0;
    h2(0, 2) = 5.0;
    CHECK(predictConstantVelocity(h1, h2)(0, 2) == doctest::Approx(7.0));
}

TEST_CASE("motion bases have the expected degrees of freedom") {
    CHECK(motionBasis(MotionModel::Translation).size() == 2);
    CHECK(motionBasis(MotionModel::Similarity).size() == 4);
    CHECK(motionBasis(MotionModel::Affine).size() == 6);
    CHECK(motionBasis(MotionModel::Perspective).size() == 8);
}
