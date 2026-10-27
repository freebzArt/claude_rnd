#pragma once

#include "ptrack/tracker.h"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <list>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>

namespace ptrack {

// Caches decoded tracking frames and reads ahead on a background thread, so
// decoding the next frames overlaps with solving the current one. Frames are
// read through the wrapped source one at a time (movie decoders are not
// thread-safe), and each frame is decoded at most once while it fits in the
// memory budget. Read-ahead follows the direction of the last two requests,
// so backward tracking prefetches backwards.
class FrameCache {
public:
    struct Stats {
        size_t hits = 0, misses = 0, prefetched = 0, evicted = 0;
    };

    explicit FrameCache(FrameSource source, size_t budgetBytes = size_t(2) << 30, int readAhead = 4);
    ~FrameCache();
    FrameCache(const FrameCache&) = delete;
    FrameCache& operator=(const FrameCache&) = delete;

    // Thread-safe. Returns the frame (empty if the source has none).
    Image get(int frame);

    // A FrameSource that reads through this cache. The cache must outlive it.
    FrameSource source();

    Stats stats() const;

private:
    using Ptr = std::shared_ptr<const Image>;

    Ptr load(int frame);                         // read through the source (source mutex held inside)
    void insert(int frame, Ptr img);             // cache mutex held by caller
    void schedule(int frame);                    // cache mutex held by caller
    void worker();

    FrameSource source_;
    size_t budget_;
    int readAhead_;

    mutable std::mutex mutex_;                   // guards everything below
    std::condition_variable wake_, loaded_;
    std::unordered_map<int, std::pair<Ptr, std::list<int>::iterator>> entries_;
    std::list<int> lru_;                         // front = most recently used
    size_t bytes_ = 0;
    std::deque<int> queue_;                      // frames to prefetch
    std::set<int> inFlight_;                     // being read by either thread
    int lastRequest_ = 0;
    bool haveLast_ = false;
    bool stop_ = false;
    Stats stats_;

    std::mutex sourceMutex_;                     // one read at a time through the source
    std::thread thread_;
};

} // namespace ptrack
