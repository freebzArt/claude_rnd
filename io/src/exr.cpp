#include "ptrack/io/exr.h"

#include "ptrack/colour.h"

#include <ImfChannelList.h>
#include <ImfFrameBuffer.h>
#include <ImfHeader.h>
#include <ImfInputPart.h>
#include <ImfMultiPartInputFile.h>
#include <ImfOutputFile.h>
#include <ImfStandardAttributes.h>
#include <Iex.h>

#include <algorithm>
#include <stdexcept>

namespace ptrack::io {

namespace {

ExrInfo infoFromHeader(const Imf::Header& h, int parts, bool overscan = false) {
    ExrInfo info;
    const Imath::Box2i dw = h.dataWindow(), disp = h.displayWindow();
    info.width = disp.max.x - disp.min.x + 1;
    info.height = disp.max.y - disp.min.y + 1;
    info.displayMinX = disp.min.x;
    info.displayMinY = disp.min.y;
    info.dataMinX = dw.min.x;
    info.dataMinY = dw.min.y;
    info.dataMaxX = dw.max.x;
    info.dataMaxY = dw.max.y;
    info.pixelAspect = h.pixelAspectRatio();
    info.parts = parts;
    // Region the returned images cover: the display window, or its union with
    // the data window when overscan is kept.
    int x0 = disp.min.x, y0 = disp.min.y, x1 = disp.max.x, y1 = disp.max.y;
    if (overscan) {
        x0 = std::min(x0, dw.min.x);
        y0 = std::min(y0, dw.min.y);
        x1 = std::max(x1, dw.max.x);
        y1 = std::max(y1, dw.max.y);
    }
    info.originX = x0 - disp.min.x;
    info.originY = y0 - disp.min.y;
    info.imageWidth = x1 - x0 + 1;
    info.imageHeight = y1 - y0 + 1;
    for (auto it = h.channels().begin(); it != h.channels().end(); ++it) info.channels.push_back(it.name());
    return info;
}

std::string qualify(const std::string& layer, const std::string& ch) {
    return layer.empty() ? ch : layer + "." + ch;
}

} // namespace

ExrInfo probeExr(const std::string& path, int part) {
    try {
        Imf::MultiPartInputFile file(path.c_str());
        if (part < 0 || part >= file.parts())
            throw std::runtime_error(path + ": part " + std::to_string(part) + " does not exist");
        return infoFromHeader(file.header(part), file.parts());
    } catch (const Iex::BaseExc& e) {
        throw std::runtime_error(path + ": " + e.what());
    }
}

namespace {

// Read named channels of one part into display-window images.
std::vector<Image> readChannels(Imf::InputPart& in, const ExrInfo& info, const std::vector<std::string>& names) {
    const Imf::Header& h = in.header();
    const Imath::Box2i dw = h.dataWindow();
    const int dwW = dw.max.x - dw.min.x + 1, dwH = dw.max.y - dw.min.y + 1;
    std::vector<std::vector<float>> planes(names.size(), std::vector<float>(size_t(dwW) * dwH, 0.0f));
    Imf::FrameBuffer fb;
    for (size_t i = 0; i < names.size(); ++i) {
        // Base pointer offset so that (dw.min.x, dw.min.y) lands on element 0.
        char* base = reinterpret_cast<char*>(planes[i].data()) -
                     (static_cast<ptrdiff_t>(dw.min.x) + static_cast<ptrdiff_t>(dw.min.y) * dwW) *
                         static_cast<ptrdiff_t>(sizeof(float));
        fb.insert(names[i].c_str(), Imf::Slice(Imf::FLOAT, base, sizeof(float), sizeof(float) * size_t(dwW)));
    }
    in.setFrameBuffer(fb);
    in.readPixels(dw.min.y, dw.max.y);

    std::vector<Image> out(names.size(), Image(info.imageWidth, info.imageHeight, 0.0f));
    const int ox = info.displayMinX + info.originX, oy = info.displayMinY + info.originY;
    for (int y = 0; y < info.imageHeight; ++y) {
        const int sy = y + oy - dw.min.y;
        if (sy < 0 || sy >= dwH) continue;
        for (int x = 0; x < info.imageWidth; ++x) {
            const int sx = x + ox - dw.min.x;
            if (sx < 0 || sx >= dwW) continue;
            const size_t idx = size_t(sy) * dwW + sx;
            for (size_t c = 0; c < names.size(); ++c) out[c].at(x, y) = planes[c][idx];
        }
    }
    return out;
}

template <typename Fn>
auto withPart(const std::string& path, int part, bool overscan, Fn&& fn) {
    try {
        Imf::MultiPartInputFile file(path.c_str());
        if (part < 0 || part >= file.parts())
            throw std::runtime_error(path + ": part " + std::to_string(part) + " does not exist");
        Imf::InputPart in(file, part);
        return fn(in, infoFromHeader(in.header(), file.parts(), overscan));
    } catch (const Iex::BaseExc& e) {
        throw std::runtime_error(path + ": " + e.what());
    }
}

} // namespace

RgbImage readExrRgb(const std::string& path, const ExrReadOptions& opts, ExrInfo* infoOut) {
    return withPart(path, opts.part, opts.overscan, [&](Imf::InputPart& in, const ExrInfo& info) {
        if (infoOut) *infoOut = info;
        const Imf::ChannelList& ch = in.header().channels();
        std::vector<std::string> names;
        for (const char* c : {"R", "G", "B"}) names.push_back(qualify(opts.layer, c));
        const bool haveRGB = std::all_of(names.begin(), names.end(),
                                         [&](const std::string& c) { return ch.findChannel(c); });
        RgbImage rgb;
        if (haveRGB) {
            auto planes = readChannels(in, info, names);
            rgb.r = std::move(planes[0]);
            rgb.g = std::move(planes[1]);
            rgb.b = std::move(planes[2]);
        } else {
            const std::string y = qualify(opts.layer, "Y"); // luminance-only files
            if (!ch.findChannel(y))
                throw std::runtime_error(path + ": no R/G/B or Y channels in layer '" + opts.layer + "'");
            rgb.r = std::move(readChannels(in, info, {y})[0]);
            rgb.g = rgb.r;
            rgb.b = rgb.r;
        }
        return rgb;
    });
}

Image readExr(const std::string& path, const ExrReadOptions& opts, ExrInfo* infoOut) {
    if (opts.channel == "luma") {
        const RgbImage rgb = readExrRgb(path, opts, infoOut);
        Image out(rgb.r.width(), rgb.r.height());
        const size_t n = size_t(out.width()) * out.height();
        for (size_t i = 0; i < n; ++i) out.data()[i] = luma709(rgb.r.data()[i], rgb.g.data()[i], rgb.b.data()[i]);
        return out;
    }
    return withPart(path, opts.part, opts.overscan, [&](Imf::InputPart& in, const ExrInfo& info) {
        if (infoOut) *infoOut = info;
        const Imf::ChannelList& ch = in.header().channels();
        std::string name = qualify(opts.layer, opts.channel);
        if (!ch.findChannel(name) && ch.findChannel(opts.channel)) name = opts.channel;
        if (!ch.findChannel(name)) throw std::runtime_error(path + ": channel '" + name + "' not found");
        return std::move(readChannels(in, info, {name})[0]);
    });
}

void writeExrGrey(const std::string& path, const Image& img, bool halfFloat) {
    ExrWriteOptions o;
    o.halfFloat = halfFloat;
    writeExrGrey(path, img, o);
}

void writeExrGrey(const std::string& path, const Image& img, const ExrWriteOptions& o) {
    try {
        const int dispW = o.displayWidth > 0 ? o.displayWidth : img.width();
        const int dispH = o.displayHeight > 0 ? o.displayHeight : img.height();
        const Imath::Box2i display(Imath::V2i(0, 0), Imath::V2i(dispW - 1, dispH - 1));
        const Imath::Box2i data(Imath::V2i(o.dataOriginX, o.dataOriginY),
                                Imath::V2i(o.dataOriginX + img.width() - 1, o.dataOriginY + img.height() - 1));
        Imf::Header header(display, data, o.pixelAspect);
        header.compression() = Imf::ZIP_COMPRESSION;
        const Imf::PixelType type = o.halfFloat ? Imf::HALF : Imf::FLOAT;
        for (const char* c : {"R", "G", "B"}) header.channels().insert(c, Imf::Channel(type));
        Imf::OutputFile out(path.c_str(), header);
        Imf::FrameBuffer fb;
        // Base pointers are offset so the data window's corner lands on element 0.
        const ptrdiff_t shift = static_cast<ptrdiff_t>(o.dataOriginX) +
                                static_cast<ptrdiff_t>(o.dataOriginY) * img.width();
        std::vector<half> buf;
        if (o.halfFloat) {
            buf.resize(size_t(img.width()) * img.height());
            for (size_t i = 0; i < buf.size(); ++i) buf[i] = half(img.data()[i]);
            char* base = reinterpret_cast<char*>(buf.data()) - shift * static_cast<ptrdiff_t>(sizeof(half));
            for (const char* c : {"R", "G", "B"})
                fb.insert(c, Imf::Slice(Imf::HALF, base, sizeof(half), sizeof(half) * size_t(img.width())));
        } else {
            char* base = const_cast<char*>(reinterpret_cast<const char*>(img.data())) -
                         shift * static_cast<ptrdiff_t>(sizeof(float));
            for (const char* c : {"R", "G", "B"})
                fb.insert(c, Imf::Slice(Imf::FLOAT, base, sizeof(float), sizeof(float) * size_t(img.width())));
        }
        out.setFrameBuffer(fb);
        out.writePixels(img.height());
    } catch (const Iex::BaseExc& e) {
        throw std::runtime_error(path + ": " + e.what());
    }
}

} // namespace ptrack::io
