// ptrack: command-line front end to the tracking engine.
//
//   ptrack synth --out DIR                        make a synthetic shot with ground truth
//   ptrack track --seq PATTERN --ref F ...        track a region, write JSON and a Nuke CornerPin
//   ptrack nuke  --track FILE                     CornerPin2D text from a saved track
//   ptrack eval  --track FILE --truth FILE        corner error against ground truth
//   ptrack info  FILE.exr                         EXR header summary

#include "ptrack/colour.h"
#include "ptrack/export_nuke.h"
#include "ptrack/io/exr.h"
#include "ptrack/io/sequence.h"
#include "ptrack/io/track_json.h"
#include "ptrack/raster.h"
#include "ptrack/synth.h"
#include "ptrack/tracker.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ptrack;
namespace fs = std::filesystem;

namespace {

struct Args {
    std::string command;
    std::vector<std::string> positional;
    std::map<std::string, std::string> opts; // --key value, or --flag -> "1"

    bool has(const std::string& k) const { return opts.count(k) > 0; }
    std::string get(const std::string& k, const std::string& def = "") const {
        auto it = opts.find(k);
        return it == opts.end() ? def : it->second;
    }
    std::string require(const std::string& k) const {
        if (!has(k)) throw std::runtime_error("missing --" + k);
        return get(k);
    }
    int getInt(const std::string& k, int def) const { return has(k) ? std::stoi(get(k)) : def; }
    double getDouble(const std::string& k, double def) const { return has(k) ? std::stod(get(k)) : def; }
};

const std::vector<std::string> kFlags = {"occluder", "stabilize", "clipboard", "help", "quiet"};

Args parseArgs(int argc, char** argv) {
    Args a;
    if (argc > 1) a.command = argv[1];
    for (int i = 2; i < argc; ++i) {
        std::string s = argv[i];
        if (s.rfind("--", 0) == 0) {
            const std::string key = s.substr(2);
            const bool isFlag = std::find(kFlags.begin(), kFlags.end(), key) != kFlags.end();
            if (isFlag || i + 1 >= argc) a.opts[key] = "1";
            else a.opts[key] = argv[++i];
        } else {
            a.positional.push_back(s);
        }
    }
    return a;
}

std::vector<double> parseNumbers(const std::string& s) {
    std::vector<double> v;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) v.push_back(std::stod(item));
    return v;
}

// Four corners "x,y,x,y,x,y,x,y" in BL, BR, TR, TL order; converted from Nuke
// coordinates (y up) when coords == "nuke".
std::array<Vec2, 4> parseQuad(const std::string& s, const std::string& coords, int height) {
    const auto v = parseNumbers(s);
    if (v.size() != 8) throw std::runtime_error("a quad needs 8 numbers: x,y for BL, BR, TR, TL");
    std::array<Vec2, 4> q;
    for (int i = 0; i < 4; ++i) {
        q[i] = Vec2(v[2 * i], v[2 * i + 1]);
        if (coords == "nuke") q[i].y() = height - q[i].y();
    }
    return q;
}

#ifdef _WIN32
#define PTRACK_POPEN _popen
#define PTRACK_PCLOSE _pclose
#else
#define PTRACK_POPEN popen
#define PTRACK_PCLOSE pclose
#endif

bool pipeTo(const char* cmd, const std::string& text) {
    FILE* p = PTRACK_POPEN(cmd, "w");
    if (!p) return false;
    std::fwrite(text.data(), 1, text.size(), p);
    return PTRACK_PCLOSE(p) == 0;
}

bool copyToClipboard(const std::string& text) {
#ifdef _WIN32
    return pipeTo("clip", text);
#else
    const char* candidates[][2] = {{"pbcopy", "pbcopy"},
                                   {"clip.exe", "clip.exe"}, // WSL
                                   {"wl-copy", "wl-copy"},
                                   {"xclip", "xclip -selection clipboard"},
                                   {"xsel", "xsel --clipboard --input"}};
    for (const auto& c : candidates) {
        const std::string probe = std::string("command -v ") + c[0] + " >/dev/null 2>&1";
        if (std::system(probe.c_str()) == 0 && pipeTo(c[1], text)) return true;
    }
    return false;
#endif
}

