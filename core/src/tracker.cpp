#include "ptrack/tracker.h"

#include <algorithm>
#include <cstdio>

namespace ptrack {

namespace {

struct MaskBounds {
    int w = 0, h = 0;
};

MaskBounds maskBounds(const Image& mask) {
    int minX = mask.width(), minY = mask.height(), maxX = -1, maxY = -1;
    for (int y = 0; y < mask.height(); ++y)
        for (int x = 0; x < mask.width(); ++x)
            if (mask.at(x, y) > 0.0f) {
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
            }
    if (maxX < 0) return {};
    return {maxX - minX + 1, maxY - minY + 1};
}

// Median match quality of the last few accepted frames in this direction.
double recentZncc(const std::vector<double>& history) {
    if (history.empty()) return 1.0;
    std::vector<double> h(history.end() - std::min<size_t>(history.size(), 5), history.end());
    std::nth_element(h.begin(), h.begin() + h.size() / 2, h.end());
    return h[h.size() / 2];
}

std::string pauseReason(const FrameDiagnostics& d, const TrackParams& p, double recent) {
    char buf[160];
    if (d.visibleFraction < p.pauseBelowVisible) {
        std::snprintf(buf, sizeof buf, "only %.0f%% of the region is inside the frame",
                      100.0 * d.visibleFraction);
        return buf;
    }
    if (d.inlierRatio < p.pauseBelowInliers) {
        std::snprintf(buf, sizeof buf,
                      "the solver rejected %.0f%% of the matte (occlusion or a large change)",
                      100.0 * (1.0 - d.inlierRatio));
        return buf;
    }
    if (d.zncc < p.pauseBelowZncc) {
        std::snprintf(buf, sizeof buf, "match quality dropped (ZNCC %.2f, inliers %.0f%%)", d.zncc,
                      100.0 * d.inlierRatio);
        return buf;
    }
    if (d.zncc < recent - p.pauseOnZnccDrop) {
        std::snprintf(buf, sizeof buf, "match quality fell suddenly (ZNCC %.2f, recent %.2f)%s", d.zncc,
                      recent, d.converged ? "" : " and the solver did not converge");
        return buf;
    }
    return {};
}

} // namespace

const char* toString(ReferenceStrategy s) {
    switch (s) {
    case ReferenceStrategy::Keyframe: return "keyframe";
    case ReferenceStrategy::FrameToFrame: return "frame-to-frame";
    case ReferenceStrategy::Hybrid: return "hybrid";
    }
    return "hybrid";
}

bool parseReferenceStrategy(const std::string& s, ReferenceStrategy& out) {
    for (ReferenceStrategy r :
         {ReferenceStrategy::Keyframe, ReferenceStrategy::FrameToFrame, ReferenceStrategy::Hybrid}) {
        if (s == toString(r)) {
            out = r;
            return true;
        }
    }
    return false;
}

const TrackedFrame* TrackResult::find(int frame) const {
    auto it = std::lower_bound(frames.begin(), frames.end(), frame,
                               [](const TrackedFrame& f, int v) { return f.frame < v; });
    return (it != frames.end() && it->frame == frame) ? &*it : nullptr;
}

std::array<Vec2, 4> cornersAt(const TrackedFrame& f, const std::array<Vec2, 4>& surface) {
    std::array<Vec2, 4> out;
    for (int i = 0; i < 4; ++i) out[i] = applyH(f.h, surface[i]);
    return out;
}

TrackResult trackRegion(const FrameSource& source, int referenceFrame, const Image& referenceMask,
                        int first, int last, const TrackParams& params) {
    TrackResult result;
    result.referenceFrame = referenceFrame;

    const Image refImg = source(referenceFrame);
    if (refImg.empty()) {
        result.pausedAt = referenceFrame;
        result.pauseReason = "reference frame could not be read";
        return result;
    }
    const MaskBounds mb = maskBounds(referenceMask);
    if (mb.w == 0) {
        result.pausedAt = referenceFrame;
        result.pauseReason = "the tracking matte is empty";
        return result;
    }
    const int levels = params.pyramidLevels > 0 ? params.pyramidLevels : autoLevels(mb.w, mb.h);
    const Pyramid refPyr(refImg, levels);
    const Template refTmpl = buildTemplate(refPyr, referenceMask, params.solver.minMaskWeight);

    TrackedFrame ref;
    ref.frame = referenceFrame;
    {
        // Self-solve for the reference frame's diagnostics (should be exact).
        const SolveResult s = solveESM(refTmpl, refPyr, Mat3::Identity(), 1.0, 0.0, params.solver);
        ref.diag = s.diag;
        ref.h = Mat3::Identity();
    }
    std::vector<TrackedFrame> out{ref};

    for (int dir : {+1, -1}) {
        const int end = dir > 0 ? last : first;
        Mat3 hPrev = Mat3::Identity(), hPrev2 = Mat3::Identity();
        bool havePrev2 = false;
        double kfGain = 1.0, kfBias = 0.0;
        Pyramid prevPyr = refPyr;
        std::vector<double> znccHistory;
        std::vector<float> prevWeights; // robust weights of the previous solve, reference samples

        for (int f = referenceFrame + dir; dir > 0 ? f <= end : f >= end; f += dir) {
            const Image img = source(f);
            if (img.empty()) {
                if (!result.pausedAt) {
                    result.pausedAt = f;
                    result.pauseReason = "frame could not be read";
                }
                break;
            }
            const Pyramid pyr(img, levels);
            const Mat3 hPred = (params.constantVelocity && havePrev2)
                                   ? predictConstantVelocity(hPrev2, hPrev)
                                   : hPrev;

            TrackedFrame tf;
            tf.frame = f;
            SolveResult accepted;
            if (params.strategy == ReferenceStrategy::Keyframe) {
                accepted = solveESM(refTmpl, pyr, hPred, kfGain, kfBias, params.solver);
                tf.refinedOnReference = true;
            } else {
                // Previous frame's appearance, resampled into reference coordinates.
                // Pixels rejected last frame (occluders) are left out of its template.
                const Template prevTmpl =
                    weightTemplate(resampleTemplate(refTmpl, prevPyr, hPrev), prevWeights);
                accepted = solveESM(prevTmpl, pyr, hPred, 1.0, 0.0, params.solver);
                if (params.strategy == ReferenceStrategy::Hybrid) {
                    const SolveResult r =
                        solveESM(refTmpl, pyr, accepted.h, kfGain, kfBias, params.solver);
                    if (r.diag.converged && r.diag.zncc >= params.refineMinZncc &&
                        r.diag.visibleFraction >= params.pauseBelowVisible) {
                        accepted = r;
                        tf.refinedOnReference = true;
                    }
                }
            }
            tf.h = accepted.h;
            tf.gain = accepted.gain;
            tf.bias = accepted.bias;
            tf.diag = accepted.diag;
            if (tf.refinedOnReference) {
                kfGain = accepted.gain;
                kfBias = accepted.bias;
            }

            const std::string why = pauseReason(tf.diag, params, recentZncc(znccHistory));
            if (!why.empty()) {
                if (!result.pausedAt) {
                    result.pausedAt = f;
                    result.pauseReason = why;
                }
                break;
            }
            out.push_back(tf);
            znccHistory.push_back(tf.diag.zncc);
            prevWeights = accepted.robustWeights;
            hPrev2 = hPrev;
            hPrev = tf.h;
            havePrev2 = true;
            prevPyr = pyr;
        }
    }

    std::sort(out.begin(), out.end(),
              [](const TrackedFrame& a, const TrackedFrame& b) { return a.frame < b.frame; });
    result.frames = std::move(out);
    return result;
}

} // namespace ptrack
