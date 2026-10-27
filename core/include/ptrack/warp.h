#pragma once

#include "ptrack/image.h"
#include "ptrack/motion.h"

namespace ptrack {

// Resample `src` through a homography: out(p) = src(H^-1 p), bilinear.
// Pixels that map outside `src` get `fill`. Used for stabilised previews and tests.
Image warpImage(const Image& src, const Mat3& h, int width, int height, float fill = 0.0f);

// Conjugate a homography by a translation: T * H * T^-1 with T moving points by
// `offset`. Converts tracks between image-array and display-window coordinates
// when an image has overscan (array origin = display origin + offset).
Mat3 translateHomography(const Mat3& h, const Vec2& offset);

} // namespace ptrack
