#include "ptrack/frame_cache.h"

namespace ptrack {

namespace {

size_t sizeOf(const Image& img) { return size_t(img.width()) * img.height() * sizeof(float); }

} // namespace

FrameCache::FrameCache(FrameSource source, size_t budgetBytes, int readAhead)
    : source_(std::move(source)), budget_(budgetBytes), readAhead_(readAhead) {
    if (readAhead_ > 0) thread_ = std::thread([this] { worker(); });
}

FrameCache::~FrameCache() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
}

FrameCache::Ptr FrameCache::load(int frame) {
    std::lock_guard<std::mutex> lock(sourceMutex_);
    return std::make_shared<const Image>(source_(frame));
}

void FrameCache::insert(int frame, Ptr img) {
    if (entries_.count(frame)) return;
    const size_t sz = sizeOf(*img);
    lru_.push_front(frame);
    entries_[frame] = {std::move(img), lru_.begin()};
    bytes_ += sz;
    // Evict least recently used, but never the frame just inserted.
    while (bytes_ > budget_ && lru_.size() > 1) {
        const int victim = lru_.back();
        lru_.pop_back();
        bytes_ -= sizeOf(*entries_[victim].first);
        entries_.erase(victim);
        ++stats_.evicted;
    }
}

void FrameCache::schedule(int frame) {
    if (entries_.count(frame) || inFlight_.count(frame)) return;
    for (int q : queue_)
        if (q == frame) return;
    queue_.push_back(frame);
}

Image FrameCache::get(int frame) {
    std::unique_lock<std::mutex> lock(mutex_);
    // Read ahead in the direction of travel.
    const int dir = (haveLast_ && frame < lastRequest_) ? -1 : 1;
    lastRequest_ = frame;
    haveLast_ = true;
    queue_.clear(); // stale requests from a previous direction or position
    for (int k = 1; k <= readAhead_; ++k) schedule(frame + dir * k);
    wake_.notify_one();

    while (true) {
        auto it = entries_.find(frame);
        if (it != entries_.end()) {
            ++stats_.hits;
            lru_.splice(lru_.begin(), lru_, it->second.second);
            return *it->second.first;
        }
        if (!inFlight_.count(frame)) break;
        loaded_.wait(lock); // the worker is reading it right now
    }
    ++stats_.misses;
    inFlight_.insert(frame);
    lock.unlock();
    Ptr img = load(frame);
    lock.lock();
    inFlight_.erase(frame);
    insert(frame, img);
    loaded_.notify_all();
    return *img;
}

void FrameCache::worker() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (true) {
        wake_.wait(lock, [this] { return stop_ || !queue_.empty(); });
        if (stop_) return;
        const int frame = queue_.front();
        queue_.pop_front();
        if (entries_.count(frame) || inFlight_.count(frame)) continue;
        inFlight_.insert(frame);
        lock.unlock();
        Ptr img = load(frame);
        lock.lock();
        inFlight_.erase(frame);
        insert(frame, img);
        ++stats_.prefetched;
        loaded_.notify_all();
    }
}

FrameSource FrameCache::source() {
    return [this](int frame) { return get(frame); };
}

FrameCache::Stats FrameCache::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

} // namespace ptrack
