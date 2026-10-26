#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

namespace ptrack {

// Single-channel float image in internal image coordinates (docs/conventions.md):
// pixel (i, j) covers [i, i+1) x [j, j+1), so its centre is (i + 0.5, j + 0.5); y points down.
class Image {
public:
    Image() = default;
    Image(int width, int height, float fill = 0.0f)
        : w_(width), h_(height), px_(static_cast<size_t>(width) * height, fill) {}

    int width() const { return w_; }
    int height() const { return h_; }
    bool empty() const { return px_.empty(); }

    float& at(int x, int y) { return px_[static_cast<size_t>(y) * w_ + x]; }
    float at(int x, int y) const { return px_[static_cast<size_t>(y) * w_ + x]; }
    float* row(int y) { return px_.data() + static_cast<size_t>(y) * w_; }
    const float* row(int y) const { return px_.data() + static_cast<size_t>(y) * w_; }
    float* data() { return px_.data(); }
    const float* data() const { return px_.data(); }

    // Bilinear sample at continuous image coordinates. Returns false when the
    // four neighbouring pixel centres are not all inside the image.
    bool sample(double x, double y, float& value) const {
        const double fx = x - 0.5, fy = y - 0.5;
        const double x0f = std::floor(fx), y0f = std::floor(fy);
        const int x0 = static_cast<int>(x0f), y0 = static_cast<int>(y0f);
        if (x0 < 0 || y0 < 0 || x0 + 1 >= w_ || y0 + 1 >= h_) return false;
        const float ax = static_cast<float>(fx - x0f), ay = static_cast<float>(fy - y0f);
        const float* r0 = row(y0) + x0;
        const float* r1 = r0 + w_;
        const float top = r0[0] + ax * (r0[1] - r0[0]);
        const float bot = r1[0] + ax * (r1[1] - r1[0]);
        value = top + ay * (bot - top);
        return true;
    }

private:
    int w_ = 0, h_ = 0;
    std::vector<float> px_;
};

} // namespace ptrack
