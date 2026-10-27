#pragma once

#include "ptrack/colour.h"
#include "ptrack/synth.h"
#include "ptrack/tracker.h"

#include <algorithm>

namespace ptrack::test {

struct CornerError {
    double mean = 0.0, max = 0.0;
    int frames = 0;
};

// Surface-corner error of a track against synthetic ground truth, in pixels.
inline CornerError cornerError(const TrackResult& track, const SynthShot& shot) {
    CornerError e;
    double sum = 0.0;
    int n = 0;
    for (const TrackedFrame& f : track.frames) {
        const Mat3& truth = shot.truthAt(f.frame);
        for (const Vec2& c : shot.surface) {
            const double d = (applyH(f.h, c) - applyH(truth, c)).norm();
            sum += d;
            e.max = std::max(e.max, d);
            ++n;
        }
        ++e.frames;
    }
    e.mean = n ? sum / n : 0.0;
    return e;
}

inline FrameSource sourceFor(const SynthShot& shot, TrackingCurve curve = TrackingCurve::None) {
    return [&shot, curve](int f) -> Image {
        if (f < shot.startFrame || f >= shot.startFrame + static_cast<int>(shot.frames.size())) return {};
        Image img = shot.frame(f);
        applyCurve(curve, img);
        return img;
    };
}

} // namespace ptrack::test
