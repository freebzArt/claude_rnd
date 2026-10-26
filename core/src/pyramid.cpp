#include "ptrack/pyramid.h"

#include <algorithm>
#include <cmath>

namespace ptrack {

namespace {

inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

} // namespace

Image downsample2(const Image& src) {
    const int w = src.width() / 2, h = src.height() / 2;
    if (w < 1 || h < 1) return {};
    // Horizontal pass: new x centre 2i+1 sits between source pixels 2i and 2i+1.
    Image tmp(w, src.height());
    for (int y = 0; y < src.height(); ++y) {
        const float* s = src.row(y);
        float* d = tmp.row(y);
        const int last = src.width() - 1;
        for (int i = 0; i < w; ++i) {
            const int x = 2 * i;
            d[i] = (s[clampi(x - 1, 0, last)] + 3.0f * s[x] + 3.0f * s[clampi(x + 1, 0, last)] +
                    s[clampi(x + 2, 0, last)]) *
                   0.125f;
        }
    }
    Image dst(w, h);
    const int last = src.height() - 1;
    for (int j = 0; j < h; ++j) {
        const int y = 2 * j;
        const float* r0 = tmp.row(clampi(y - 1, 0, last));
        const float* r1 = tmp.row(y);
        const float* r2 = tmp.row(clampi(y + 1, 0, last));
        const float* r3 = tmp.row(clampi(y + 2, 0, last));
        float* d = dst.row(j);
        for (int i = 0; i < w; ++i) d[i] = (r0[i] + 3.0f * r1[i] + 3.0f * r2[i] + r3[i]) * 0.125f;
    }
    return dst;
}

Image downsampleMask2(const Image& src) {
    const int w = src.width() / 2, h = src.height() / 2;
    if (w < 1 || h < 1) return {};
    Image dst(w, h);
    for (int j = 0; j < h; ++j) {
        const float* a = src.row(2 * j);
        const float* b = src.row(2 * j + 1);
        float* d = dst.row(j);
        for (int i = 0; i < w; ++i)
            d[i] = 0.25f * (a[2 * i] + a[2 * i + 1] + b[2 * i] + b[2 * i + 1]);
    }
    return dst;
}

void computeGradients(const Image& src, Image& gx, Image& gy) {
    const int w = src.width(), h = src.height();
    gx = Image(w, h);
    gy = Image(w, h);
    if (w < 2 || h < 2) return;
    for (int y = 0; y < h; ++y) {
        const float* s = src.row(y);
        float* dx = gx.row(y);
        dx[0] = s[1] - s[0];
        for (int x = 1; x < w - 1; ++x) dx[x] = 0.5f * (s[x + 1] - s[x - 1]);
        dx[w - 1] = s[w - 1] - s[w - 2];

        const float* up = src.row(y > 0 ? y - 1 : y);
        const float* dn = src.row(y < h - 1 ? y + 1 : y);
        const float k = (y > 0 && y < h - 1) ? 0.5f : 1.0f;
        float* dy = gy.row(y);
        for (int x = 0; x < w; ++x) dy[x] = k * (dn[x] - up[x]);
    }
}

Image gaussianBlur(const Image& src, double sigma) {
    if (sigma <= 0.0 || src.empty()) return src;
    const int radius = std::max(1, static_cast<int>(std::ceil(3.0 * sigma)));
    std::vector<float> k(2 * radius + 1);
    double sum = 0.0;
    for (int i = -radius; i <= radius; ++i) {
        const double v = std::exp(-0.5 * i * i / (sigma * sigma));
        k[i + radius] = static_cast<float>(v);
        sum += v;
    }
    for (float& v : k) v = static_cast<float>(v / sum);

    const int w = src.width(), h = src.height();
    Image tmp(w, h), dst(w, h);
    for (int y = 0; y < h; ++y) {
        const float* s = src.row(y);
        float* d = tmp.row(y);
        for (int x = 0; x < w; ++x) {
            float acc = 0.0f;
            for (int i = -radius; i <= radius; ++i) acc += k[i + radius] * s[clampi(x + i, 0, w - 1)];
            d[x] = acc;
        }
    }
    for (int y = 0; y < h; ++y) {
        float* d = dst.row(y);
        for (int x = 0; x < w; ++x) {
            float acc = 0.0f;
            for (int i = -radius; i <= radius; ++i)
                acc += k[i + radius] * tmp.at(x, clampi(y + i, 0, h - 1));
            d[x] = acc;
        }
    }
    return dst;
}

void Pyramid::build(const Image& base, int levels, bool withGradients) {
    levels_.clear();
    levels_.reserve(levels);
    Image cur = base;
    double scale = 1.0;
    for (int l = 0; l < levels && !cur.empty(); ++l) {
        PyramidLevel lv;
        lv.scale = scale;
        if (withGradients) computeGradients(cur, lv.gx, lv.gy);
        Image next = (l + 1 < levels) ? downsample2(cur) : Image();
        lv.img = std::move(cur);
        levels_.push_back(std::move(lv));
        cur = std::move(next);
        scale *= 0.5;
    }
}

int autoLevels(int regionWidth, int regionHeight, int minSize, int maxLevels) {
    int levels = 1;
    int s = std::min(regionWidth, regionHeight);
    while (levels < maxLevels && s / 2 >= minSize) {
        s /= 2;
        ++levels;
    }
    return levels;
}

} // namespace ptrack