void emitNuke(const std::string& text, const Args& a) {
    if (a.has("nuke")) {
        const std::string dest = a.get("nuke");
        if (dest == "-" || dest == "1") {
            std::cout << text;
        } else {
            std::ofstream(dest) << text;
            std::cerr << "Nuke CornerPin2D written to " << dest << "\n";
        }
    }
    if (a.has("clipboard")) {
        if (copyToClipboard(text)) std::cerr << "CornerPin2D copied to the clipboard; paste it into Nuke's node graph.\n";
        else std::cerr << "No clipboard tool found (pbcopy, clip.exe, wl-copy, xclip or xsel). Use --nuke FILE instead.\n";
    }
}

int cmdSynth(const Args& a) {
    SynthParams p;
    p.width = a.getInt("width", p.width);
    p.height = a.getInt("height", p.height);
    p.frames = a.getInt("frames", p.frames);
    p.startFrame = a.getInt("start", p.startFrame);
    p.referenceOffset = a.getInt("ref-offset", p.referenceOffset);
    p.motion = a.getDouble("motion", p.motion);
    p.noiseSigma = a.getDouble("noise", p.noiseSigma);
    p.flicker = a.getDouble("flicker", p.flicker);
    p.occluder = a.has("occluder");
    p.seed = static_cast<unsigned>(a.getInt("seed", 1));
    const fs::path out = a.require("out");
    fs::create_directories(out);

    const SynthShot shot = makeSynthShot(p);
    for (int i = 0; i < p.frames; ++i) {
        char name[64];
        std::snprintf(name, sizeof name, "plate.%04d.exr", p.startFrame + i);
        io::writeExrGrey((out / name).string(), shot.frames[i]);
    }
    io::writeExrGrey((out / "mask.exr").string(), shot.mask);

    io::TrackDocument truth;
    truth.formatWidth = p.width;
    truth.formatHeight = p.height;
    truth.surface = shot.surface;
    truth.source = (out / "plate.####.exr").string();
    truth.track.referenceFrame = shot.referenceFrame;
    for (int i = 0; i < p.frames; ++i) {
        TrackedFrame f;
        f.frame = p.startFrame + i;
        f.h = shot.truth[i];
        truth.track.frames.push_back(f);
    }
    io::writeTruthJson((out / "truth.json").string(), truth);

    std::ostringstream surf;
    for (int i = 0; i < 4; ++i) surf << (i ? "," : "") << shot.surface[i].x() << ',' << shot.surface[i].y();
    std::cout << "Wrote " << p.frames << " frames, mask.exr and truth.json to " << out.string() << "\n"
              << "Track it with:\n  ptrack track --seq " << (out / "plate.####.exr").string() << " --ref "
              << shot.referenceFrame << " --range " << p.startFrame << "-" << p.startFrame + p.frames - 1
              << " --mask " << (out / "mask.exr").string() << " --surface " << surf.str()
              << " --out " << (out / "track.json").string() << "\n";
    return 0;
}

