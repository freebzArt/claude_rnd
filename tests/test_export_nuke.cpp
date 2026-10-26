#include "ptrack/export_nuke.h"

#include <doctest/doctest.h>

#include <string>

using namespace ptrack;

TEST_CASE("CornerPin2D export flips y and leaves gaps for missing frames") {
    TrackResult t;
    t.referenceFrame = 1001;
    for (int f : {1001, 1002, 1004}) {
        TrackedFrame tf;
        tf.frame = f;
        tf.h(0, 2) = (f - 1001) * 2.0; // translate right 2 px per frame
        t.frames.push_back(tf);
    }
    NukeExportOptions o;
    o.formatHeight = 1080;
    o.surface = {Vec2(100, 900), Vec2(500, 900), Vec2(500, 200), Vec2(100, 200)};
    o.nodeName = "Pin1";
    const std::string s = nukeCornerPin2D(t, o);
    CHECK(s.find("CornerPin2D {") != std::string::npos);
    CHECK(s.find(" to1 {{curve x1001 100 102 x1004 106} {curve x1001 180 180 x1004 180}}") !=
          std::string::npos);
    CHECK(s.find(" from3 {500 880}") != std::string::npos);
    CHECK(s.find(" name Pin1") != std::string::npos);
    CHECK(s.find("invert") == std::string::npos);
    o.stabilize = true;
    CHECK(nukeCornerPin2D(t, o).find(" invert true") != std::string::npos);
}
