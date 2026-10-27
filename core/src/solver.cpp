#include "ptrack/solver.h"

#include <Eigen/Dense>

#include <algorithm>
#include <cmath>
#include <limits>

namespace ptrack {

namespace {

constexpr int kMaxParams = 10;
using MatP = Eigen::Matrix<double, kMaxParams, kMaxParams>;
using VecP = Eigen::Matrix<double, kMaxParams, 1>;

double robustWeight(RobustLoss loss, double u) {
    const double au = std::abs(u);
    switch (loss) {
    case RobustLoss::None: return 1.0;
    case RobustLoss::Huber: {
        constexpr double k = 1.345;
        return au <= k ? 1.0 : k / au;
    }
    case RobustLoss::Tukey: {
        constexpr double c = 4.685;
        if (au >= c) return 0.0;
        const double t = 1.0 - (u / c) * (u / c);
        return t * t;
    }
    case RobustLoss::Cauchy: {
        constexpr double c = 2.385;
        return 1.0 / (1.0 + (u / c) * (u / c));
    }
    }
    return 1.0;
}

// Jacobian of the projective map x -> H x at x (2x2, row-major d(u,v)/d(x,y)).
inline void warpJacobian(const Mat3& h, double x, double y, double& u, double& v, double d[4]) {
    const double hx = h(0, 0) * x + h(0, 1) * y + h(0, 2);
    const double hy = h(1, 0) * x + h(1, 1) * y + h(1, 2);
    const double hw = h(2, 0) * x + h(2, 1) * y + h(2, 2);
    const double iw = 1.0 / hw;
    u = hx * iw;
    v = hy * iw;
    d[0] = (h(0, 0) - u * h(2, 0)) * iw;
    d[1] = (h(0, 1) - u * h(2, 1)) * iw;
    d[2] = (h(1, 0) - v * h(2, 0)) * iw;
    d[3] = (h(1, 1) - v * h(2, 1)) * iw;
}

double medianAbs(std::vector<double>& v) {
    if (v.empty()) return 0.0;
    const size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + mid, v.end());
    return v[mid];
}

struct LevelState {
    // Per-sample cache for one iteration.
    std::vector<float> value, gcx, gcy, resid;
    std::vector<unsigned char> valid;
    // Per-sample derivative of the warped position w.r.t. each basis
    // parameter, in normalised coordinates (du, dv per parameter).
    std::vector<float> dudv;
};

void precomputeBasisDerivatives(const TemplateLevel& tl, const std::vector<Mat3>& basis, LevelState& st) {
    const int dof = static_cast<int>(basis.size());
    const size_t n = tl.x.size();
    const double k = 1.0 / tl.spread;
    st.dudv.resize(n * dof * 2);
    for (size_t i = 0; i < n; ++i) {
        const double xn = k * (tl.x[i] - tl.centroid.x());
        const double yn = k * (tl.y[i] - tl.centroid.y());
        for (int b = 0; b < dof; ++b) {
            const Mat3& B = basis[b];
            const double q0 = B(0, 0) * xn + B(0, 1) * yn + B(0, 2);
            const double q1 = B(1, 0) * xn + B(1, 1) * yn + B(1, 2);
            const double q2 = B(2, 0) * xn + B(2, 1) * yn + B(2, 2);
            // Back to level pixels: d/dtheta of N^-1 * w(N x) scales by 1/k.
            st.dudv[(i * dof + b) * 2 + 0] = static_cast<float>((q0 - xn * q2) / k);
            st.dudv[(i * dof + b) * 2 + 1] = static_cast<float>((q1 - yn * q2) / k);
        }
    }
}

// Sample the frame through Hl for every template sample; fills value,
// gradient-in-reference-coordinates and residual. Returns the visible count.
size_t sampleFrame(const TemplateLevel& tl, const PyramidLevel& fl, const Mat3& hl, double gain,
                   double bias, LevelState& st) {
    const size_t n = tl.x.size();
    st.value.resize(n);
    st.gcx.resize(n);
    st.gcy.resize(n);
    st.resid.resize(n);
    st.valid.assign(n, 0);
    size_t visible = 0;
    for (size_t i = 0; i < n; ++i) {
        double u, v, d[4];
        warpJacobian(hl, tl.x[i], tl.y[i], u, v, d);
        float val, dx, dy;
        if (!fl.sampleWithGradient(u, v, val, dx, dy)) continue;
        st.valid[i] = 1;
        st.value[i] = val;
        st.gcx[i] = static_cast<float>(dx * d[0] + dy * d[2]);
        st.gcy[i] = static_cast<float>(dx * d[1] + dy * d[3]);
        st.resid[i] = static_cast<float>(gain * val + bias - tl.value[i]);
        ++visible;
    }
    return visible;
}

double residualScale(const TemplateLevel& tl, const LevelState& st, const SolverParams& p) {
    if (p.lossScale > 0.0) return p.lossScale;
    std::vector<double> a;
    a.reserve(tl.x.size());
    for (size_t i = 0; i < tl.x.size(); ++i)
        if (st.valid[i] && tl.mask[i] > 0.5f) a.push_back(std::abs(st.resid[i]));
    if (a.empty())
        for (size_t i = 0; i < tl.x.size(); ++i)
            if (st.valid[i]) a.push_back(std::abs(st.resid[i]));
    return std::max(1.4826 * medianAbs(a), 1e-4);
}

struct Normal {
    MatP a = MatP::Zero();
    VecP g = VecP::Zero();
    double weightSum = 0.0;
};

// Build the robust-weighted normal equations. Optionally reports each
// sample's robust weight and Jacobian row through the callback.
template <typename PerSample>
Normal accumulate(const TemplateLevel& tl, const LevelState& st, int dof, int n, double gain,
                  RobustLoss loss, double scale, PerSample&& perSample) {
    Normal ne;
    double J[kMaxParams];
    for (size_t i = 0; i < tl.x.size(); ++i) {
        if (!st.valid[i]) continue;
        const double r = st.resid[i];
        const double w = tl.mask[i] * robustWeight(loss, r / scale);
        perSample(i, w, J, false);
        if (w <= 0.0) continue;
        const double ex = 0.5 * (gain * st.gcx[i] + tl.gx[i]);
        const double ey = 0.5 * (gain * st.gcy[i] + tl.gy[i]);
        const float* dd = &st.dudv[i * dof * 2];
        for (int b = 0; b < dof; ++b) J[b] = ex * dd[2 * b] + ey * dd[2 * b + 1];
        if (n > dof) {
            J[dof] = st.value[i];
            J[dof + 1] = 1.0;
        }
        for (int r0 = 0; r0 < n; ++r0) {
            const double wj = w * J[r0];
            ne.g(r0) += wj * r;
            for (int c = r0; c < n; ++c) ne.a(r0, c) += wj * J[c];
        }
        ne.weightSum += w;
        perSample(i, w, J, true);
    }
    for (int r0 = 0; r0 < n; ++r0)
        for (int c = 0; c < r0; ++c) ne.a(r0, c) = ne.a(c, r0);
    return ne;
}

Eigen::VectorXd solveNormal(const Normal& ne, int n, Eigen::MatrixXd* systemOut = nullptr) {
    Eigen::MatrixXd a = ne.a.topLeftCorner(n, n);
    const double damp = 1e-9 * std::max(a.trace() / n, 1e-12);
    a.diagonal().array() += damp;
    if (systemOut) *systemOut = a;
    return a.ldlt().solve(-ne.g.head(n));
}

Mat3 normaliser(const TemplateLevel& tl, bool inverse) {
    const double k = 1.0 / tl.spread;
    Mat3 m = Mat3::Identity();
    if (!inverse) {
        m(0, 0) = m(1, 1) = k;
        m(0, 2) = -k * tl.centroid.x();
        m(1, 2) = -k * tl.centroid.y();
    } else {
        m(0, 0) = m(1, 1) = tl.spread;
        m(0, 2) = tl.centroid.x();
        m(1, 2) = tl.centroid.y();
    }
    return m;
}

} // namespace

