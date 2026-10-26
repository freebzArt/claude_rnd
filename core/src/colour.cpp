#include "ptrack/colour.h"

#include <cmath>

namespace ptrack {

const char* toString(TrackingCurve c) {
    switch (c) {
    case TrackingCurve::None: return "none";
    case TrackingCurve::ACEScct: return "acescct";
    }
    return "none";
}

bool parseTrackingCurve(const std::string& s, TrackingCurve& out) {
    if (s == "none") out = TrackingCurve::None;
    else if (s == "acescct") out = TrackingCurve::ACEScct;
    else return false;
    return true;
}

float applyCurve(TrackingCurve c, float lin) {
    switch (c) {
    case TrackingCurve::None: return lin;
    case TrackingCurve::ACEScct:
        // S-2016-001. Negative and tiny values use the linear toe segment.
        if (lin <= 0.0078125f) return 10.5402377416545f * lin + 0.0729055341958355f;
        return (std::log2(lin) + 9.72f) / 17.52f;
    }
    return lin;
}

void applyCurve(TrackingCurve c, Image& img) {
    if (c == TrackingCurve::None) return;
    float* p = img.data();
    const size_t n = static_cast<size_t>(img.width()) * img.height();
    for (size_t i = 0; i < n; ++i) p[i] = applyCurve(c, p[i]);
}

} // namespace ptrack
