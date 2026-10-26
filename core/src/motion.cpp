#include "ptrack/motion.h"

#include <Eigen/Dense>

#include <cmath>

namespace ptrack {

namespace {

Mat3 unit(int r, int c) {
    Mat3 m = Mat3::Zero();
    m(r, c) = 1.0;
    return m;
}

std::vector<Mat3> makeBasis(MotionModel model) {
    std::vector<Mat3> b;
    b.push_back(unit(0, 2)); // tx
    b.push_back(unit(1, 2)); // ty
    if (model == MotionModel::Translation) return b;
    if (model == MotionModel::Similarity) {
        b.push_back(unit(1, 0) - unit(0, 1)); // rotation
        b.push_back(unit(0, 0) + unit(1, 1)); // uniform scale
        return b;
    }
    b.push_back(unit(0, 0));
    b.push_back(unit(0, 1));
    b.push_back(unit(1, 0));
    b.push_back(unit(1, 1));
    if (model == MotionModel::Affine) return b;
    b.push_back(unit(2, 0));
    b.push_back(unit(2, 1));
    return b;
}

} // namespace

int degreesOfFreedom(MotionModel model) {
    switch (model) {
    case MotionModel::Translation: return 2;
    case MotionModel::Similarity: return 4;
    case MotionModel::Affine: return 6;
    case MotionModel::Perspective: return 8;
    }
    return 8;
}

const char* toString(MotionModel model) {
    switch (model) {
    case MotionModel::Translation: return "translation";
    case MotionModel::Similarity: return "similarity";
    case MotionModel::Affine: return "affine";
    case MotionModel::Perspective: return "perspective";
    }
    return "perspective";
}

bool parseMotionModel(const std::string& s, MotionModel& out) {
    for (MotionModel m : {MotionModel::Translation, MotionModel::Similarity, MotionModel::Affine,
                          MotionModel::Perspective}) {
        if (s == toString(m)) {
            out = m;
            return true;
        }
    }
    return false;
}

const std::vector<Mat3>& motionBasis(MotionModel model) {
    static const std::vector<Mat3> bases[4] = {
        makeBasis(MotionModel::Translation), makeBasis(MotionModel::Similarity),
        makeBasis(MotionModel::Affine), makeBasis(MotionModel::Perspective)};
    return bases[static_cast<int>(model)];
}

Mat3 expm(const Mat3& a) {
    const double norm = a.cwiseAbs().rowwise().sum().maxCoeff();
    int squarings = 0;
    if (norm > 0.5) squarings = static_cast<int>(std::ceil(std::log2(norm / 0.5)));
    const Mat3 s = a / std::ldexp(1.0, squarings);
    Mat3 result = Mat3::Identity();
    Mat3 term = Mat3::Identity();
    for (int k = 1; k <= 12; ++k) {
        term = term * s / static_cast<double>(k);
        result += term;
    }
    for (int i = 0; i < squarings; ++i) result = result * result;
    return result;
}

Mat3 normalizeH(const Mat3& h) {
    if (std::abs(h(2, 2)) > 1e-12) return h / h(2, 2);
    return h / h.norm();
}

Mat3 toLevel(const Mat3& h, double s) {
    Mat3 out = h;
    out(0, 2) *= s;
    out(1, 2) *= s;
    out(2, 0) /= s;
    out(2, 1) /= s;
    return out;
}

Mat3 homographyFromCorners(const std::array<Vec2, 4>& src, const std::array<Vec2, 4>& dst) {
    Eigen::Matrix<double, 8, 8> a;
    Eigen::Matrix<double, 8, 1> b;
    for (int i = 0; i < 4; ++i) {
        const double x = src[i].x(), y = src[i].y(), u = dst[i].x(), v = dst[i].y();
        a.row(2 * i) << x, y, 1, 0, 0, 0, -u * x, -u * y;
        a.row(2 * i + 1) << 0, 0, 0, x, y, 1, -v * x, -v * y;
        b(2 * i) = u;
        b(2 * i + 1) = v;
    }
    const Eigen::Matrix<double, 8, 1> h = a.fullPivLu().solve(b);
    Mat3 m;
    m << h(0), h(1), h(2), h(3), h(4), h(5), h(6), h(7), 1.0;
    return m;
}

Mat3 predictConstantVelocity(const Mat3& hPrev2, const Mat3& hPrev) {
    return normalizeH(hPrev * hPrev2.inverse() * hPrev);
}

} // namespace ptrack
