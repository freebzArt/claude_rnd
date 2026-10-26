#pragma once

#include "ptrack/tracker.h"

#include <array>
#include <string>

namespace ptrack::io {

// Everything the CLI and later the workbench need to reuse a track.
// Schema: docs/project-format.md, "Track result".
struct TrackDocument {
    TrackResult track;
    std::array<Vec2, 4> surface{};  // reference-frame coords (BL, BR, TR, TL)
    int formatWidth = 0, formatHeight = 0;
    double pixelAspect = 1.0;
    std::string source;             // file pattern the track was made from
    std::string settings;           // human-readable summary of the parameters used
};

void writeTrackJson(const std::string& path, const TrackDocument& doc);
TrackDocument readTrackJson(const std::string& path);

// Synthetic ground truth: per-frame homographies and the surface, same layout.
void writeTruthJson(const std::string& path, const TrackDocument& truth);

} // namespace ptrack::io