const char* toString(RobustLoss loss) {
    switch (loss) {
    case RobustLoss::None: return "none";
    case RobustLoss::Huber: return "huber";
    case RobustLoss::Tukey: return "tukey";
    case RobustLoss::Cauchy: return "cauchy";
    }
    return "huber";
}

bool parseRobustLoss(const std::string& s, RobustLoss& out) {
    for (RobustLoss l : {RobustLoss::None, RobustLoss::Huber, RobustLoss::Tukey, RobustLoss::Cauchy}) {
        if (s == toString(l)) {
            out = l;
            return true;
        }
    }
    return false;
}

Template buildTemplate(const Pyramid& reference, const Image& mask, float minMaskWeight) {
    Template t;
    Image m = mask;
    for (int l = 0; l < reference.levels(); ++l) {
        const PyramidLevel& pl = reference.level(l);
        if (l > 0) m = downsampleMask2(m);
        if (m.width() != pl.img.width() || m.height() != pl.img.height()) break;
        TemplateLevel tl;
        tl.scale = pl.scale;
        tl.width = m.width();
        tl.height = m.height();
        double sw = 0.0, sx = 0.0, sy = 0.0;
        double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
        for (int j = 0; j < m.height(); ++j) {
            for (int i = 0; i < m.width(); ++i) {
                const float w = m.at(i, j);
                if (w < minMaskWeight) continue;
                const float x = i + 0.5f, y = j + 0.5f;
                tl.x.push_back(x);
                tl.y.push_back(y);
                tl.value.push_back(pl.img.at(i, j));
                tl.gx.push_back(pl.gx.at(i, j));
                tl.gy.push_back(pl.gy.at(i, j));
                tl.mask.push_back(std::min(w, 1.0f));
                sw += w;
                sx += w * x;
                sy += w * y;
                minX = std::min<double>(minX, x);
                maxX = std::max<double>(maxX, x);
                minY = std::min<double>(minY, y);
                maxY = std::max<double>(maxY, y);
            }
        }
        if (tl.x.size() < 16) break; // too few samples to constrain anything
        tl.maskSum = sw;
        tl.centroid = Vec2(sx / sw, sy / sw);
        double ss = 0.0;
        for (size_t i = 0; i < tl.x.size(); ++i) {
            const double dx = tl.x[i] - tl.centroid.x(), dy = tl.y[i] - tl.centroid.y();
            ss += tl.mask[i] * (dx * dx + dy * dy);
        }
        tl.spread = std::max(std::sqrt(ss / sw), 1.0);
        tl.box = {Vec2(minX, maxY), Vec2(maxX, maxY), Vec2(maxX, minY), Vec2(minX, minY)};
        t.levels.push_back(std::move(tl));
    }
    return t;
}

