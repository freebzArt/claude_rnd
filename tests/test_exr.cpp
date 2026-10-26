#include "ptrack/io/exr.h"
#include "ptrack/io/sequence.h"
#include "ptrack/io/track_json.h"

#include <doctest/doctest.h>

#include <filesystem>

using namespace ptrack;
namespace fs = std::filesystem;

namespace {

fs::path tempDir() {
    const fs::path d = fs::temp_directory_path() / "ptrack_tests";
    fs::create_directories(d);
    return d;
}

} // namespace

TEST_CASE("EXR grey round trip in full float") {
    Image img(37, 23);
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x) img.at(x, y) = 0.01f * x + 0.1f * y;
    const std::string p = (tempDir() / "roundtrip.exr").string();
    io::writeExrGrey(p, img, false);
    io::ExrInfo info;
    const Image back = io::readExr(p, {}, &info);
    REQUIRE(back.width() == 37);
    REQUIRE(back.height() == 23);
    CHECK(info.channels.size() == 3);
    CHECK(back.at(20, 10) == doctest::Approx(img.at(20, 10)).epsilon(1e-6));
    io::ExrReadOptions g;
    g.channel = "G";
    CHECK(io::readExr(p, g).at(5, 5) == doctest::Approx(img.at(5, 5)));
    g.channel = "Z";
    CHECK_THROWS_AS(io::readExr(p, g), std::runtime_error);
}

TEST_CASE("sequence patterns from Nuke and Flame forms") {
    CHECK(io::Sequence("shot/plate.%04d.exr").path(1001) == "shot/plate.1001.exr");
    CHECK(io::Sequence("shot/plate.####.exr").path(7) == "shot/plate.0007.exr");
    CHECK(io::Sequence("shot/plate.1001.exr").path(1002) == "shot/plate.1002.exr");
    CHECK(io::Sequence("100%_plate.%04d.exr").path(3) == "100%_plate.0003.exr");
    CHECK_THROWS(io::Sequence("noframe.exr"));
    const auto r = io::Sequence::parseRange("1001-1100");
    REQUIRE(r);
    CHECK(r->first == 1001);
    CHECK(r->second == 1100);
    CHECK_FALSE(io::Sequence::parseRange("1100-1001"));
}

TEST_CASE("sequence scan finds frames and keeps gaps") {
    const fs::path d = tempDir() / "scan";
    fs::remove_all(d);
    fs::create_directories(d);
    const Image img(4, 4, 0.5f);
    for (int f : {1001, 1002, 1005}) io::writeExrGrey((d / ("p." + std::to_string(f) + ".exr")).string(), img);
    const auto frames = io::Sequence((d / "p.####.exr").string()).scan();
    CHECK(frames == std::vector<int>{1001, 1002, 1005});
}

TEST_CASE("track JSON round trip") {
    io::TrackDocument d;
    d.formatWidth = 1920;
    d.formatHeight = 1080;
    d.surface = {Vec2(1, 9), Vec2(9, 9), Vec2(9, 1), Vec2(1, 1)};
    d.track.referenceFrame = 1001;
    TrackedFrame f;
    f.frame = 1001;
    f.h(0, 2) = 3.5;
    f.diag.zncc = 0.98;
    d.track.frames.push_back(f);
    d.track.pausedAt = 1002;
    d.track.pauseReason = "test";
    const std::string p = (tempDir() / "t.json").string();
    io::writeTrackJson(p, d);
    const io::TrackDocument b = io::readTrackJson(p);
    CHECK(b.formatHeight == 1080);
    REQUIRE(b.track.frames.size() == 1);
    CHECK(b.track.frames[0].h(0, 2) == doctest::Approx(3.5));
    CHECK(b.track.frames[0].diag.zncc == doctest::Approx(0.98));
    CHECK(b.track.pausedAt == 1002);
    CHECK(b.surface[2].x() == doctest::Approx(9.0));
}
