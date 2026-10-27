#include "ptrack/solver.h"
#include "ptrack/synth.h"
#include "ptrack/warp.h"

#include <Eigen/Dense>

#include <cmath>

#include "test_helpers.h"

#include <doctest/doctest.h>

using namespace ptrack;

namespace {

const SynthShot& shot() {
    static const SynthShot s = [] {
        SynthParams p;
        p.frames = 12;
        p.motion = 1.5;
        return makeSynthShot(p);
    }();
    return s;
}

double cornerErr(const Mat3& h, const Mat3& truth, const std::array<Vec2, 4>& surface) {
    double m = 0;
    for (const Vec2& c : surface) m = std::max(m, (applyH(h, c) - applyH(truth, c)).norm());
    return m;
}

} // namespace

TEST_CASE("ESM recovers a perspective motion from identity") {
    const SynthShot& s = shot();
    const Pyramid ref(s.frame(s.referenceFrame), 3);
    const Template t = buildTemplate(ref, s.mask);
    REQUIRE(t.levelCount() == 3);
    const int f = s.startFrame + 8;
    const Pyramid cur(s.frame(f), 3);
    SolverParams params;
    const SolveResult r = solveESM(t, cur, Mat3::Identity(), 1.0, 0.0, params);
    CHECK(r.diag.converged);
    CHECK(cornerErr(r.h, s.truthAt(f), s.surface) < 0.05);
    CHECK(r.diag.zncc > 0.99);
    CHECK(r.diag.visibleFraction == doctest::Approx(1.0));
}

TEST_CASE("joint gain and bias absorb a lighting change") {
    const SynthShot& s = shot();
    const Pyramid ref(s.frame(s.referenceFrame), 3);
    const Template t = buildTemplate(ref, s.mask);
    const int f = s.startFrame + 4;
    Image lit = s.frame(f);
    for (int y = 0; y < lit.height(); ++y)
        for (int x = 0; x < lit.width(); ++x) lit.at(x, y) = 1.3f * lit.at(x, y) + 0.05f;
    const Pyramid cur(lit, 3);
    const SolveResult r = solveESM(t, cur, Mat3::Identity(), 1.0, 0.0, SolverParams{});
    CHECK(cornerErr(r.h, s.truthAt(f), s.surface) < 0.05);
    CHECK(r.gain == doctest::Approx(1.0 / 1.3).epsilon(0.01));
}

TEST_CASE("per-pixel contributions sum to the solver step") {
    const SynthShot& s = shot();
    const Pyramid ref(s.frame(s.referenceFrame), 3);
    const Template t = buildTemplate(ref, s.mask);
    const Pyramid cur(s.frame(s.startFrame + 3), 3);
    SolverParams params;
    params.keepPerPixel = true;
    params.maxIterations = 2; // stop early so the remaining step is not negligible
    const SolveResult r = solveESM(t, cur, Mat3::Identity(), 1.0, 0.0, params);
    REQUIRE(r.perPixel.contribution.cols() == static_cast<Eigen::Index>(t.levels[0].x.size()));
    const Eigen::VectorXd sum = r.perPixel.contribution.cast<double>().rowwise().sum();
    CHECK((sum - r.perPixel.lastStep).norm() < 1e-4 * (1.0 + r.perPixel.lastStep.norm()));
}

TEST_CASE("degenerate texture shows up in the condition number") {
    // Vertical stripes constrain x but not y: the aperture problem.
    Image img(160, 120);
    for (int y = 0; y < 120; ++y)
        for (int x = 0; x < 160; ++x) img.at(x, y) = 0.5f + 0.4f * std::sin(x * 0.4f);
    const Image mask = Image(160, 120, 1.0f);
    const Pyramid pyr(img, 2);
    const Template t = buildTemplate(pyr, mask);
    SolverParams params;
    params.model = MotionModel::Translation;
    const SolveResult r = solveESM(t, pyr, Mat3::Identity(), 1.0, 0.0, params);
    CHECK(r.diag.conditionNumber > 1e6);
}

TEST_CASE("similarity on anamorphic pixels means rotation on screen") {
    // A 2x-squeezed plate: a 4 degree screen-space rotation is not a rotation
    // in stored pixels, so the similarity model must know the pixel aspect.
    Image tex(320, 360);
    for (int y = 0; y < tex.height(); ++y)
        for (int x = 0; x < tex.width(); ++x)
            tex.at(x, y) = 0.5f + 0.2f * std::sin(0.31f * x + 0.05f * y) * std::cos(0.17f * y) +
                           0.15f * std::sin(0.07f * x * 2.0f - 0.11f * y);
    const double par = 2.0, a = 4.0 * 3.14159265358979 / 180.0;
    Mat3 rot = Mat3::Identity();
    rot << std::cos(a), -std::sin(a), 0, std::sin(a), std::cos(a), 0, 0, 0, 1;
    const Eigen::DiagonalMatrix<double, 3> s(par, 1.0, 1.0), si(1.0 / par, 1.0, 1.0);
    // Rotate about the centre on screen, then express in stored pixels.
    Mat3 c = Mat3::Identity(), ci = Mat3::Identity();
    c(0, 2) = 160 * par;
    c(1, 2) = 180;
    ci(0, 2) = -160 * par;
    ci(1, 2) = -180;
    const Mat3 truth = Mat3(si * (c * rot * ci) * s);
    const Image cur = warpImage(tex, truth, tex.width(), tex.height());
    Image mask(320, 360);
    for (int y = 90; y < 270; ++y)
        for (int x = 80; x < 240; ++x) mask.at(x, y) = 1.0f;
    const Pyramid ref(tex, 3), frame(cur, 3);
    const Template t = buildTemplate(ref, mask);
    const std::array<Vec2, 4> box = {Vec2(80, 270), Vec2(240, 270), Vec2(240, 90), Vec2(80, 90)};

    SolverParams p;
    p.model = MotionModel::Similarity;
    p.pixelAspect = par;
    const SolveResult good = solveESM(t, frame, Mat3::Identity(), 1.0, 0.0, p);
    p.pixelAspect = 1.0;
    const SolveResult bad = solveESM(t, frame, Mat3::Identity(), 1.0, 0.0, p);
    const double eGood = cornerErr(good.h, truth, box), eBad = cornerErr(bad.h, truth, box);
    MESSAGE("anamorphic similarity: with pixel aspect " << eGood << " px, without " << eBad << " px");
    CHECK(eGood < 0.05);
    CHECK(eBad > 1.0);
}
