#pragma once

#include "ptrack/image.h"

#include <string>

namespace ptrack {

// Lighting-robust preprocessing, applied to every pyramid level in that
// level's own pixels (so coarse levels keep their detail for large motions).
// Run it on a log-encoded tracking image (the ACEScct curve or an OCIO log
// space): a shadow or exposure change multiplies brightness, which becomes an
// additive, slowly varying offset in log space, and that is exactly what these
// filters remove.
enum class Normalize {
    None,
    Bandpass,      // difference of Gaussians: keeps detail between fineSigma and coarseSigma
    LocalContrast, // subtract the local mean and divide by the local spread (radius coarseSigma)
};

struct PreprocessParams {
    Normalize mode = Normalize::None;
    double coarseSigma = 6.0; // pixels: structure larger than this is treated as lighting
    double fineSigma = 0.0;   // pixels: Bandpass also removes detail finer than this (grain); 0 = keep
};

const char* toString(Normalize n);
bool parseNormalize(const std::string& s, Normalize& out);

// Apply to one image (one pyramid level).
Image preprocess(const Image& img, const PreprocessParams& params);

} // namespace ptrack
