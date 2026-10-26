#pragma once

#include <Eigen/Core>

#include <array>
#include <string>
#include <vector>

namespace ptrack {

using Mat3 = Eigen::Matrix3d;
using Vec2 = Eigen::Vector2d;

enum class MotionModel {
    Translation, // 2 DOF
    Similarity,  // translation + rotation + uniform scale, 4 DOF
    Affine,      // 6 DOF
    Perspective, // full homography, 8 DOF
};

int degreesOfFreedom(MotionModel model);
const char* toString(MotionModel model);
bool parseMotionModel(const std::string& s, MotionModel& out);

// Generators of the local parameterisation H <- H * exp(sum theta_k B_k).
// Perspective spans all of gl(3) modulo scale; smaller models span subgroups.
const std::vector<Mat3>& motionBasis(MotionModel model);

// Matrix exponential (scaling and squaring with a Taylor series). Accurate for
// the small updates the solvers produce and well beyond.
Mat3 expm(const Mat3& a);

// Apply a homography to a point.
inline Vec2 applyH(const Mat3& h, const Vec2& p) {
    const double x = h(0, 0) * p.x() + h(0, 1) * p.y() + h(0, 2);
    const double y = h(1, 0) * p.x() + h(1, 1) * p.y() + h(1, 2);
    const double w = h(2, 0) * p.x() + h(2, 1) * p.y() + h(2, 2);
    return {x / w, y / w};
}

// Scale H to H(2,2) = 1 (falls back to unit Frobenius norm if H(2,2) ~ 0).
Mat3 normalizeH(const Mat3& h);

// Express a homography in pyramid-level coordinates: S * H * S^-1 with S = diag(s, s, 1).
Mat3 toLevel(const Mat3& h, double s);

// Homography that maps four source points to four destination points (DLT).
Mat3 homographyFromCorners(const std::array<Vec2, 4>& src, const std::array<Vec2, 4>& dst);

// Constant-velocity prediction in the plane: H_prev * H_prev2^-1 * H_prev.
Mat3 predictConstantVelocity(const Mat3& hPrev2, const Mat3& hPrev);

} // namespace ptrack
