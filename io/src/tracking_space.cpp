#include "ptrack/io/tracking_space.h"

#ifdef PTRACK_HAVE_OCIO
#include <OpenColorIO/OpenColorIO.h>
namespace OCIO = OCIO_NAMESPACE;
#endif

#include <cstdlib>
#include <stdexcept>
#include <vector>

namespace ptrack::io {

struct TrackingSpace::Impl {
#ifdef PTRACK_HAVE_OCIO
    OCIO::ConstCPUProcessorRcPtr cpu;
#endif
};

bool TrackingSpace::ocioAvailable() {
#ifdef PTRACK_HAVE_OCIO
    return true;
#else
    return false;
#endif
}

bool TrackingSpace::isColourChannel(const std::string& c) {
    return c == "luma" || c == "R" || c == "G" || c == "B";
}

TrackingSpace::TrackingSpace(const TrackingSpaceOptions& opts) : impl_(std::make_unique<Impl>()), opts_(opts) {
    if (!isColourChannel(opts.channel))
        throw std::runtime_error("tracking channel must be luma, R, G or B (got '" + opts.channel + "')");
    const bool wantOcio = !opts.from.empty() || !opts.to.empty();
    if (!wantOcio) {
        description_ = opts.channel + ", curve " + toString(opts.curve);
        return;
    }
    if (opts.from.empty() || opts.to.empty())
        throw std::runtime_error("an OCIO conversion needs both a source and a tracking colourspace");
#ifdef PTRACK_HAVE_OCIO
    try {
        OCIO::ConstConfigRcPtr config;
        if (!opts.ocioConfig.empty()) {
            config = OCIO::Config::CreateFromFile(opts.ocioConfig.c_str());
        } else {
            const char* env = std::getenv("OCIO");
            if (!env || !*env)
                throw std::runtime_error("no OCIO config: pass one or set the OCIO environment variable");
            config = OCIO::Config::CreateFromEnv();
        }
        // Resolve roles ("scene_linear", "compositing_log") as well as colourspace names.
        auto resolve = [&](const std::string& name) {
            const char* cs = config->getCanonicalName(name.c_str());
            if (!cs || !*cs) throw std::runtime_error("OCIO colourspace or role '" + name + "' not found in the config");
            return std::string(cs);
        };
        const std::string from = resolve(opts.from), to = resolve(opts.to);
        impl_->cpu = config->getProcessor(from.c_str(), to.c_str())->getDefaultCPUProcessor();
        description_ = opts.channel + ", OCIO " + from + " -> " + to;
    } catch (const OCIO::Exception& e) {
        throw std::runtime_error(std::string("OCIO: ") + e.what());
    }
#else
    throw std::runtime_error("this build has no OCIO support; use the built-in tracking curve instead");
#endif
}

TrackingSpace::~TrackingSpace() = default;

bool TrackingSpace::usesOcio() const {
#ifdef PTRACK_HAVE_OCIO
    return impl_->cpu != nullptr;
#else
    return false;
#endif
}

Image TrackingSpace::apply(const RgbImage& in) const {
    if (in.empty()) return {};
    const int w = in.width(), h = in.height();
    const size_t n = size_t(w) * h;
    Image out(w, h);
    const int pick = opts_.channel == "R" ? 0 : opts_.channel == "G" ? 1 : opts_.channel == "B" ? 2 : -1;

#ifdef PTRACK_HAVE_OCIO
    if (impl_->cpu) {
        // Convert row by row in packed RGB to bound scratch memory.
        std::vector<float> px(size_t(w) * 3);
        for (int y = 0; y < h; ++y) {
            const float *r = in.r.row(y), *g = in.g.row(y), *b = in.b.row(y);
            for (int x = 0; x < w; ++x) {
                px[3 * x] = r[x];
                px[3 * x + 1] = g[x];
                px[3 * x + 2] = b[x];
            }
            OCIO::PackedImageDesc desc(px.data(), w, 1, 3);
            impl_->cpu->apply(desc);
            float* d = out.row(y);
            for (int x = 0; x < w; ++x)
                d[x] = pick >= 0 ? px[3 * x + pick] : luma709(px[3 * x], px[3 * x + 1], px[3 * x + 2]);
        }
        return out;
    }
#endif
    const float* src[3] = {in.r.data(), in.g.data(), in.b.data()};
    for (size_t i = 0; i < n; ++i) out.data()[i] = pick >= 0 ? src[pick][i] : luma709(src[0][i], src[1][i], src[2][i]);
    applyCurve(opts_.curve, out);
    return out;
}

} // namespace ptrack::io
