#include "ptrack/export_nuke.h"

#include <cstdio>
#include <sstream>

namespace ptrack {

namespace {

std::string num(double v, int precision) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", precision, v);
    std::string s = buf;
    // Trim trailing zeros to keep scripts compact ("12.5000" -> "12.5", "3.0000" -> "3").
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    if (s == "-0") s = "0";
    return s;
}

// Nuke animation curve: "curve x1001 v v v x1010 v", jumping with an x-key at each gap.
std::string curve(const TrackResult& track, int corner, int axis, const NukeExportOptions& o) {
    std::ostringstream ss;
    ss << "{curve";
    int expect = -1;
    for (const TrackedFrame& f : track.frames) {
        const Vec2 p = toNuke(applyH(f.h, o.surface[corner]), o.formatHeight);
        if (f.frame != expect) ss << " x" << f.frame;
        ss << ' ' << num(axis == 0 ? p.x() : p.y(), o.precision);
        expect = f.frame + 1;
    }
    ss << '}';
    return ss.str();
}

} // namespace

std::string nukeCornerPin2D(const TrackResult& track, const NukeExportOptions& o) {
    std::ostringstream ss;
    ss << "set cut_paste_input [stack 0]\n"
       << "push $cut_paste_input\n"
       << "CornerPin2D {\n";
    for (int c = 0; c < 4; ++c)
        ss << " to" << (c + 1) << " {" << curve(track, c, 0, o) << ' ' << curve(track, c, 1, o) << "}\n";
    if (o.stabilize) ss << " invert true\n";
    for (int c = 0; c < 4; ++c) {
        const Vec2 p = toNuke(o.surface[c], o.formatHeight);
        ss << " from" << (c + 1) << " {" << num(p.x(), o.precision) << ' ' << num(p.y(), o.precision)
           << "}\n";
    }
    ss << " name " << o.nodeName << "\n"
       << " label \"ref frame " << track.referenceFrame << "\"\n"
       << " selected true\n"
       << "}\n";
    return ss.str();
}

} // namespace ptrack
