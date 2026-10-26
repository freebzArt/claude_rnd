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
        CHECK(e.max < 2.0);
    }
}
