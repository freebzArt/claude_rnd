#include "ptrack/raster.h"

#include <doctest/doctest.h>

using namespace ptrack;

TEST_CASE("polygon coverage integrates to the polygon area") {
    const std::vector<Vec2> rect = {Vec2(10.25, 30.5), Vec2(40.75, 30.5), Vec2(40.75, 12.25),
                                    Vec2(10.25, 12.25)};
    const Image m = rasterizePolygon(64, 48, rect, 4);
    double area = 0;
    for (int y = 0; y < m.height(); ++y)
        for (int x = 0; x < m.width(); ++x) area += m.at(x, y);
    CHECK(area == doctest::Approx(30.5 * 18.25).epsilon(1e-6));
    CHECK(m.at(20, 20) == doctest::Approx(1.0f));
    CHECK(m.at(5, 5) == doctest::Approx(0.0f));
}

TEST_CASE("insetPolygon moves vertices towards the centroid") {
    const std::vector<Vec2> sq = {Vec2(0, 10), Vec2(10, 10), Vec2(10, 0), Vec2(0, 0)};
    const auto in = insetPolygon(sq, 1.0);
    CHECK((in[0] - Vec2(0, 10)).norm() == doctest::Approx(1.0));
}
