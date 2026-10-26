#pragma once

#include <optional>
#include <string>
#include <vector>

namespace ptrack::io {

// A frame-numbered file pattern. Accepts the forms artists paste from Nuke and
// Flame: printf style "plate.%04d.exr", hashes "plate.####.exr" (one # per
// digit), or a literal file name with a frame number, which is converted.
class Sequence {
public:
    explicit Sequence(std::string pattern);

    std::string path(int frame) const;
    const std::string& pattern() const { return printf_; }

    // Frames that exist on disk, sorted. Gaps are kept as gaps.
    std::vector<int> scan() const;

    // Parse "1001-1100" or "1001" into an inclusive range.
    static std::optional<std::pair<int, int>> parseRange(const std::string& s);

private:
    std::string printf_; // normalised to a printf pattern with one %0Nd
};

} // namespace ptrack::io
