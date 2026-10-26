#include "ptrack/raster.h"

#include <algorithm>
#include <cmath>

namespace ptrack {

Image rasterizePolygon(int width, int height, const std::vector<Vec2>& points, int supersample) {
    Image out(width, height);
    if (points.size() < 3 || width <= 0 || height <= 0) return out;
    supersample = std::max(1, supersample);
    const double inv = 1.0 / supersample;
    const float weight = 1.0f / static_cast<float>(supersample * supersample);
    const size_t n = points.size();

    double minY = points[0].y(), maxY = points[0].y();
    for (const Vec2& p : points) {
        minY = std::min(minY, p.y());
        maxY = std::max(maxY, p.y());
    }
    const int y0 = std::max(0, static_cast<int>(std::floor(minY)));
    const int y1 = std::min(height - 1, static_cast<int>(std::ceil(maxY)));

    std::vector<double> xs;
    for (int py = y0; py <= y1; ++py) {
        for (int sy = 0; sy < supersample; ++sy) {
            const double y = py + (sy + 0.5) * inv;
            xs.clear();
            for (size_t i = 0; i < n; ++i) {
                const Vec2& a = points[i];
                const Vec2& b = points[(i + 1) % n];
                if ((a.y() <= y) == (b.y() <= y)) continue;
                xs.push_back(a.x() + (y - a.y()) / (b.y() - a.y()) * (b.x() - a.x()));
            }
            std::sort(xs.begin(), xs.end());
            float* row = out.row(py);
            for (size_t k = 0; k + 1 < xs.size(); k += 2) {
                // Sub-sample columns whose centres fall inside [xs[k], xs[k+1]).
                const long s0 = static_cast<long>(std::ceil(xs[k] * supersample - 0.5));
                const long s1 = static_cast<long>(std::ceil(xs[k + 1] * supersample - 0.5));
                for (long s = std::max(0L, s0); s < s1 && s < static_cast<long>(width) * supersample; ++s)
                    row[s / supersample] += weight;
            }
        }
    }
    return out;
}

std::vector<Vec2> insetPolygon(const std::vector<Vec2>& points, double distance) {
    if (points.empty()) return points;
    Vec2 c = Vec2::Zero();
    for (const Vec2& p : points) c += p;
    c /= static_cast<double>(points.size());
    std::vector<Vec2> out;
    out.reserve(points.size());
    for (const Vec2& p : points) {
        const Vec2 d = c - p;
        const double len = d.norm();
        out.push_back(len > distance ? p + d * (distance / len) : c);
    }
    return out;
}

} // namespace ptrack
