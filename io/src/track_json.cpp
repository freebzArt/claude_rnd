#include "ptrack/io/track_json.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <stdexcept>

using nlohmann::json;

namespace ptrack::io {

namespace {

constexpr int kSchemaVersion = 1;

json mat(const Mat3& h) {
    json a = json::array();
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) a.push_back(h(r, c));
    return a;
}

Mat3 mat(const json& a) {
    if (!a.is_array() || a.size() != 9) throw std::runtime_error("homography must have 9 numbers");
    Mat3 h;
    for (int i = 0; i < 9; ++i) h(i / 3, i % 3) = a[i].get<double>();
    return h;
}

json corners(const std::array<Vec2, 4>& c) {
    json a = json::array();
    for (const Vec2& p : c) a.push_back({p.x(), p.y()});
    return a;
}

std::array<Vec2, 4> corners(const json& a) {
    if (!a.is_array() || a.size() != 4) throw std::runtime_error("surface must have 4 corners");
    std::array<Vec2, 4> c;
    for (int i = 0; i < 4; ++i) c[i] = Vec2(a[i][0].get<double>(), a[i][1].get<double>());
    return c;
}

json document(const TrackDocument& d, const char* kind, bool withDiagnostics) {
    json j;
    j["schema"] = "ptrack.track";
    j["version"] = kSchemaVersion;
    j["kind"] = kind;
    j["source"] = d.source;
    j["format"] = {{"width", d.formatWidth}, {"height", d.formatHeight}, {"pixelAspect", d.pixelAspect}};
    j["coordinates"] = "internal: origin top-left of display window, y down, pixel edges at integers";
    j["referenceFrame"] = d.track.referenceFrame;
    j["surface"] = corners(d.surface);
    if (!d.settings.empty()) j["settings"] = d.settings;
    if (!d.track.keyframes.empty()) j["keyframes"] = d.track.keyframes;
    if (d.track.pausedAt) j["paused"] = {{"frame", *d.track.pausedAt}, {"reason", d.track.pauseReason}};
    json frames = json::array();
    for (const TrackedFrame& f : d.track.frames) {
        json jf;
        jf["frame"] = f.frame;
        jf["h"] = mat(f.h);
        jf["corners"] = corners(cornersAt(f, d.surface));
        if (withDiagnostics) {
            jf["gain"] = f.gain;
            jf["bias"] = f.bias;
            jf["refined"] = f.refinedOnReference;
            if (f.refinedOnReference) jf["keyframe"] = f.keyframe;
            if (f.newKeyframe) jf["newKeyframe"] = true;
            if (f.lightingFallback) jf["lightingFallback"] = true;
            const FrameDiagnostics& g = f.diag;
            jf["diagnostics"] = {{"rms", g.rms},
                                 {"zncc", g.zncc},
                                 {"znccAll", g.znccAll},
                                 {"inlierRatio", g.inlierRatio},
                                 {"visibleFraction", g.visibleFraction},
                                 {"conditionNumber", std::isfinite(g.conditionNumber) ? g.conditionNumber : -1.0},
                                 {"minEigenvalue", g.minEigenvalue},
                                 {"iterations", g.iterations},
                                 {"converged", g.converged}};
        }
        frames.push_back(jf);
    }
    j["frames"] = frames;
    return j;
}

void writeJson(const std::string& path, const json& j) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("cannot write " + path);
    out << j.dump(1) << '\n';
}

} // namespace

void writeTrackJson(const std::string& path, const TrackDocument& doc) {
    writeJson(path, document(doc, "track", true));
}

void writeTruthJson(const std::string& path, const TrackDocument& truth) {
    writeJson(path, document(truth, "truth", false));
}

TrackDocument readTrackJson(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot read " + path);
    json j;
    try {
        in >> j;
    } catch (const json::exception& e) {
        throw std::runtime_error(path + ": " + e.what());
    }
    if (j.value("schema", "") != "ptrack.track")
        throw std::runtime_error(path + ": not a ptrack track file");
    if (j.value("version", 0) > kSchemaVersion)
        throw std::runtime_error(path + ": written by a newer version (schema " +
                                 std::to_string(j.value("version", 0)) + ")");
    TrackDocument d;
    try {
        d.source = j.value("source", "");
        d.formatWidth = j.at("format").value("width", 0);
        d.formatHeight = j.at("format").value("height", 0);
        d.pixelAspect = j.at("format").value("pixelAspect", 1.0);
        d.track.referenceFrame = j.at("referenceFrame").get<int>();
        d.surface = corners(j.at("surface"));
        d.settings = j.value("settings", "");
        if (j.contains("keyframes")) d.track.keyframes = j["keyframes"].get<std::vector<int>>();
        if (j.contains("paused")) {
            d.track.pausedAt = j["paused"].at("frame").get<int>();
            d.track.pauseReason = j["paused"].value("reason", "");
        }
        for (const json& jf : j.at("frames")) {
            TrackedFrame f;
            f.frame = jf.at("frame").get<int>();
            f.h = mat(jf.at("h"));
            f.gain = jf.value("gain", 1.0);
            f.bias = jf.value("bias", 0.0);
            f.refinedOnReference = jf.value("refined", false);
            f.keyframe = jf.value("keyframe", d.track.referenceFrame);
            f.newKeyframe = jf.value("newKeyframe", false);
            f.lightingFallback = jf.value("lightingFallback", false);
            if (jf.contains("diagnostics")) {
                const json& g = jf["diagnostics"];
                f.diag.rms = g.value("rms", 0.0);
                f.diag.zncc = g.value("zncc", 0.0);
                f.diag.znccAll = g.value("znccAll", 0.0);
                f.diag.inlierRatio = g.value("inlierRatio", 0.0);
                f.diag.visibleFraction = g.value("visibleFraction", 0.0);
                f.diag.conditionNumber = g.value("conditionNumber", 0.0);
                f.diag.minEigenvalue = g.value("minEigenvalue", 0.0);
                f.diag.iterations = g.value("iterations", 0);
                f.diag.converged = g.value("converged", false);
            }
            d.track.frames.push_back(f);
        }
    } catch (const json::exception& e) {
        throw std::runtime_error(path + ": " + e.what());
    }
    return d;
}

} // namespace ptrack::io
