#include "ptrack/tracker.h"

#include "test_helpers.h"

#include <doctest/doctest.h>

#include <cstdio>
#include <string>

using namespace ptrack;
using test::cornerError;
using test::sourceFor;

namespace {

TrackResult runTrack(const SynthShot& s, ReferenceStrategy strategy, RobustLoss loss = RobustLoss::Tukey) {
    TrackParams p;
    p.strategy = strategy;
    p.solver.loss = loss;
    const int last = s.startFrame + static_cast<int>(s.frames.size()) - 1;
    return trackRegion(sourceFor(s), s.referenceFrame, s.mask, s.startFrame, last, p);
}

void report(const char* name, const test::CornerError& e) {
    std::printf("  %-28s mean %.4f px  max %.4f px  (%d frames)\n", name, e.mean, e.max, e.frames);
}

} // namespace

TEST_CASE("accuracy: clean shot, all strategies") {
    SynthParams sp;
    sp.frames = 30;
    sp.referenceOffset = 10; // track both directions
    const SynthShot s = makeSynthShot(sp);
    for (ReferenceStrategy st :
         {ReferenceStrategy::Hybrid, ReferenceStrategy::Keyframe, ReferenceStrategy::FrameToFrame}) {
        const TrackResult r = runTrack(s, st);
        const auto e = cornerError(r, s);
        report(toString(st), e);
        CHECK_FALSE(r.pausedAt.has_value());
        CHECK(e.frames == 30);
        CHECK(e.mean < (st == ReferenceStrategy::FrameToFrame ? 0.1 : 0.05));
    }
}

TEST_CASE("accuracy: grain") {
    SynthParams sp;
    sp.frames = 30;
    sp.noiseSigma = 0.02;
    sp.seed = 3;
    const SynthShot s = makeSynthShot(sp);
    const auto e = cornerError(runTrack(s, ReferenceStrategy::Hybrid), s);
    report("hybrid + grain", e);
    CHECK(e.frames == 30);
    CHECK(e.mean < 0.15);
}

TEST_CASE("accuracy: lighting flicker") {
    SynthParams sp;
    sp.frames = 30;
    sp.flicker = 0.2;
    sp.seed = 4;
    const SynthShot s = makeSynthShot(sp);
    const auto e = cornerError(runTrack(s, ReferenceStrategy::Hybrid), s);
    report("hybrid + flicker", e);
    CHECK(e.frames == 30);
    CHECK(e.mean < 0.05);
}

TEST_CASE("accuracy: occluder crossing the plane") {
    SynthParams sp;
    sp.frames = 30;
    sp.occluder = true;
    sp.seed = 5;
    const SynthShot s = makeSynthShot(sp);
    const TrackResult r = runTrack(s, ReferenceStrategy::Hybrid, RobustLoss::Tukey);
    if (r.pausedAt) std::printf("  paused at %d: %s\n", *r.pausedAt, r.pauseReason.c_str());
    const auto robust = cornerError(r, s);
    report("hybrid + occluder (tukey)", robust);
    CHECK(robust.frames == 30);
    CHECK(robust.mean < 0.2);
}

TEST_CASE("auto-pause reports a reason when the plate disappears") {
    SynthParams sp;
    sp.frames = 12;
    SynthShot s = makeSynthShot(sp);
    for (int i = 8; i < 12; ++i) s.frames[i] = Image(sp.width, sp.height, 0.2f); // flat frames
    TrackParams p;
    const TrackResult r = trackRegion(sourceFor(s), s.referenceFrame, s.mask, s.startFrame,
                                      s.startFrame + 11, p);
    REQUIRE(r.pausedAt.has_value());
    CHECK(*r.pausedAt == s.startFrame + 8);
    CHECK_FALSE(r.pauseReason.empty());
    CHECK(r.frames.back().frame == s.startFrame + 7);
}

