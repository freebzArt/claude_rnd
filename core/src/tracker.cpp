#include "ptrack/tracker.h"

#include <algorithm>
#include <climits>
#include <map>
#include <optional>
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
    LevelFilter filter;
    if (params.preprocess.mode != Normalize::None)
        filter = [pp = params.preprocess](const Image& im) { return preprocess(im, pp); };
    const Pyramid refPyr(refImg, levels, true, filter);
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

    // Lighting fallback (hybrid, no normalisation chosen): when no keyframe
    // matches the plain image, retry the reference on lighting-normalised
    // pyramids. Normal frames keep full accuracy; shadows and exposure shifts
    // stay anchored instead of drifting frame to frame. Built on first use.
    const bool fallback = params.lightingFallback && params.strategy == ReferenceStrategy::Hybrid &&
                          params.preprocess.mode == Normalize::None;
    const LevelFilter litFilter = [](const Image& im) {
        PreprocessParams pp;
        pp.mode = Normalize::LocalContrast;
        return preprocess(im, pp);
    };
    Template litTmpl;

    for (int dir : {+1, -1}) {
        const int end = dir > 0 ? last : first;
        Mat3 hPrev = Mat3::Identity(), hPrev2 = Mat3::Identity();
        bool havePrev2 = false;
        double kfGain = 1.0, kfBias = 0.0;
        Pyramid prevPyr = refPyr;
        // Hybrid keyframes: the reference first, then any added on the way when
        // the reference stops matching. All live in reference coordinates.
        struct Anchor {
            Template tmpl;
            int frame;
            double gain = 1.0, bias = 0.0;
            std::vector<double> zncc; // scores of accepted refinements, for the drop test
        };
        std::vector<Anchor> anchors{{refTmpl, referenceFrame, 1.0, 0.0, {}}};
        int unanchored = 0;
        Anchor lit{{}, referenceFrame, 1.0, 0.0, {}};
        // Recent scores per kind of solve (each keyframe, the lighting check,
        // frame-to-frame): scores against different templates aren't comparable.
        std::map<int, std::vector<double>> znccHistory;
        constexpr int kFrameToFrame = INT_MIN, kLit = INT_MIN + 1;
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
            const Pyramid pyr(img, levels, true, filter);
            const Mat3 hPred = (params.constantVelocity && havePrev2)
                                   ? predictConstantVelocity(hPrev2, hPrev)
                                   : hPrev;

            TrackedFrame tf;
            tf.frame = f;
            SolveResult accepted;
            bool acceptedIsFrameToFrame = params.strategy != ReferenceStrategy::Keyframe;
            std::optional<FrameDiagnostics> verifiedBy; // lighting check that confirmed frame-to-frame
            tf.keyframe = referenceFrame;
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
                    // Refine on the original reference if it still matches, else on
                    // the newest keyframe. Either stops frame-to-frame drift.
                    std::vector<size_t> order{0};
                    if (anchors.size() > 1) order.push_back(anchors.size() - 1);
                    for (size_t ai : order) {
                        Anchor& an = anchors[ai];
                        const SolveResult r = solveESM(an.tmpl, pyr, accepted.h, an.gain, an.bias, params.solver);
                        // A keyframe's score may decline gradually (lighting, perspective)
                        // and still give the right answer; reject it below the floor or on
                        // a sudden drop, as auto-pause does.
                        const double floor = an.zncc.empty() ? params.refineMinZncc : params.pauseBelowZncc;
                        if (r.diag.converged && r.diag.zncc >= floor &&
                            r.diag.zncc >= recentZncc(an.zncc) - params.pauseOnZnccDrop &&
                            r.diag.visibleFraction >= params.pauseBelowVisible) {
                            accepted = r;
                            acceptedIsFrameToFrame = false;
                            an.gain = r.gain;
                            an.bias = r.bias;
                            an.zncc.push_back(r.diag.zncc);
                            tf.refinedOnReference = true;
                            tf.keyframe = an.frame;
                            break;
                        }
                    }
                    if (!tf.refinedOnReference && fallback) {
                        if (litTmpl.empty())
                            litTmpl = buildTemplate(Pyramid(refImg, levels, true, litFilter), referenceMask,
                                                    params.solver.minMaskWeight);
                        if (lit.tmpl.empty()) lit.tmpl = litTmpl;
                        const Pyramid litPyr(img, levels, true, litFilter);
                        const SolveResult r = solveESM(lit.tmpl, litPyr, accepted.h, lit.gain, lit.bias, params.solver);
                        const double floor = lit.zncc.empty() ? params.refineMinZncc : params.pauseBelowZncc;
                        if (r.diag.converged && r.diag.zncc >= floor &&
                            r.diag.zncc >= recentZncc(lit.zncc) - params.pauseOnZnccDrop &&
                            r.diag.visibleFraction >= params.pauseBelowVisible) {
                            lit.gain = r.gain;
                            lit.bias = r.bias;
                            lit.zncc.push_back(r.diag.zncc);
                            // The normalised solve is robust to lighting but less precise.
                            // Use it as a check: keep frame-to-frame while they agree, and
                            // correct it only once it has drifted away.
                            double disagree = 0.0;
                            for (const Vec2& c : litTmpl.levels[0].box)
                                disagree = std::max(disagree, (applyH(r.h, c) - applyH(accepted.h, c)).norm());
                            if (disagree > params.fallbackCorrection) {
                                accepted = r; // same sample layout as the plain template
                                acceptedIsFrameToFrame = false;
                                tf.lightingFallback = true;
                            } else {
                                verifiedBy = r.diag;
                            }
                            tf.refinedOnReference = true; // verified against the reference either way
                            tf.keyframe = referenceFrame;
                        }
                    }
                    unanchored = tf.refinedOnReference ? 0 : unanchored + 1;
                    const FrameDiagnostics& d = accepted.diag;
                    if (!tf.refinedOnReference && params.reanchorAfter > 0 && unanchored >= params.reanchorAfter &&
                        d.converged && d.zncc >= params.reanchorMinZncc && d.inlierRatio >= 0.6) {
                        // The frame-to-frame solve is healthy but no keyframe matches any more:
                        // this frame becomes a keyframe, without the pixels it rejected.
                        anchors.push_back({weightTemplate(resampleTemplate(refTmpl, pyr, accepted.h),
                                                          accepted.robustWeights),
                                           f, 1.0, 0.0, {}});
                        tf.newKeyframe = true;
                        tf.keyframe = f;
                        unanchored = 0;
                        result.keyframes.push_back(f);
                    }
                }
            }
            tf.h = accepted.h;
            tf.gain = accepted.gain;
            tf.bias = accepted.bias;
            tf.diag = accepted.diag;
            if (params.strategy == ReferenceStrategy::Keyframe) {
                kfGain = accepted.gain;
                kfBias = accepted.bias;
            }

            // Judge the frame by the solve that vouches for it: a keyframe, the
            // lighting check, or frame-to-frame alone.
            const bool byLightingCheck = tf.lightingFallback || verifiedBy.has_value();
            const int kind = byLightingCheck ? kLit : acceptedIsFrameToFrame ? kFrameToFrame : tf.keyframe;
            const FrameDiagnostics& judged = verifiedBy ? *verifiedBy : tf.diag;
            const std::string why = pauseReason(judged, params, recentZncc(znccHistory[kind]));
            if (!why.empty()) {
                if (!result.pausedAt) {
                    result.pausedAt = f;
                    result.pauseReason = why;
                }
                break;
            }
            out.push_back(tf);
            znccHistory[kind].push_back(judged.zncc);
            if (params.carryRejections) prevWeights = accepted.robustWeights;
            hPrev2 = hPrev;
            hPrev = tf.h;
            havePrev2 = true;
            prevPyr = pyr;
        }
    }

    std::sort(out.begin(), out.end(),
              [](const TrackedFrame& a, const TrackedFrame& b) { return a.frame < b.frame; });
    result.frames = std::move(out);
    result.keyframes.push_back(referenceFrame);
    std::sort(result.keyframes.begin(), result.keyframes.end());
    return result;
}

} // namespace ptrack
