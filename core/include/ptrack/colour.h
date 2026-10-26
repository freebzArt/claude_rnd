#pragma once

#include "ptrack/image.h"

#include <string>

namespace ptrack {

// Built-in tracking-space curves (docs/conventions.md, section 6). OCIO
// transforms replace these when a config is available.
enum class TrackingCurve {
    None,    // use values as they are (display-referred sources)
    ACEScct, // scene-linear in, ACEScct-encoded out: tames highlights
};

const char* toString(TrackingCurve c);
bool parseTrackingCurve(const std::string& s, TrackingCurve& out);

float applyCurve(TrackingCurve c, float linear);

// Rec.709 luminance weights.
inline float luma709(float r, float g, float b) { return 0.2126f * r + 0.7152f * g + 0.0722f * b; }

// Apply a curve to every pixel in place.
void applyCurve(TrackingCurve c, Image& img);

} // namespace ptrack