TEST_CASE("no silent slips: occluder already in the reference, grain and flicker") {
    // The hardest synthetic case: nothing is held out. A strategy may pause,
    // but every frame it accepts must still be on the plane.
    SynthParams sp;
    sp.frames = 24;
    sp.referenceOffset = 6;
    sp.noiseSigma = 0.01;
    sp.flicker = 0.1;
    sp.occluder = true;
    const SynthShot s = makeSynthShot(sp);
    for (ReferenceStrategy st :
         {ReferenceStrategy::Hybrid, ReferenceStrategy::Keyframe, ReferenceStrategy::FrameToFrame}) {
        const TrackResult r = runTrack(s, st);
        const auto e = cornerError(r, s);
        report((std::string("stress, ") + toString(st)).c_str(), e);
        if (r.pausedAt) std::printf("  paused at %d: %s\n", *r.pausedAt, r.pauseReason.c_str());
        // ~2 px on the first frame after the reference, where the occluder is
        // baked into the template, is the known limit without a holdout. A
        // slip, which this guards against, is tens to hundreds of pixels.
        CHECK(e.max < 2.5);
    }
}

TEST_CASE("lighting: a shadow sweeps across the plane") {
    // A soft shadow edge crosses the whole plane. Its moving brightness ramp
    // looks like motion, so frame-to-frame alone drifts by hundreds of pixels
    // while every score stays high. Hybrid must stay anchored: the reference
    // is retried on lighting-normalised images when the plain match fails.
    SynthParams sp;
    sp.frames = 40;
    sp.shadow = 0.85;
    sp.noiseSigma = 0.01;
    sp.seed = 7;
    const SynthShot s = makeSynthShot(sp);
    const int last = s.startFrame + sp.frames - 1;

    TrackParams hy;
    const TrackResult h = trackRegion(sourceFor(s), s.referenceFrame, s.mask, s.startFrame, last, hy);
    const auto e = cornerError(h, s);
    report("shadow, hybrid (fallback)", e);
    int fallbackFrames = 0;
    for (const TrackedFrame& f : h.frames) fallbackFrames += f.lightingFallback ? 1 : 0;
    std::printf("  lighting fallback on %d frames\n", fallbackFrames);
    CHECK_FALSE(h.pausedAt.has_value());
    CHECK(fallbackFrames > 0);
    CHECK(e.mean < 0.3);
    CHECK(e.max < 1.0);

    TrackParams lc = hy;
    lc.preprocess.mode = Normalize::LocalContrast;
    lc.strategy = ReferenceStrategy::Keyframe;
    // Normalisation works on log-encoded images, as the CLI feeds it.
    const auto ek = cornerError(
        trackRegion(sourceFor(s, TrackingCurve::ACEScct), s.referenceFrame, s.mask, s.startFrame, last, lc), s);
    report("shadow, keyframe + local contrast", ek);
    CHECK(ek.frames == 40);
    CHECK(ek.mean < 0.3);
}

TEST_CASE("re-anchoring: the surface's content changes completely") {
    // The plane's texture morphs into another (think of a screen playing
    // video). No method recovers exact geometry from changing content, so this
    // documents behaviour: keyframe-only pauses, hybrid finishes by adding
    // keyframes, and re-anchoring never makes it worse. Holding out the
    // changing content is the real fix.
    SynthParams sp;
    sp.frames = 40;
    sp.morph = 1.0;
    sp.noiseSigma = 0.01;
    sp.seed = 7;
    const SynthShot s = makeSynthShot(sp);
    const int last = s.startFrame + sp.frames - 1;

    TrackParams kf;
    kf.strategy = ReferenceStrategy::Keyframe;
    const TrackResult k = trackRegion(sourceFor(s), s.referenceFrame, s.mask, s.startFrame, last, kf);
    CHECK(k.pausedAt.has_value());

    TrackParams hy;
    const TrackResult h = trackRegion(sourceFor(s), s.referenceFrame, s.mask, s.startFrame, last, hy);
    const auto eh = cornerError(h, s);
    report("morph, hybrid + re-anchor", eh);
    CHECK_FALSE(h.pausedAt.has_value());
    CHECK(h.keyframes.size() > 1);

    TrackParams none = hy;
    none.reanchorAfter = 0;
    const auto en = cornerError(trackRegion(sourceFor(s), s.referenceFrame, s.mask, s.startFrame, last, none), s);
    report("morph, hybrid, no re-anchor", en);
    CHECK(eh.mean <= 1.1 * en.mean);
}