int cmdTrack(const Args& a) {
    const io::Sequence seq(a.require("seq"));
    const int ref = std::stoi(a.require("ref"));

    int first = ref, last = ref;
    if (a.has("range")) {
        const auto r = io::Sequence::parseRange(a.get("range"));
        if (!r) throw std::runtime_error("--range must look like 1001-1100");
        first = r->first;
        last = r->second;
    } else {
        const auto frames = seq.scan();
        if (frames.empty()) throw std::runtime_error("no frames found for " + seq.pattern());
        first = frames.front();
        last = frames.back();
    }
    if (ref < first || ref > last) throw std::runtime_error("--ref must lie inside the range");

    io::ExrReadOptions ro;
    ro.channel = a.get("channel", "luma");
    ro.layer = a.get("layer", "");
    ro.part = a.getInt("part", 0);
    io::ExrInfo info;
    io::readExr(seq.path(ref), ro, &info); // validates the reference and fetches the format
    const std::string curveName = a.get("curve", "auto");
    TrackingCurve curve = TrackingCurve::ACEScct; // EXR is scene-linear by default
    if (curveName != "auto" && !parseTrackingCurve(curveName, curve))
        throw std::runtime_error("--curve must be auto, acescct or none");

    const std::string coords = a.get("coords", "internal");
    Image mask;
    std::array<Vec2, 4> surface{};
    bool haveSurface = false;
    if (a.has("mask")) {
        mask = io::readExr(a.get("mask"), [] { io::ExrReadOptions o; o.channel = "G"; return o; }());
        if (mask.width() != info.width || mask.height() != info.height)
            throw std::runtime_error("the mask's format does not match the plate");
    } else if (a.has("quad")) {
        surface = parseQuad(a.get("quad"), coords, info.height);
        haveSurface = true;
        mask = rasterizePolygon(info.width, info.height, {surface.begin(), surface.end()});
    } else {
        throw std::runtime_error("give a tracking region with --mask FILE or --quad x,y,...");
    }
    if (a.has("surface")) {
        surface = parseQuad(a.get("surface"), coords, info.height);
        haveSurface = true;
    }
    if (!haveSurface) {
        // Default surface: the matte's bounding box.
        int x0 = info.width, y0 = info.height, x1 = 0, y1 = 0;
        for (int y = 0; y < mask.height(); ++y)
            for (int x = 0; x < mask.width(); ++x)
                if (mask.at(x, y) > 0.5f) {
                    x0 = std::min(x0, x);
                    y0 = std::min(y0, y);
                    x1 = std::max(x1, x + 1);
                    y1 = std::max(y1, y + 1);
                }
        surface = {Vec2(x0, y1), Vec2(x1, y1), Vec2(x1, y0), Vec2(x0, y0)};
    }

    TrackParams tp;
    if (a.has("model") && !parseMotionModel(a.get("model"), tp.solver.model))
        throw std::runtime_error("--model must be translation, similarity, affine or perspective");
    if (a.has("strategy") && !parseReferenceStrategy(a.get("strategy"), tp.strategy))
        throw std::runtime_error("--strategy must be keyframe, frame-to-frame or hybrid");
    if (a.has("loss") && !parseRobustLoss(a.get("loss"), tp.solver.loss))
        throw std::runtime_error("--loss must be none, huber, tukey or cauchy");
    tp.pyramidLevels = a.getInt("levels", 0);
    tp.pauseBelowZncc = a.getDouble("pause-zncc", tp.pauseBelowZncc);

    const bool quiet = a.has("quiet");
    FrameSource source = [&](int f) -> Image {
        const std::string path = seq.path(f);
        if (!fs::exists(path)) return {};
        Image img = io::readExr(path, ro);
        applyCurve(curve, img);
        if (!quiet) std::cerr << "\rtracking frame " << f << "   " << std::flush;
        return img;
    };
    const TrackResult result = trackRegion(source, ref, mask, first, last, tp);
    if (!quiet) std::cerr << "\r";

    std::cerr << "Tracked " << result.frames.size() << " of " << (last - first + 1) << " frames ("
              << result.frames.front().frame << "-" << result.frames.back().frame << ").\n";
    if (result.pausedAt)
        std::cerr << "Paused at frame " << *result.pausedAt << ": " << result.pauseReason << ".\n";

    io::TrackDocument doc;
    doc.track = result;
    doc.surface = surface;
    doc.formatWidth = info.width;
    doc.formatHeight = info.height;
    doc.pixelAspect = info.pixelAspect;
    doc.source = seq.pattern();
    doc.settings = std::string("model=") + toString(tp.solver.model) + " strategy=" + toString(tp.strategy) +
                   " loss=" + toString(tp.solver.loss) + " curve=" + toString(curve);
    const std::string outPath = a.get("out", "track.json");
    io::writeTrackJson(outPath, doc);
    std::cerr << "Track written to " << outPath << "\n";

    NukeExportOptions no;
    no.formatHeight = info.height;
    no.surface = surface;
    no.stabilize = a.has("stabilize");
    no.nodeName = a.get("name", "PlanarTrack");
    emitNuke(nukeCornerPin2D(result, no), a);
    return result.pausedAt ? 2 : 0;
}

