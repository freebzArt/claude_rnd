#pragma once

#include "ptrack/motion.h"
#include "ptrack/pyramid.h"

#include <Eigen/Core>

#include <vector>

namespace ptrack {

enum class RobustLoss { None, Huber, Tukey, Cauchy };

const char* toString(RobustLoss loss);
bool parseRobustLoss(const std::string& s, RobustLoss& out);

struct SolverParams {
    MotionModel model = MotionModel::Perspective;
    RobustLoss loss = RobustLoss::Tukey;
    int warmupIterations = 3;       // Huber iterations per level before a redescending loss
    double lossScale = 0.0;         // residual scale; 0 = automatic (1.4826 * MAD)
    bool photometric = true;        // jointly estimate gain and bias
    int maxIterations = 40;         // per pyramid level
    double tolerance = 1e-3;        // stop when region corners move less than this (level pixels)
    float minMaskWeight = 1e-3f;    // template pixels below this weight are ignored
    bool keepPerPixel = false;      // record per-pixel residuals, weights and contributions
};

// Reference samples for one pyramid level, in structure-of-arrays form so the
// same layout maps directly to GPU buffers later.
struct TemplateLevel {
    double scale = 1.0;                 // level coords = image coords * scale
    int width = 0, height = 0;          // size of the level image the samples came from
    std::vector<float> x, y;            // sample positions (level coords, pixel centres)
    std::vector<float> value, gx, gy;   // template intensity and gradient
    std::vector<float> mask;            // matte weight in [0, 1]
    Vec2 centroid = Vec2::Zero();       // for parameter normalisation
    double spread = 1.0;
    std::array<Vec2, 4> box;            // bounding box corners, for convergence tests
    double maskSum = 0.0;
};

// A tracking template: the reference appearance of a region and its matte,
// living in reference-frame coordinates at every pyramid level.
struct Template {
    std::vector<TemplateLevel> levels;
    int levelCount() const { return static_cast<int>(levels.size()); }
    bool empty() const { return levels.empty() || levels[0].x.empty(); }
};

// Build a template from a reference pyramid and a full-resolution matte
// (same size as level 0; weights in [0, 1]).
Template buildTemplate(const Pyramid& reference, const Image& mask, float minMaskWeight = 1e-3f);

// Re-sample a template's appearance from another frame through H (reference -> frame).
// Positions and matte are kept; samples that land outside the frame get zero matte.
// This is how frame-to-frame tracking keeps all templates in reference coordinates.
Template resampleTemplate(const Template& base, const Pyramid& frame, const Mat3& h);

// Multiply a template's matte by per-sample weights given at its finest level
// (for example the robust weights of the last solve), carried down every level.
// Frame-to-frame tracking uses this so pixels rejected as occluded on one frame
// do not become part of the next frame's template.
Template weightTemplate(const Template& base, const std::vector<float>& finestWeights);

struct FrameDiagnostics {
    double rms = 0.0;             // RMS residual over visible matte-weighted samples
    double zncc = 0.0;            // correlation over the pixels the robust estimator kept
    double znccAll = 0.0;         // correlation over the whole visible matte (occluders included)
    double inlierRatio = 0.0;     // matte weight kept by the robust estimator (w > 0.5)
    double visibleFraction = 0.0; // matte weight that lands inside the frame
    double conditionNumber = 0.0; // of the geometric normal matrix (normalised coordinates)
    double minEigenvalue = 0.0;   // per unit matte weight; small = degenerate texture
    int iterations = 0;           // total over all levels
    bool converged = false;       // finest level converged within maxIterations
};

// Per-pixel record of the final iteration at the finest level. contribution
// holds each sample's share of the last parameter step: the shares sum to the
// step exactly, which is what the heat map visualises (docs, Phase 5).
struct PerPixel {
    std::vector<float> x, y, residual, weight;
    Eigen::MatrixXf contribution; // (parameters x samples)
    Eigen::VectorXd lastStep;     // the step the contributions sum to
};

struct SolveResult {
    Mat3 h = Mat3::Identity();
    double gain = 1.0, bias = 0.0;
    FrameDiagnostics diag;
    std::vector<float> robustWeights; // finest level, per sample: 1 trusted, 0 rejected or unseen
    PerPixel perPixel; // filled when SolverParams::keepPerPixel
};

// Align a template to a frame with Efficient Second-order Minimisation and
// robust IRLS, coarse to fine. hInit maps reference coords to frame coords.
SolveResult solveESM(const Template& tmpl, const Pyramid& frame, const Mat3& hInit, double gain,
                     double bias, const SolverParams& params);

} // namespace ptrack
