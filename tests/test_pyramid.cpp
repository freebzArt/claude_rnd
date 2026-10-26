#include "ptrack/pyramid.h"

#include <doctest/doctest.h>

#include <cmath>

using namespace ptrack;

namespace {

// Intensity-weighted centroid using pixel-centre coordinates.
void centroid(const Image& img, double& cx, double& cy) {
    double s = 0, sx = 0, sy = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x) {
            const double v = img.at(x, y);
            s += v;
            sx += v * (x + 0.5);
            sy += v * (y + 0.5);
        }
    cx = sx / s;
    cy = sy / s;
}

} // namespace

TEST_CASE("downsampling keeps a constant image constant") {
    const Image img(64, 48, 0.37f);
    const Image d = downsample2(img);
    CHECK(d.width() == 32);
    CHECK(d.height() == 24);
    for (int y = 0; y < d.height(); ++y)
        for (int x = 0; x < d.width(); ++x) CHECK(d.at(x, y) == doctest::Approx(0.37f));
}

TEST_CASE("pyramid levels scale coordinates by exactly one half") {
    Image img(128, 96);
    const double bx = 50.3, by = 41.7;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x) {
            const double dx = x + 0.5 - bx, dy = y + 0.5 - by;
            img.at(x, y) = static_cast<float>(std::exp(-(dx * dx + dy * dy) / (2 * 36.0)));
        }
    const Pyramid pyr(img, 3);
    REQUIRE(pyr.levels() == 3);
    for (int l = 0; l < 3; ++l) {
        double cx, cy;
        centroid(pyr.level(l).img, cx, cy);
        CHECK(cx == doctest::Approx(bx * pyr.level(l).scale).epsilon(1e-3));
        CHECK(cy == doctest::Approx(by * pyr.level(l).scale).epsilon(1e-3));
    }
}

TEST_CASE("bilinear sampling reproduces a linear ramp") {
    Image img(16, 16);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) img.at(x, y) = static_cast<float>(2.0 * (x + 0.5) + 3.0 * (y + 0.5));
    float v;
    REQUIRE(img.sample(7.3, 4.9, v));
    CHECK(v == doctest::Approx(2.0 * 7.3 + 3.0 * 4.9).epsilon(1e-6));
    CHECK_FALSE(img.sample(0.2, 5.0, v));
}

TEST_CASE("autoLevels keeps the coarsest region usable") {
    CHECK(autoLevels(40, 40) == 1);
    CHECK(autoLevels(200, 120) == 3);
    CHECK(autoLevels(5000, 5000) == 6);
}