int cmdNuke(const Args& a) {
    const io::TrackDocument doc = io::readTrackJson(a.require("track"));
    NukeExportOptions no;
    no.formatHeight = doc.formatHeight;
    no.surface = doc.surface;
    no.stabilize = a.has("stabilize");
    no.nodeName = a.get("name", "PlanarTrack");
    Args out = a;
    if (!out.has("nuke") && !out.has("clipboard")) out.opts["nuke"] = "-";
    emitNuke(nukeCornerPin2D(doc.track, no), out);
    return 0;
}

int cmdEval(const Args& a) {
    const io::TrackDocument track = io::readTrackJson(a.require("track"));
    const io::TrackDocument truth = io::readTrackJson(a.require("truth"));
    double sum = 0.0, worst = 0.0;
    int n = 0, worstFrame = 0;
    for (const TrackedFrame& f : track.track.frames) {
        const TrackedFrame* t = truth.track.find(f.frame);
        if (!t) continue;
        for (int c = 0; c < 4; ++c) {
            const double d = (applyH(f.h, track.surface[c]) - applyH(t->h, track.surface[c])).norm();
            sum += d;
            ++n;
            if (d > worst) {
                worst = d;
                worstFrame = f.frame;
            }
        }
    }
    if (n == 0) throw std::runtime_error("no frames in common between track and truth");
    std::printf("frames %d  mean corner error %.4f px  max %.4f px (frame %d)\n", n / 4, sum / n, worst,
                worstFrame);
    return 0;
}

int cmdInfo(const Args& a) {
    if (a.positional.empty()) throw std::runtime_error("usage: ptrack info FILE.exr");
    const io::ExrInfo i = io::probeExr(a.positional[0], a.getInt("part", 0));
    std::printf("display %dx%d at (%d,%d)  data (%d,%d)-(%d,%d)  pixel aspect %.4g  parts %d\nchannels:",
                i.width, i.height, i.displayMinX, i.displayMinY, i.dataMinX, i.dataMinY, i.dataMaxX,
                i.dataMaxY, i.pixelAspect, i.parts);
    for (const auto& c : i.channels) std::printf(" %s", c.c_str());
    std::printf("\n");
    return 0;
}

void usage() {
    std::cerr <<
        R"(ptrack - planar tracker (engine CLI)

  ptrack synth --out DIR [--frames N] [--noise S] [--flicker F] [--occluder] [--seed N]
  ptrack track --seq PATTERN --ref FRAME (--mask FILE | --quad x,y,x,y,x,y,x,y)
               [--range A-B] [--surface x,y,...] [--coords internal|nuke]
               [--model perspective] [--strategy hybrid] [--loss huber] [--levels N]
               [--curve auto|acescct|none] [--layer L] [--channel luma|R|G|B|...]
               [--out track.json] [--nuke FILE|-] [--clipboard] [--stabilize] [--name NODE]
  ptrack nuke  --track FILE [--stabilize] [--name NODE] [--nuke FILE|-] [--clipboard]
  ptrack eval  --track FILE --truth FILE
  ptrack info  FILE.exr

Quads list corners bottom-left, bottom-right, top-right, top-left. Internal
coordinates have y down from the top of the frame; --coords nuke takes y up,
as Nuke shows it. See docs/conventions.md.
)";
}

} // namespace

int main(int argc, char** argv) {
    const Args a = parseArgs(argc, argv);
    try {
        if (a.command == "synth") return cmdSynth(a);
        if (a.command == "track") return cmdTrack(a);
        if (a.command == "nuke") return cmdNuke(a);
        if (a.command == "eval") return cmdEval(a);
        if (a.command == "info") return cmdInfo(a);
        usage();
        return a.command.empty() || a.command == "help" || a.has("help") ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "ptrack " << a.command << ": " << e.what() << "\n";
        return 1;
    }
}
