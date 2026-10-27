#ifdef PTRACK_HAVE_FFMPEG

#include "ptrack/io/exr.h"
#include "ptrack/io/media.h"
#include "ptrack/io/movie.h"

#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <string>

using namespace ptrack;
namespace fs = std::filesystem;

namespace {

// Encode a small test movie with the ffmpeg command-line tool. Returns an
// empty path (and the tests skip) when ffmpeg isn't installed.
std::string makeMovie(const std::string& name, const std::string& codecArgs) {
    if (std::system("ffmpeg -version >/dev/null 2>&1") != 0) return {};
    const fs::path d = fs::temp_directory_path() / "ptrack_tests" / "movie";
    fs::create_directories(d);
    for (int f = 0; f < 12; ++f) {
        Image img(96, 64);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 96; ++x) img.at(x, y) = 0.1f + 0.06f * f + 0.004f * x + 0.002f * y;
        char n[32];
        std::snprintf(n, sizeof n, "src.%04d.exr", f);
        io::writeExrGrey((d / n).string(), img);
    }
    const fs::path out = d / name;
    const std::string cmd = "ffmpeg -v error -y -framerate 24 -i \"" + (d / "src.%04d.exr").string() + "\" " +
                            codecArgs + " -timecode 00:00:10:00 \"" + out.string() + "\"";
    return std::system(cmd.c_str()) == 0 ? out.string() : std::string();
}

} // namespace

TEST_CASE("movie: random access matches sequential decoding") {
    for (const char* codec : {"-c:v prores_ks -pix_fmt yuv422p10le", "-c:v libx264 -g 5 -crf 10 -pix_fmt yuv420p"}) {
        const std::string path = makeMovie(std::string(codec).find("prores") != std::string::npos ? "a.mov" : "a.mp4", codec);
        if (path.empty()) {
            MESSAGE("ffmpeg not available or codec missing; skipping " << codec);
            continue;
        }
        io::MovieReader seq(path), rnd(path);
        REQUIRE(seq.info().frameCount == 12);
        CHECK(seq.info().fps == doctest::Approx(24.0));
        std::vector<Image> frames;
        for (int i = 0; i < 12; ++i) frames.push_back(seq.readFrame(i));
        CHECK(seq.readFrame(12).empty());
        for (int i : {7, 2, 11, 0, 6, 5}) {
            const Image f = rnd.readFrame(i);
            REQUIRE_FALSE(f.empty());
            CHECK(f.at(40, 30) == doctest::Approx(frames[i].at(40, 30)));
        }
        // Frames get brighter over time, so order mistakes show up here.
        CHECK(frames[9].at(40, 30) > frames[3].at(40, 30) + 0.2f);
    }
}

TEST_CASE("movie: host frame numbering from start frame or timecode") {
    const std::string path = makeMovie("tc.mov", "-c:v prores_ks");
    if (path.empty()) return;
    io::MediaOptions o;
    CHECK(io::openMedia(path, o)->first() == 1);
    o.movieStart = 1001;
    const auto m = io::openMedia(path, o);
    CHECK(m->first() == 1001);
    CHECK(m->last() == 1012);
    CHECK_FALSE(m->read(1005).empty());
    CHECK(m->read(1013).empty());
    o.movieStartFromTimecode = true;
    CHECK(io::openMedia(path, o)->first() == 240); // 10 s at 24 fps
}

TEST_CASE("timecode parsing") {
    int f = 0;
    REQUIRE(io::timecodeToFrame("01:00:00:00", 24.0, f));
    CHECK(f == 86400);
    REQUIRE(io::timecodeToFrame("00:00:01:12", 25.0, f));
    CHECK(f == 37);
    CHECK_FALSE(io::timecodeToFrame("bogus", 24.0, f));
}

#endif
