#pragma once

#include "ptrack/image.h"

#include <functional>
#include <vector>

namespace ptrack {

// One pyramid level with precomputed gradients. Level coordinates are image
// coordinates scaled by `scale` (= 2^-level), exactly, thanks to edge-origin pixels.
struct PyramidLevel {
    Image img, gx, gy;
    double scale = 1.0;

    // Bilinear sample of value and gradient. Returns false outside the valid area.
    bool sampleWithGradient(double x, double y, float& v, float& dx, float& dy) const {
        return img.sample(x, y, v) && gx.sample(x, y, dx) && gy.sample(x, y, dy);
    }
};

// Optional per-level filter (e.g. lighting normalisation). Levels are
// downsampled from the unfiltered image; each is filtered in its own pixels.
using LevelFilter = std::function<Image(const Image&)>;

class Pyramid {
public:
    Pyramid() = default;
    Pyramid(const Image& base, int levels, bool withGradients = true, const LevelFilter& filter = {}) {
        build(base, levels, withGradients, filter);
    }

    void build(const Image& base, int levels, bool withGradients = true, const LevelFilter& filter = {});

    int levels() const { return static_cast<int>(levels_.size()); }
    const PyramidLevel& level(int l) const { return levels_[l]; }

private:
    std::vector<PyramidLevel> levels_;
};

// Halve resolution with the even binomial filter [1 3 3 1]/8, which is centred on
// the new pixel's centre. Output size is floor(w/2) x floor(h/2).
Image downsample2(const Image& src);

// Downsample a weight mask by plain 2x2 averaging, so coverage is preserved.
Image downsampleMask2(const Image& src);

// Central-difference gradients in pixels^-1, one-sided at the borders.
void computeGradients(const Image& src, Image& gx, Image& gy);

// Separable Gaussian blur with clamped borders. sigma <= 0 returns a copy.
Image gaussianBlur(const Image& src, double sigma);

// Number of levels that keeps the coarsest level of a region at least
// `minSize` pixels on its shorter side.
int autoLevels(int regionWidth, int regionHeight, int minSize = 24, int maxLevels = 6);

} // namespace ptrack