Template resampleTemplate(const Template& base, const Pyramid& frame, const Mat3& h) {
    Template t = base;
    for (int l = 0; l < t.levelCount() && l < frame.levels(); ++l) {
        TemplateLevel& tl = t.levels[l];
        const PyramidLevel& fl = frame.level(l);
        const Mat3 hl = toLevel(h, tl.scale);
        for (size_t i = 0; i < tl.x.size(); ++i) {
            double u, v, d[4];
            warpJacobian(hl, tl.x[i], tl.y[i], u, v, d);
            float val, dx, dy;
            if (!fl.sampleWithGradient(u, v, val, dx, dy)) {
                tl.mask[i] = 0.0f;
                continue;
            }
            tl.value[i] = val;
            tl.gx[i] = static_cast<float>(dx * d[0] + dy * d[2]);
            tl.gy[i] = static_cast<float>(dx * d[1] + dy * d[3]);
        }
    }
    return t;
}

Template weightTemplate(const Template& base, const std::vector<float>& finestWeights) {
    Template t = base;
    if (t.empty() || finestWeights.size() != t.levels[0].x.size()) return t;
    const TemplateLevel& l0 = t.levels[0];
    Image w(l0.width, l0.height, 1.0f);
    for (size_t i = 0; i < l0.x.size(); ++i)
        w.at(static_cast<int>(l0.x[i]), static_cast<int>(l0.y[i])) = finestWeights[i];
    for (int l = 0; l < t.levelCount(); ++l) {
        if (l > 0) w = downsampleMask2(w);
        TemplateLevel& tl = t.levels[l];
        if (w.width() != tl.width || w.height() != tl.height) break;
        for (size_t i = 0; i < tl.x.size(); ++i)
            tl.mask[i] *= w.at(static_cast<int>(tl.x[i]), static_cast<int>(tl.y[i]));
    }
    return t;
}

