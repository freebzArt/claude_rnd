#include "ptrack/io/tracking_space.h"

#include <doctest/doctest.h>

#include <string>

using namespace ptrack;

namespace {

RgbImage grey(float v) {
    RgbImage im{Image(8, 4, v), Image(8, 4, v), Image(8, 4, v)};
    im.r.at(3, 2) = 2.0f * v; // one coloured pixel
    return im;
}

} // namespace

TEST_CASE("built-in tracking curve and channel selection") {
    io::TrackingSpaceOptions o;
    o.curve = TrackingCurve::ACEScct;
    const io::TrackingSpace s(o);
    CHECK_FALSE(s.usesOcio());
    const Image t = s.apply(grey(0.18f));
    CHECK(t.at(0, 0) == doctest::Approx(applyCurve(TrackingCurve::ACEScct, 0.18f)));
    o.channel = "R";
    o.curve = TrackingCurve::None;
    CHECK(io::TrackingSpace(o).apply(grey(0.18f)).at(3, 2) == doctest::Approx(0.36f));
    o.channel = "depth.Z";
    CHECK_THROWS_AS(io::TrackingSpace{o}, std::runtime_error);
}

#ifdef PTRACK_HAVE_OCIO
TEST_CASE("OCIO conversion by role matches the built-in ACEScct curve on grey") {
    io::TrackingSpaceOptions o;
    o.ocioConfig = PTRACK_TEST_DATA "/test_config.ocio";
    o.from = "scene_linear";    // role -> ACEScg
    o.to = "compositing_log";   // role -> ACEScct
    const io::TrackingSpace s(o);
    CHECK(s.usesOcio());
    CHECK(s.description().find("ACEScg -> ACEScct") != std::string::npos);
    for (float v : {0.001f, 0.18f, 1.0f, 12.0f}) {
        const Image t = s.apply(grey(v));
        CHECK(t.at(0, 0) == doctest::Approx(applyCurve(TrackingCurve::ACEScct, v)).epsilon(1e-4));
    }
}

TEST_CASE("OCIO errors are readable") {
    io::TrackingSpaceOptions o;
    o.ocioConfig = PTRACK_TEST_DATA "/test_config.ocio";
    o.from = "ACEScg";
    CHECK_THROWS_WITH_AS(io::TrackingSpace{o}, doctest::Contains("both"), std::runtime_error);
    o.to = "NotASpace";
    CHECK_THROWS_WITH_AS(io::TrackingSpace{o}, doctest::Contains("NotASpace"), std::runtime_error);
}
#endif
