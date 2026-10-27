#pragma once

#include "ptrack/image.h"

#include <memory>
#include <string>

namespace ptrack::io {

struct MediaOptions {
    std::string layer;            // EXR layer prefix
    int part = 0;                 // EXR part
    // Movies: host frame number of the first frame. Nuke numbers movie frames
    // from 1 by default (docs/conventions.md, section 5).
    int movieStart = 1;
    bool movieStartFromTimecode = false;
};

// A source of frames addressed by host frame number, for any supported format.
class Media {
public:
    virtual ~Media() = default;

    // Colour of one frame in display-window coordinates, as stored (no colour
    // conversion). Empty if the frame is missing or past the ends.
    virtual RgbImage readRgb(int frame) = 0;

    // One named non-colour channel ("A", "depth.Z"). EXR only.
    virtual Image readChannel(int frame, const std::string& name);

    int first() const { return first_; }
    int last() const { return last_; }
    int width() const { return width_; }
    int height() const { return height_; }
    double pixelAspect() const { return pixelAspect_; }
    // Scene-linear sources (EXR) default to a log tracking curve; display-referred
    // sources (most movies) are tracked as they are.
    bool sceneLinear() const { return sceneLinear_; }
    const std::string& description() const { return description_; }

protected:
    int first_ = 0, last_ = -1, width_ = 0, height_ = 0;
    double pixelAspect_ = 1.0;
    bool sceneLinear_ = false;
    std::string description_;
};

bool isMoviePath(const std::string& path);

// Open an EXR sequence pattern ("plate.####.exr", "plate.%04d.exr") or a
// movie file. Throws std::runtime_error with a readable message.
std::unique_ptr<Media> openMedia(const std::string& path, const MediaOptions& opts = {});

} // namespace ptrack::io
