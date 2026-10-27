#pragma once

#include "ptrack/colour.h"
#include "ptrack/image.h"

#include <memory>
#include <string>

namespace ptrack::io {

// How a source's colour becomes the single channel the tracker sees
// (docs/conventions.md, section 6). With an OCIO config and both colourspaces
// set, RGB is converted from `from` to `to` first; otherwise the built-in
// curve is applied after the channel is picked.
struct TrackingSpaceOptions {
    std::string channel = "luma";   // luma, R, G or B
    std::string ocioConfig;         // config path; empty = $OCIO
    std::string from;               // source colourspace or role, e.g. "ACEScg" or "scene_linear"
    std::string to;                 // tracking colourspace or role, e.g. "ACEScct" or "compositing_log"
    TrackingCurve curve = TrackingCurve::None; // used when no OCIO conversion is set
};

class TrackingSpace {
public:
    explicit TrackingSpace(const TrackingSpaceOptions& opts); // throws std::runtime_error
    ~TrackingSpace();
    TrackingSpace(const TrackingSpace&) = delete;
    TrackingSpace& operator=(const TrackingSpace&) = delete;

    Image apply(const RgbImage& rgb) const;
    bool usesOcio() const;
    const std::string& description() const { return description_; }

    static bool ocioAvailable();
    static bool isColourChannel(const std::string& channel);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    TrackingSpaceOptions opts_;
    std::string description_;
};

} // namespace ptrack::io
