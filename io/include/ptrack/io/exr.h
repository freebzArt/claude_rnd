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
    // Region covered by images returned with these options, relative to the
    // display window origin: (0, 0, width, height) unless overscan is kept.
    int originX = 0, originY = 0, imageWidth = 0, imageHeight = 0;
    std::vector<std::string> channels;         // of the part that was read
};

struct ExrReadOptions {
    int part = 0;          // multi-part files: which part
    std::string layer;     // channel prefix, e.g. "beauty" reads beauty.R/G/B; empty = R/G/B
    // "luma" combines R, G, B with Rec.709 weights; anything else names one channel
    // within the layer ("G", "A", "Y") or a full channel name ("depth.Z").
    std::string channel = "luma";
    // Keep data outside the display window. Returned images then start at
    // (originX, originY) relative to the display origin (negative for overscan).
    bool overscan = false;
};

// Read R, G and B (or Y, copied to all three) into display-window coordinates.
RgbImage readExrRgb(const std::string& path, const ExrReadOptions& opts = {}, ExrInfo* info = nullptr);

// Read one tracking channel into display-window coordinates. Overscan outside
// the display window is cropped unless opts.overscan; areas without data are zero.
// Throws std::runtime_error with a readable message on failure.
Image readExr(const std::string& path, const ExrReadOptions& opts = {}, ExrInfo* info = nullptr);

// Header-only inspection.
ExrInfo probeExr(const std::string& path, int part = 0);

struct ExrWriteOptions {
    bool halfFloat = true;
    int displayWidth = 0, displayHeight = 0; // 0 = the image size
    int dataOriginX = 0, dataOriginY = 0;    // where the image sits relative to the display origin
    float pixelAspect = 1.0f;
};

// Write a single-channel image as R, G and B (so any viewer shows grey), ZIP compressed.
void writeExrGrey(const std::string& path, const Image& img, bool halfFloat = true);
void writeExrGrey(const std::string& path, const Image& img, const ExrWriteOptions& opts);

} // namespace ptrack::io
