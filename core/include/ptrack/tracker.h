#pragma once

#include "ptrack/preprocess.h"
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
    PreprocessParams preprocess;    // lighting normalisation, applied per pyramid level
    ReferenceStrategy strategy = ReferenceStrategy::Hybrid;
    int pyramidLevels = 0;          // 0 = automatic from the region size
    bool constantVelocity = true;   // predict the next frame from the last two
    double refineMinZncc = 0.8;     // hybrid: a keyframe's first refinement must score this; later
                                    // ones may decline gradually to pauseBelowZncc
    double pauseBelowZncc = 0.5;    // auto-pause when the trusted pixels correlate worse
    double pauseBelowInliers = 0.3; // auto-pause when the solver rejects more of the matte
    double pauseBelowVisible = 0.2; // auto-pause when less of the region is in frame
    double pauseOnZnccDrop = 0.15;  // auto-pause when quality falls this far below recent frames
    bool lightingFallback = true;   // hybrid: retry the reference on normalised images when it stops matching
    double fallbackCorrection = 0.3; // ... and take that answer once frame-to-frame disagrees by more (px)
    bool carryRejections = true;    // leave pixels rejected on one frame out of the next frame's template
    int reanchorAfter = 3;          // hybrid: add a keyframe after this many frames match none (0 = never)
    double reanchorMinZncc = 0.95;  // ... but only from a frame-to-frame solve at least this good
};

// Returns the tracking-space image for a frame, or an empty image if unavailable.
using FrameSource = std::function<Image(int frame)>;

struct TrackedFrame {
    int frame = 0;
    Mat3 h = Mat3::Identity(); // reference coords -> this frame's coords
    double gain = 1.0, bias = 0.0;
    FrameDiagnostics diag;
    bool refinedOnReference = false; // hybrid: refined on a keyframe (the reference or an added one)
    int keyframe = 0;                // keyframe this frame was aligned to, if refined
    bool newKeyframe = false;        // hybrid: this frame became a keyframe
    bool lightingFallback = false;   // hybrid: corrected by the lighting-normalised reference solve
};

struct TrackResult {
    int referenceFrame = 0;
    std::vector<TrackedFrame> frames; // sorted by frame number, reference included
    std::vector<int> keyframes;       // the reference plus any added by re-anchoring, sorted
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
