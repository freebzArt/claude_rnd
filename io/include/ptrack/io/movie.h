#pragma once

#include "ptrack/image.h"

#include <cstdint>
#include <memory>
#include <string>

namespace ptrack::io {

struct MovieInfo {
    int width = 0, height = 0;
    double fps = 0.0;
    int64_t frameCount = 0;
    float pixelAspect = 1.0f;
    std::string codec;          // e.g. "prores", "h264"
    std::string timecode;       // start timecode from the container, if any ("01:00:00:00")
    std::string transfer;       // colour tags as written in the file ("bt709", "unknown", ...)
    std::string primaries;
    bool fullRange = false;     // false = video (legal) range
};

// Decodes movie files (QuickTime, MP4, MXF) through FFmpeg. Frames are
// addressed by 0-based index; Media maps them to host frame numbers.
// Sequential reads (forward tracking) decode straight through; any other
// access seeks to the nearest earlier keyframe and decodes forward.
class MovieReader {
public:
    explicit MovieReader(const std::string& path); // throws std::runtime_error
    ~MovieReader();
    MovieReader(const MovieReader&) = delete;
    MovieReader& operator=(const MovieReader&) = delete;

    const MovieInfo& info() const;

    // One channel of the frame as display-referred values in [0, 1]:
    // "luma" (Rec.709 weights over full-range RGB), or "R", "G", "B".
    // Returns an empty image past the end of the movie.
    Image readFrame(int64_t index, const std::string& channel = "luma");

    // Full-range RGB in [0, 1], display-referred as stored.
    RgbImage readFrameRgb(int64_t index);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Frame number for a timecode at a frame rate (non-drop-frame), e.g.
// "01:00:00:00" at 24 fps -> 86400. Returns false if it doesn't parse.
bool timecodeToFrame(const std::string& tc, double fps, int& frame);

} // namespace ptrack::io
