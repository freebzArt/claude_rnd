#include "ptrack/frame_cache.h"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <thread>

using namespace ptrack;

namespace {

struct CountingSource {
    std::mutex m;
    std::map<int, int> reads;
    std::atomic<int> concurrent{0}, maxConcurrent{0};

    FrameSource source(int first, int last) {
        return [this, first, last](int f) -> Image {
            const int now = ++concurrent;
            int prev = maxConcurrent.load();
            while (now > prev && !maxConcurrent.compare_exchange_weak(prev, now)) {
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(3)); // "decode"
            {
                std::lock_guard<std::mutex> lock(m);
                ++reads[f];
            }
            --concurrent;
            if (f < first || f > last) return {};
            return Image(64, 32, static_cast<float>(f));
        };
    }
};

} // namespace

TEST_CASE("frame cache: forward then backward reads decode each frame once and read ahead") {
    CountingSource cs;
    FrameCache cache(cs.source(1001, 1020), size_t(64) << 20, 4);
    for (int f = 1010; f <= 1020; ++f) {
        const Image img = cache.get(f);
        REQUIRE_FALSE(img.empty());
        CHECK(img.at(0, 0) == doctest::Approx(f));
        std::this_thread::sleep_for(std::chrono::milliseconds(5)); // "solve"
    }
    for (int f = 1009; f >= 1001; --f) CHECK(cache.get(f).at(1, 1) == doctest::Approx(f));
    CHECK(cache.get(1021).empty());
    for (const auto& [f, n] : cs.reads) CHECK_MESSAGE(n == 1, "frame " << f << " read " << n << " times");
    CHECK(cs.maxConcurrent.load() == 1); // never two reads through the source at once
    const auto st = cache.stats();
    CHECK(st.prefetched > 0);
    CHECK(st.hits > 0);
}

TEST_CASE("frame cache: stays within its memory budget") {
    CountingSource cs;
    const size_t frameBytes = 64 * 32 * sizeof(float);
    FrameCache cache(cs.source(1, 100), 5 * frameBytes, 0); // no read-ahead
    for (int f = 1; f <= 20; ++f) cache.get(f);
    CHECK(cache.stats().evicted == 15);
    cache.get(20); // still cached
    cache.get(1);  // evicted: read again
    CHECK(cs.reads[20] == 1);
    CHECK(cs.reads[1] == 2);
}
