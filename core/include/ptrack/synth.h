#pragma once

#include "ptrack/image.h"
#include "ptrack/motion.h"

#include <array>
#include <vector>

namespace ptrack {

// Synthetic shots with exact ground truth, for the accuracy harness.
// A procedurally textured plane moves over a background that moves
// differently, so the matte matters. Textures are evaluated analytically and
// supersampled, so frames contain no resampling error of their own.
struct SynthParams {
    int width = 640, height = 360;
    int frames = 40;
    int startFrame = 1001;
    int referenceOffset = 0;     // reference frame = startFrame + referenceOffset
    double motion = 1.0;         // scales all corner motion
    double noiseSigma = 0.0;     // additive Gaussian noise (linear units)
    double flicker = 0.0;        // relative gain flicker amplitude
    bool occluder = false;       // a disc crosses the plane mid-shot
    double morph = 0.0;          // the plane's texture blends this far towards another over the shot
    double shadow = 0.0;         // depth of a soft shadow edge that sweeps across the plane (0..1)
    unsigned seed = 1;
};

struct SynthShot {
    std::vector<Image> frames;        // linear-light luminance
    std::vector<Mat3> truth;          // reference coords -> frame coords, per frame
    std::array<Vec2, 4> surface;      // plane corners in the reference frame (BL, BR, TR, TL)
    Image mask;                       // matte of the plane in the reference frame (inset)
    int startFrame = 1001;
    int referenceFrame = 1001;

    const Image& frame(int f) const { return frames[f - startFrame]; }
    const Mat3& truthAt(int f) const { return truth[f - startFrame]; }
};

SynthShot makeSynthShot(const SynthParams& params);

} // namespace ptrack