SolveResult solveESM(const Template& tmpl, const Pyramid& frame, const Mat3& hInit, double gain,
                     double bias, const SolverParams& params) {
    SolveResult res;
    res.h = normalizeH(hInit);
    res.gain = gain;
    res.bias = bias;
    if (tmpl.empty() || frame.levels() == 0) return res;

    const std::vector<Mat3> basis = motionBasis(params.model, params.pixelAspect);
    const int dof = static_cast<int>(basis.size());
    const int n = dof + (params.photometric ? 2 : 0);
    const int levels = std::min(tmpl.levelCount(), frame.levels());

    Mat3 h = res.h;
    double a = gain, b = bias;
    LevelState st;
    auto noop = [](size_t, double, const double*, bool) {};

    for (int l = levels - 1; l >= 0; --l) {
        const TemplateLevel& tl = tmpl.levels[l];
        const PyramidLevel& fl = frame.level(l);
        precomputeBasisDerivatives(tl, basis, st);
        const Mat3 nrm = normaliser(tl, false), nrmInv = normaliser(tl, true);
        const double tol = (l == 0) ? params.tolerance : std::max(params.tolerance, 0.01);
        Mat3 hl = toLevel(h, tl.scale);
        bool converged = false;

        for (int it = 0; it < params.maxIterations; ++it) {
            const size_t visible = sampleFrame(tl, fl, hl, a, b, st);
            if (visible < static_cast<size_t>(4 * n)) break;
            const double scale = residualScale(tl, st, params);
            // Redescending losses (Tukey) reject outright; warm up with Huber so a
            // poor starting guess at a new level does not reject the good pixels.
            const RobustLoss loss = (params.loss == RobustLoss::Tukey && it < params.warmupIterations)
                                        ? RobustLoss::Huber
                                        : params.loss;
            Normal ne = accumulate(tl, st, dof, n, a, loss, scale, noop);
            if (ne.weightSum < 1e-6 && params.loss == RobustLoss::Tukey)
                ne = accumulate(tl, st, dof, n, a, RobustLoss::Huber, scale, noop);
            if (ne.weightSum < 1e-6) break;
            const Eigen::VectorXd delta = solveNormal(ne, n);
            if (!delta.allFinite()) break;

            Mat3 lie = Mat3::Zero();
            for (int k = 0; k < dof; ++k) lie += delta(k) * basis[k];
            const Mat3 hNew = normalizeH(hl * nrmInv * expm(lie) * nrm);
            if (n > dof) {
                a += delta(dof);
                b += delta(dof + 1);
            }
            double move = 0.0;
            for (const Vec2& c : tl.box) move = std::max(move, (applyH(hNew, c) - applyH(hl, c)).norm());
            hl = hNew;
            ++res.diag.iterations;
            if (move < tol) {
                converged = true;
                break;
            }
        }
        h = toLevel(hl, 1.0 / tl.scale);
        if (l == 0) res.diag.converged = converged;
    }

    res.h = normalizeH(h);
    res.gain = a;
    res.bias = b;

    // Final evaluation at the solution: diagnostics, and optionally the
    // per-pixel record used by the heat maps.
    const TemplateLevel& tl = tmpl.levels[0];
    precomputeBasisDerivatives(tl, basis, st);
    const size_t visible = sampleFrame(tl, frame.level(0), res.h, a, b, st);
    if (visible == 0) return res;
    const double scale = residualScale(tl, st, params);

    std::vector<float> rw(tl.x.size(), 0.0f);
    PerPixel& pp = res.perPixel;
    std::vector<std::array<double, kMaxParams>> jrows;
    if (params.keepPerPixel) jrows.resize(tl.x.size());
    Normal ne = accumulate(tl, st, dof, n, a, params.loss, scale,
                           [&](size_t i, double w, const double* J, bool hasJ) {
                               if (!hasJ) {
                                   rw[i] = tl.mask[i] > 0.0f ? static_cast<float>(w / tl.mask[i]) : 0.0f;
                                   return;
                               }
                               if (params.keepPerPixel)
                                   std::copy(J, J + n, jrows[i].begin());
                           });

    // Weighted normalised cross-correlation between template and warped frame.
    struct Corr {
        double w = 0, t = 0, f = 0, tt = 0, ff = 0, tf = 0;
        void add(double wt, double tv, double fv) {
            w += wt;
            t += wt * tv;
            f += wt * fv;
            tt += wt * tv * tv;
            ff += wt * fv * fv;
            tf += wt * tv * fv;
        }
        double value() const {
            if (w <= 0) return 0.0;
            const double ct = tt - t * t / w, cf = ff - f * f / w, ctf = tf - t * f / w;
            return (ct > 1e-12 && cf > 1e-12) ? ctf / std::sqrt(ct * cf) : 0.0;
        }
    } all, kept;
    res.robustWeights = rw;
    double mw = 0.0, mvis = 0.0, inl = 0.0, r2 = 0.0;
    for (size_t i = 0; i < tl.x.size(); ++i) {
        mw += tl.mask[i];
        if (!st.valid[i]) continue;
        const double m = tl.mask[i];
        mvis += m;
        if (rw[i] > 0.5f) inl += m;
        r2 += m * st.resid[i] * st.resid[i];
        all.add(m, tl.value[i], st.value[i]);
        kept.add(m * rw[i], tl.value[i], st.value[i]);
    }
    FrameDiagnostics& d = res.diag;
    d.visibleFraction = mw > 0 ? mvis / mw : 0.0;
    if (mvis > 0) {
        d.rms = std::sqrt(r2 / mvis);
        d.inlierRatio = inl / mvis;
        d.zncc = kept.value();
        d.znccAll = all.value();
    }
    const Eigen::MatrixXd geo = ne.a.topLeftCorner(dof, dof);
    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> es(geo, Eigen::EigenvaluesOnly);
    const double emin = es.eigenvalues().minCoeff(), emax = es.eigenvalues().maxCoeff();
    d.conditionNumber = emin > 0 ? emax / emin : std::numeric_limits<double>::infinity();
    d.minEigenvalue = ne.weightSum > 0 ? emin / ne.weightSum : 0.0;

    if (params.keepPerPixel) {
        Eigen::MatrixXd sys;
        pp.lastStep = solveNormal(ne, n, &sys);
        const Eigen::LDLT<Eigen::MatrixXd> ldlt(sys);
        const size_t count = tl.x.size();
        pp.x.assign(tl.x.begin(), tl.x.end());
        pp.y.assign(tl.y.begin(), tl.y.end());
        pp.residual.assign(count, 0.0f);
        pp.weight.assign(count, 0.0f);
        pp.contribution = Eigen::MatrixXf::Zero(n, static_cast<Eigen::Index>(count));
        Eigen::MatrixXd rhs = Eigen::MatrixXd::Zero(n, static_cast<Eigen::Index>(count));
        for (size_t i = 0; i < count; ++i) {
            if (!st.valid[i]) continue;
            pp.residual[i] = st.resid[i];
            pp.weight[i] = rw[i];
            const double w = tl.mask[i] * rw[i];
            for (int k = 0; k < n; ++k) rhs(k, static_cast<Eigen::Index>(i)) = -w * jrows[i][k] * st.resid[i];
        }
        pp.contribution = ldlt.solve(rhs).cast<float>();
    }
    return res;
}

} // namespace ptrack
