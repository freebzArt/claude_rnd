#pragma once

#include "ptrack/solver.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ptrack {

enum class ReferenceStrategy {
    Keyframe,     // always align to the reference template: no drift, less tolerant of change
    FrameToFrame, // align to the previous frame: tolerant of change, accumulates drift
    Hybrid,       // frame-to-frame, then refine against the reference when it still matches
};

const char* toString(ReferenceStrategy s);
bool parseReferenceStrategy(const std::string& s, ReferenceStrategy& out);

struct TrackParams {
    SolverParams solver;
    ReferenceStrategy strategy = ReferenceStrategy::Hybrid;
    int pyramidLevels = 0;          // 0 = automatic from the region size
    bool constantVelocity = true;   // predict the next frame from the last two
    double refineMinZncc = 0.8;     // hybrid: accept the reference refinement above this
    double pauseBelowZncc = 0.5;    // auto-pause when the trusted pixels correlate worse
    double pauseBelowInliers = 0.3; // auto-pause when the solver rejects more of the matte
    double pauseBelowVisible = 0.2; // auto-pause when less of the region is in frame
    double pauseOnZnccDrop = 0.15;  // auto-pause when quality falls this far below recent frames
};

// Returns the tracking-space image for a frame, or an empty image if unavailable.
using FrameSource = std::function<Image(int frame)>;

struct TrackedFrame {
    int frame = 0;
    Mat3 h = Mat3::Identity(); // reference coords -> this frame's coords
    double gain = 1.0, bias = 0.0;
    FrameDiagnostics diag;
    bool refinedOnReference = false; // hybrid: reference refinement accepted
};

struct TrackResult {
    int referenceFrame = 0;
    std::vector<TrackedFrame> frames; // sorted by frame number, reference included
    std::optional<int> pausedAt;      // first frame that failed, if tracking paused
    std::string pauseReason;

    const TrackedFrame* find(int frame) const;
};

// Track a matte-weighted region from the reference frame to every frame in
// [first, last], forward and then backward. Stops a direction at the first
// frame that fails the auto-pause tests, and records why.
TrackResult trackRegion(const FrameSource& source, int referenceFrame, const Image& referenceMask,
                        int first, int last, const TrackParams& params);

// Map the surface corners (given in reference-frame coordinates) through a frame's homography.
std::array<Vec2, 4> cornersAt(const TrackedFrame& f, const std::array<Vec2, 4>& surface);

} // namespace ptrack
