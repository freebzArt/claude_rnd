#include "ptrack/io/sequence.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <regex>
#include <stdexcept>

namespace fs = std::filesystem;

namespace ptrack::io {

Sequence::Sequence(std::string pattern) {
    static const std::regex printfRe(R"(%0?(\d*)d)");
    static const std::regex hashRe(R"(#+)");
    static const std::regex numberedRe(R"((.*[._])(\d+)(\.[A-Za-z0-9]+)$)");
    std::smatch m;
    if (std::regex_search(pattern, m, printfRe)) {
        const std::string width = m[1].str().empty() ? "1" : m[1].str();
        printf_ = m.prefix().str() + "%0" + width + "d" + m.suffix().str();
    } else if (std::regex_search(pattern, m, hashRe)) {
        printf_ = m.prefix().str() + "%0" + std::to_string(m[0].length()) + "d" + m.suffix().str();
    } else if (std::regex_match(pattern, m, numberedRe)) {
        printf_ = m[1].str() + "%0" + std::to_string(m[2].length()) + "d" + m[3].str();
    } else {
        throw std::runtime_error("'" + pattern + "' has no frame number (use %04d or ####)");
    }
    // Escape any other % so the pattern is a safe printf format.
    std::string safe;
    for (size_t i = 0; i < printf_.size(); ++i) {
        if (printf_[i] == '%' && printf_.compare(i, 2, "%0") != 0) safe += "%%";
        else safe += printf_[i];
    }
    printf_ = safe;
}

std::string Sequence::path(int frame) const {
    char buf[4096];
    std::snprintf(buf, sizeof buf, printf_.c_str(), frame);
    return buf;
}

std::vector<int> Sequence::scan() const {
    const fs::path p(printf_);
    const fs::path dir = p.has_parent_path() ? p.parent_path() : fs::path(".");
    const std::string name = p.filename().string();
    static const std::regex fieldRe(R"(%0(\d+)d)");
    std::smatch m;
    std::vector<int> frames;
    if (!std::regex_search(name, m, fieldRe) || !fs::is_directory(dir)) return frames;
    auto esc = [](const std::string& s) {
        static const std::regex special(R"([.^$|()\[\]{}*+?\\])");
        return std::regex_replace(std::regex_replace(s, std::regex("%%"), "%"), special, R"(\$&)");
    };
    const std::regex fileRe(esc(m.prefix().str()) + "(-?\\d+)" + esc(m.suffix().str()));
    for (const auto& entry : fs::directory_iterator(dir)) {
        std::smatch fm;
        const std::string fname = entry.path().filename().string();
        if (std::regex_match(fname, fm, fileRe)) frames.push_back(std::stoi(fm[1].str()));
    }
    std::sort(frames.begin(), frames.end());
    return frames;
}

std::optional<std::pair<int, int>> Sequence::parseRange(const std::string& s) {
    static const std::regex re(R"(^\s*(-?\d+)\s*(?:-\s*(-?\d+))?\s*$)");
    std::smatch m;
    if (!std::regex_match(s, m, re)) return std::nullopt;
    const int a = std::stoi(m[1].str());
    const int b = m[2].matched ? std::stoi(m[2].str()) : a;
    if (b < a) return std::nullopt;
    return std::make_pair(a, b);
}

} // namespace ptrack::io
