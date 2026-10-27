#include "ptrack/io/media.h"

#include "ptrack/io/exr.h"
#ifdef PTRACK_HAVE_FFMPEG
#include "ptrack/io/movie.h"
#endif
#include "ptrack/io/sequence.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace ptrack::io {

namespace {

class ExrSequenceMedia final : public Media {
public:
    ExrSequenceMedia(const std::string& pattern, const MediaOptions& o) : seq_(pattern) {
        opts_.channel = o.channel;
        opts_.layer = o.layer;
        opts_.part = o.part;
        const auto frames = seq_.scan();
        if (frames.empty()) throw std::runtime_error("no frames found for " + seq_.pattern());
        first_ = frames.front();
        last_ = frames.back();
        const ExrInfo info = probeExr(seq_.path(first_), o.part);
        width_ = info.width;
        height_ = info.height;
        pixelAspect_ = info.pixelAspect;
        sceneLinear_ = true;
        std::ostringstream d;
        d << seq_.pattern() << " (" << first_ << "-" << last_ << ", " << frames.size() << " frames, "
          << width_ << "x" << height_ << ")";
        description_ = d.str();
    }

    Image read(int frame) override {
        const std::string p = seq_.path(frame);
        if (!fs::exists(p)) return {};
        return readExr(p, opts_);
    }

private:
    Sequence seq_;
    ExrReadOptions opts_;
};

#ifdef PTRACK_HAVE_FFMPEG
class MovieMedia final : public Media {
public:
    MovieMedia(const std::string& path, const MediaOptions& o) : reader_(path), channel_(o.channel) {
        const MovieInfo& i = reader_.info();
        int start = o.movieStart;
        if (o.movieStartFromTimecode) {
            if (i.timecode.empty()) throw std::runtime_error(path + ": the movie has no start timecode");
            if (!timecodeToFrame(i.timecode, i.fps, start))
                throw std::runtime_error(path + ": cannot read timecode '" + i.timecode + "'");
        }
        start_ = start;
        first_ = start;
        last_ = start + static_cast<int>(i.frameCount) - 1;
        width_ = i.width;
        height_ = i.height;
        pixelAspect_ = i.pixelAspect;
        sceneLinear_ = i.transfer == "linear";
        std::ostringstream d;
        d << path << " (" << i.codec << ", " << i.width << "x" << i.height << ", " << i.fps << " fps, "
          << i.frameCount << " frames as " << first_ << "-" << last_
          << (i.timecode.empty() ? "" : ", timecode " + i.timecode) << ", "
          << (i.fullRange ? "full" : "video") << " range)";
        description_ = d.str();
    }

    Image read(int frame) override { return reader_.readFrame(frame - start_, channel_); }

private:
    MovieReader reader_;
    std::string channel_;
    int start_ = 1;
};
#endif

} // namespace

bool isMoviePath(const std::string& path) {
    std::string ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return ext == ".mov" || ext == ".mp4" || ext == ".m4v" || ext == ".mxf" || ext == ".mkv" ||
           ext == ".avi";
}

std::unique_ptr<Media> openMedia(const std::string& path, const MediaOptions& opts) {
    if (isMoviePath(path)) {
#ifdef PTRACK_HAVE_FFMPEG
        return std::make_unique<MovieMedia>(path, opts);
#else
        throw std::runtime_error(path + ": this build has no movie support (built without FFmpeg)");
#endif
    }
    return std::make_unique<ExrSequenceMedia>(path, opts);
}

} // namespace ptrack::io
