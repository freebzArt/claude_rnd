#pragma once

#include "ptrack/tracker.h"

#include <array>
#include <string>

namespace ptrack {

struct NukeExportOptions {
    int formatHeight = 0;               // display-window height, for the y flip
    std::array<Vec2, 4> surface;        // corners in reference-frame coords (BL, BR, TR, TL)
    std::string nodeName = "PlanarTrack";
    bool stabilize = false;             // inverted pin: locks the plane to its reference position
    int precision = 4;                  // decimal places
};

// Nuke CornerPin2D node as paste-ready script text: animated to1..to4 from the
// track, from1..from4 at the reference frame. Frames missing from the track
// leave gaps in the curves rather than being invented.
std::string nukeCornerPin2D(const TrackResult& track, const NukeExportOptions& opts);

// Convert internal image coordinates to Nuke's (origin bottom-left, y up).
inline Vec2 toNuke(const Vec2& p, int formatHeight) { return {p.x(), formatHeight - p.y()}; }

} // namespace ptrack
