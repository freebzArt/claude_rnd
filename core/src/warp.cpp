#include "ptrack/warp.h"

#include <Eigen/Dense>

namespace ptrack {

Image warpImage(const Image& src, const Mat3& h, int width, int height, float fill) {
    Image out(width, height, fill);
    const Mat3 inv = h.inverse();
    for (int y = 0; y < height; ++y) {
        float* row = out.row(y);
        for (int x = 0; x < width; ++x) {
            const Vec2 q = applyH(inv, Vec2(x + 0.5, y + 0.5));
            float v;
            if (src.sample(q.x(), q.y(), v)) row[x] = v;
        }
    }
    return out;
}

Mat3 translateHomography(const Mat3& h, const Vec2& offset) {
    Mat3 t = Mat3::Identity(), ti = Mat3::Identity();
    t(0, 2) = offset.x();
    t(1, 2) = offset.y();
    ti(0, 2) = -offset.x();
    ti(1, 2) = -offset.y();
    return normalizeH(t * h * ti);
}

} // namespace ptrack
