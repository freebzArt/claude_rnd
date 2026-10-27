#include "ptrack/preprocess.h"

#include "ptrack/pyramid.h"

#include <cmath>

namespace ptrack {

const char* toString(Normalize n) {
    switch (n) {
    case Normalize::None: return "none";
    case Normalize::Bandpass: return "bandpass";
    case Normalize::LocalContrast: return "local-contrast";
    }
    return "none";
}

bool parseNormalize(const std::string& s, Normalize& out) {
    for (Normalize n : {Normalize::None, Normalize::Bandpass, Normalize::LocalContrast}) {
        if (s == toString(n)) {
            out = n;
            return true;
        }
    }
    return false;
}

Image preprocess(const Image& img, const PreprocessParams& p) {
    if (p.mode == Normalize::None || img.empty()) return img;
    const size_t n = size_t(img.width()) * img.height();
    const Image coarse = gaussianBlur(img, p.coarseSigma);

    if (p.mode == Normalize::Bandpass) {
        const Image fine = p.fineSigma > 0.0 ? gaussianBlur(img, p.fineSigma) : img;
        Image out(img.width(), img.height());
        for (size_t i = 0; i < n; ++i) out.data()[i] = fine.data()[i] - coarse.data()[i];
        return out;
    }

    // Local contrast: (I - mean) / sqrt(local variance + eps). eps keeps flat
    // areas (sky, blank walls) from amplifying noise into fake texture.
    Image centred(img.width(), img.height()), sq(img.width(), img.height());
    for (size_t i = 0; i < n; ++i) {
        const float d = img.data()[i] - coarse.data()[i];
        centred.data()[i] = d;
        sq.data()[i] = d * d;
    }
    const Image var = gaussianBlur(sq, p.coarseSigma);
    double meanVar = 0.0;
    for (size_t i = 0; i < n; ++i) meanVar += var.data()[i];
    meanVar /= static_cast<double>(n);
    const float eps = static_cast<float>(0.01 * meanVar + 1e-8);
    Image out(img.width(), img.height());
    for (size_t i = 0; i < n; ++i) out.data()[i] = centred.data()[i] / std::sqrt(var.data()[i] + eps);
    return out;
}

} // namespace ptrack
