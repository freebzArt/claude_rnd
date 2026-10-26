#pragma once

#include "ptrack/image.h"
#include "ptrack/motion.h"

#include <vector>

namespace ptrack {

// Anti-aliased coverage of a closed polygon (even-odd rule), sampled on a
// supersample x supersample grid per pixel. Coordinates follow
// docs/conventions.md. This is the seed of shape rasterisation; Bezier and
// X-spline shapes flatten to polygons before reaching it.
Image rasterizePolygon(int width, int height, const std::vector<Vec2>& points, int supersample = 4);

// Move each vertex of a convex polygon towards its centroid by `distance`
// pixels (negative grows it). Used to keep a matte clear of a plane's edges.
std::vector<Vec2> insetPolygon(const std::vector<Vec2>& points, double distance);

} // namespace ptrack
