#pragma once

#include "ptrack/image.h"

#include <string>
#include <vector>

namespace ptrack::io {

struct ExrInfo {
    int width = 0, height = 0;                 // display window size
    int displayMinX = 0, displayMinY = 0;      // display window origin (EXR coords)
    int dataMinX = 0, dataMinY = 0, dataMaxX = -1, dataMaxY = -1;
    float pixelAspect = 1.0f;
    int parts = 1;
    std::vector<std::string> channels;         // of the part that was read
};

struct ExrReadOptions {
    int part = 0;          // multi-part files: which part
    std::string layer;     // channel prefix, e.g. "beauty" reads beauty.R/G/B; empty = R/G/B
    // "luma" combines R, G, B with Rec.709 weights; anything else names one channel
    // within the layer ("G", "A", "Y") or a full channel name ("depth.Z").
    std::string channel = "luma";
};

// Read one tracking channel into display-window coordinates. Overscan outside
// the display window is cropped; display areas without data are zero.
// Throws std::runtime_error with a readable message on failure.
Image readExr(const std::string& path, const ExrReadOptions& opts = {}, ExrInfo* info = nullptr);

// Header-only inspection.
ExrInfo probeExr(const std::string& path, int part = 0);

// Write a single-channel image as R, G and B (so any viewer shows grey),
// in half or full float, ZIP compressed.
void writeExrGrey(const std::string& path, const Image& img, bool halfFloat = true);

} // namespace ptrack::io
