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
#include "ptrack/io/media.h"
#include "ptrack/io/sequence.h"
#include "ptrack/io/track_json.h"
#include "ptrack/io/tracking_space.h"
#include "ptrack/raster.h"
#include "ptrack/synth.h"
#include "ptrack/tracker.h"
#include "ptrack/warp.h"

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
    p.morph = a.getDouble("morph", 0.0);
    p.shadow = a.getDouble("shadow", 0.0);
    p.seed = static_cast<unsigned>(a.getInt("seed", 1));
    // --overscan N renders N extra pixels on every side and stores them as EXR
    // data outside the display window; truth, surface and mask stay in display
    // coordinates, so the plane can be tracked into the overscan.
    const int over = a.getInt("overscan", 0);
    const int dispW = p.width, dispH = p.height;
    p.width += 2 * over;
    p.height += 2 * over;
    const fs::path out = a.require("out");
    fs::create_directories(out);

    SynthShot shot = makeSynthShot(p);
    io::ExrWriteOptions wo;
    wo.displayWidth = dispW;
    wo.displayHeight = dispH;
    wo.dataOriginX = wo.dataOriginY = -over;
    wo.pixelAspect = static_cast<float>(a.getDouble("pixel-aspect", 1.0));
    for (int i = 0; i < p.frames; ++i) {
        char name[64];
        std::snprintf(name, sizeof name, "plate.%04d.exr", p.startFrame + i);
        io::writeExrGrey((out / name).string(), shot.frames[i], wo);
    }
    if (over > 0) {
        Image disp(dispW, dispH);
        for (int y = 0; y < dispH; ++y)
            for (int x = 0; x < dispW; ++x) disp.at(x, y) = shot.mask.at(x + over, y + over);
        shot.mask = std::move(disp);
        for (Mat3& h : shot.truth) h = translateHomography(h, Vec2(-over, -over));
        for (Vec2& c : shot.surface) c -= Vec2(over, over);
    }
    io::writeExrGrey((out / "mask.exr").string(), shot.mask);

    io::TrackDocument truth;
    truth.formatWidth = dispW;
    truth.formatHeight = dispH;
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
    io::MediaOptions mo;
    mo.layer = a.get("layer", "");
    mo.part = a.getInt("part", 0);
    mo.overscan = a.has("overscan");
    if (a.get("start") == "timecode") mo.movieStartFromTimecode = true;
    else mo.movieStart = a.getInt("start", 1);
    const auto media = io::openMedia(a.require("seq"), mo);
    if (!a.has("quiet")) std::cerr << "Source: " << media->description() << "\n";
    struct {
        int width, height;
        double pixelAspect;
    } const info{media->width(), media->height(), media->pixelAspect()};

    int first = media->first(), last = media->last();
    if (a.has("range")) {
        const auto r = io::Sequence::parseRange(a.get("range"));
        if (!r) throw std::runtime_error("--range must look like 1001-1100");
        first = r->first;
        last = r->second;
    }
    const int ref = a.has("ref") ? std::stoi(a.get("ref")) : first;
    if (ref < first || ref > last) throw std::runtime_error("--ref must lie inside the range");

    const std::string curveName = a.get("curve", "auto");
    TrackingCurve curve = media->sceneLinear() ? TrackingCurve::ACEScct : TrackingCurve::None;
    if (curveName != "auto" && !parseTrackingCurve(curveName, curve))
        throw std::runtime_error("--curve must be auto, acescct or none");
    const std::string channel = a.get("channel", "luma");
    std::unique_ptr<io::TrackingSpace> space;
    if (io::TrackingSpace::isColourChannel(channel)) {
        io::TrackingSpaceOptions so;
        so.channel = channel;
        so.ocioConfig = a.get("ocio", "");
        so.from = a.get("from", "");
        so.to = a.get("to", "");
        so.curve = curve;
        space = std::make_unique<io::TrackingSpace>(so);
        if (!a.has("quiet")) std::cerr << "Tracking space: " << space->description() << "\n";
    }

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
    tp.solver.pixelAspect = a.getDouble("pixel-aspect", info.pixelAspect);
    tp.carryRejections = a.getInt("carry-rejections", 1) != 0;
    tp.lightingFallback = a.getInt("lighting-fallback", 1) != 0;
    if (a.has("normalize") && !parseNormalize(a.get("normalize"), tp.preprocess.mode))
        throw std::runtime_error("--normalize must be none, bandpass or local-contrast");
    tp.preprocess.coarseSigma = a.getDouble("normalize-radius", tp.preprocess.coarseSigma);
    tp.preprocess.fineSigma = a.getDouble("normalize-fine", tp.preprocess.fineSigma);
    tp.reanchorAfter = a.getInt("reanchor-after", tp.reanchorAfter);

    const bool quiet = a.has("quiet");
    FrameSource source = [&](int f) -> Image {
        Image img;
        if (space) {
            img = space->apply(media->readRgb(f));
        } else {
            img = media->readChannel(f, channel);
            applyCurve(curve, img);
        }
        if (img.empty()) return img;
        if (!quiet) std::cerr << "\rtracking frame " << f << "   " << std::flush;
        return img;
    };
    // With overscan the tracked images are larger than the display window:
    // place the matte inside them, track, then express the track in
    // display-window coordinates again (docs/conventions.md, section 1).
    const Vec2 origin(media->originX(), media->originY());
    Image trackMask = mask;
    if (origin != Vec2::Zero()) {
        const Image refImg = source(ref);
        if (refImg.empty()) throw std::runtime_error("cannot read the reference frame");
        trackMask = Image(refImg.width(), refImg.height());
        for (int y = 0; y < mask.height(); ++y)
            for (int x = 0; x < mask.width(); ++x) {
                const int ax = x - media->originX(), ay = y - media->originY();
                if (ax >= 0 && ay >= 0 && ax < trackMask.width() && ay < trackMask.height())
                    trackMask.at(ax, ay) = mask.at(x, y);
            }
    }
    TrackResult result = trackRegion(source, ref, trackMask, first, last, tp);
    if (origin != Vec2::Zero())
        for (TrackedFrame& f : result.frames) f.h = translateHomography(f.h, origin);
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
    doc.source = a.get("seq");
    doc.settings = std::string("model=") + toString(tp.solver.model) + " strategy=" + toString(tp.strategy) +
                   " loss=" + toString(tp.solver.loss) + " normalize=" + toString(tp.preprocess.mode) + " space=" +
                   (space ? space->description() : channel + ", curve " + toString(curve));
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
               [--overscan N] [--pixel-aspect N] [--morph M] [--shadow S]
  ptrack track --seq PATTERN|MOVIE [--ref FRAME] (--mask FILE | --quad x,y,x,y,x,y,x,y)
               [--range A-B] [--start N|timecode] [--surface x,y,...] [--coords internal|nuke]
               [--model perspective] [--strategy hybrid] [--loss huber] [--levels N]
               [--channel luma|R|G|B|A|layer.chan] [--layer L] [--part N] [--overscan]
               [--pixel-aspect N] [--normalize none|bandpass|local-contrast]
               [--normalize-radius PX] [--normalize-fine PX]
               [--curve auto|acescct|none] [--ocio CONFIG --from SPACE --to SPACE]
               [--out track.json] [--nuke FILE|-] [--clipboard] [--stabilize] [--name NODE]
  ptrack nuke  --track FILE [--stabilize] [--name NODE] [--nuke FILE|-] [--clipboard]
  ptrack eval  --track FILE --truth FILE
  ptrack info  FILE.exr

Movies (.mov, .mp4, .mxf) number their frames from --start (default 1, as
Nuke does) or from the file's timecode with --start timecode.

Tracking space: scene-linear EXRs default to the ACEScct curve, movies to none.
With an OCIO config (--ocio, or $OCIO) give --from and --to (colourspaces or
roles, e.g. --from scene_linear --to compositing_log) to convert instead.

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
