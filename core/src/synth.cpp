#include "ptrack/synth.h"

#include "ptrack/raster.h"

#include <Eigen/Dense>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>

namespace ptrack {

namespace {

constexpr double kPi = 3.14159265358979323846;

inline uint32_t hash2(int x, int y, uint32_t seed) {
    uint32_t h = static_cast<uint32_t>(x) * 0x8da6b343u ^ static_cast<uint32_t>(y) * 0xd8163841u ^
                 seed * 0xcb1ab31fu;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

inline double lattice(int x, int y, uint32_t seed) { return (hash2(x, y, seed) & 0xffffff) / 16777215.0; }

inline double smooth(double t) { return t * t * t * (t * (t * 6 - 15) + 10); }

double valueNoise(double x, double y, uint32_t seed) {
    const double fx = std::floor(x), fy = std::floor(y);
    const int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
    const double tx = smooth(x - fx), ty = smooth(y - fy);
    const double a = lattice(ix, iy, seed), b = lattice(ix + 1, iy, seed);
    const double c = lattice(ix, iy + 1, seed), d = lattice(ix + 1, iy + 1, seed);
    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
}

// Signage-like texture: multi-octave noise plus soft-edged panel lines.
double planeTexture(double u, double v, uint32_t seed) {
    double n = 0.0, amp = 0.5, freq = 1.0 / 48.0;
    for (int o = 0; o < 4; ++o) {
        n += amp * valueNoise(u * freq, v * freq, seed + o);
        amp *= 0.55;
        freq *= 2.0;
    }
    const double lu = std::abs(std::fmod(u, 40.0) - 20.0), lv = std::abs(std::fmod(v, 28.0) - 14.0);
    const double line = std::exp(-0.5 * std::pow(std::max(lu, lv) - 19.0, 2.0) / 0.8);
    return 0.03 + 0.8 * n * (1.0 - 0.6 * line);
}

double backgroundTexture(double x, double y, uint32_t seed) {
    double n = 0.0, amp = 0.5, freq = 1.0 / 20.0;
    for (int o = 0; o < 4; ++o) {
        n += amp * valueNoise(x * freq, y * freq, seed + 101 + o);
        amp *= 0.5;
        freq *= 2.0;
    }
    return 0.02 + 0.5 * n;
}

} // namespace

SynthShot makeSynthShot(const SynthParams& p) {
    SynthShot shot;
    shot.startFrame = p.startFrame;
    shot.referenceFrame = p.startFrame + p.referenceOffset;
    const double W = p.width, H = p.height;
    const double planeW = 400.0, planeH = 260.0; // plane texture extent (texture pixels)
    const std::array<Vec2, 4> planeRect = {Vec2(0, planeH), Vec2(planeW, planeH), Vec2(planeW, 0),
                                           Vec2(0, 0)}; // BL, BR, TR, TL in texture coords (y down)

    // Corner paths: a base quad with perspective, global drift, rotation and per-corner wobble.
    auto cornersAt = [&](int i) {
        const double t = static_cast<double>(i);
        const Vec2 c(W * 0.5 + p.motion * (1.6 * t + 6.0 * std::sin(t * 0.21)),
                     H * 0.5 + p.motion * (0.6 * t + 4.0 * std::sin(t * 0.13 + 1.0)));
        const double ang = p.motion * 0.004 * t;
        const double sc = 1.0 + p.motion * 0.003 * t;
        const std::array<Vec2, 4> base = {Vec2(-0.26 * W, 0.24 * H), Vec2(0.24 * W, 0.28 * H),
                                          Vec2(0.21 * W, -0.25 * H), Vec2(-0.23 * W, -0.21 * H)};
        std::array<Vec2, 4> out;
        for (int k = 0; k < 4; ++k) {
            const Vec2 wob(std::sin(t * 0.17 + k * 1.3), std::cos(t * 0.11 + k * 0.7));
            const Vec2 b = base[k] * sc;
            const Vec2 r(std::cos(ang) * b.x() - std::sin(ang) * b.y(),
                         std::sin(ang) * b.x() + std::cos(ang) * b.y());
            out[k] = c + r + p.motion * 3.0 * wob;
        }
        return out;
    };

    std::mt19937 rng(p.seed);
    std::normal_distribution<float> noise(0.0f, static_cast<float>(p.noiseSigma));
    const uint32_t seed = p.seed * 7919u;
    const int ref = p.referenceOffset;
    const Mat3 planeToRef = homographyFromCorners(planeRect, cornersAt(ref));
    const Mat3 refToPlane = planeToRef.inverse();

    constexpr int ss = 2; // supersampling per axis
    for (int i = 0; i < p.frames; ++i) {
        const Mat3 planeToImg = homographyFromCorners(planeRect, cornersAt(i));
        const Mat3 imgToPlane = planeToImg.inverse();
        shot.truth.push_back(normalizeH(planeToImg * refToPlane));

        const double gain = 1.0 + p.flicker * std::sin(i * 0.9) + 0.5 * p.flicker * std::sin(i * 2.3);
        const Vec2 bgShift(-0.8 * i, 0.3 * i); // background moves differently from the plane
        const double occT = (p.frames > 1) ? static_cast<double>(i) / (p.frames - 1) : 0.0;
        const Vec2 occC(W * (0.15 + 0.7 * occT), H * (0.35 + 0.2 * std::sin(occT * kPi)));
        const double occR = 0.09 * W;
        const bool occOn = p.occluder && occT > 0.25 && occT < 0.85;
        const double morphT = p.morph * occT; // occT is also the shot's 0..1 timeline

        Image img(p.width, p.height);
        for (int y = 0; y < p.height; ++y) {
            for (int x = 0; x < p.width; ++x) {
                double acc = 0.0;
                for (int sy = 0; sy < ss; ++sy) {
                    for (int sx = 0; sx < ss; ++sx) {
                        const Vec2 q(x + (sx + 0.5) / ss, y + (sy + 0.5) / ss);
                        double val;
                        if (occOn && (q - occC).norm() < occR) {
                            val = 0.6 + 0.3 * valueNoise(q.x() / 6.0, q.y() / 6.0, seed + 999);
                        } else {
                            const Vec2 uv = applyH(imgToPlane, q);
                            if (uv.x() >= 0 && uv.x() < planeW && uv.y() >= 0 && uv.y() < planeH) {
                                val = gain * ((1.0 - morphT) * planeTexture(uv.x(), uv.y(), seed) +
                                              morphT * planeTexture(uv.x(), uv.y(), seed + 4242));
                                if (p.shadow > 0.0) {
                                    // Shadow edge sweeping left to right across the plane.
                                    const double edge = (-0.15 + 1.3 * occT) * planeW;
                                    const double t = std::clamp((edge - uv.x()) / (0.08 * planeW) + 0.5, 0.0, 1.0);
                                    val *= 1.0 - p.shadow * t * t * (3.0 - 2.0 * t);
                                }
                            } else {
                                val = backgroundTexture(q.x() - bgShift.x(), q.y() - bgShift.y(), seed);
                            }
                        }
                        acc += val;
                    }
                }
                float v = static_cast<float>(acc / (ss * ss));
                if (p.noiseSigma > 0.0) v += noise(rng);
                img.at(x, y) = v;
            }
        }
        shot.frames.push_back(std::move(img));
    }

    shot.surface = cornersAt(ref);
    const std::vector<Vec2> quad(shot.surface.begin(), shot.surface.end());
    shot.mask = rasterizePolygon(p.width, p.height, insetPolygon(quad, 4.0));
    return shot;
}

} // namespace ptrack
