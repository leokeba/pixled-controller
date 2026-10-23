#include "pixled/frames.h"

#include <string.h>

#include <algorithm>

namespace pixled {

bool Frames::configure(const std::vector<OutputMap>& outputs) {
    std::vector<uint16_t> list;
    for (const OutputMap& m : outputs) {
        if (m.pixels == 0) continue;
        for (uint32_t u = m.universe; u <= m.lastUniverse(); u++) list.push_back(static_cast<uint16_t>(u));
    }
    std::sort(list.begin(), list.end());
    list.erase(std::unique(list.begin(), list.end()), list.end());
    if (list.size() > kMaxUniverses) return false;

    std::lock_guard<std::mutex> lock(mutex_);
    outputs_ = outputs;
    offsets_.clear();
    size_t total = 0;
    for (const OutputMap& m : outputs_) {
        offsets_.push_back(total);
        total += m.bytes();
    }
    buffer_.assign(total, 0);
    buffer_.shrink_to_fit();
    tracks_.clear();
    for (uint16_t u : list) {
        Track t;
        t.universe = u;
        tracks_.push_back(t);
    }
    ddpSource_ = 0;
    pending_ = false;
    ready_ = false;
    return true;
}

std::vector<uint16_t> Frames::universes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<uint16_t> list;
    for (const Track& t : tracks_) list.push_back(t.universe);
    return list;
}

size_t Frames::bytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return buffer_.size();
}

Frames::Track* Frames::find(uint16_t universe) {
    auto it = std::lower_bound(tracks_.begin(), tracks_.end(), universe,
                               [](const Track& t, uint16_t u) { return t.universe < u; });
    return it != tracks_.end() && it->universe == universe ? &*it : nullptr;
}

bool Frames::claim(Track& t, Protocol p, uint32_t source, uint64_t nowUs) {
    bool own = t.protocol == p && t.source == source;
    bool free = t.protocol == Protocol::None || nowUs - t.heardUs >= kSourceTimeoutUs;
    // sACN's sources are chosen by priority before they get here.
    if (!own && !free && !(p == Protocol::Sacn && t.protocol == Protocol::Sacn)) return false;
    t.protocol = p;
    t.source = source;
    t.heardUs = nowUs;
    return true;
}

void Frames::heard(Protocol p, uint32_t source, uint64_t nowUs) {
    packets_++;
    lastDataUs_.store(nowUs, std::memory_order_relaxed);
    lastProtocol_.store(p, std::memory_order_relaxed);
    lastSource_.store(source, std::memory_order_relaxed);
    if (!pending_) {
        pending_ = true;
        pendingSinceUs_ = nowUs;
    }
}

void Frames::dmx(Protocol p, uint16_t universe, const uint8_t* data, size_t length, uint32_t source,
                 uint64_t nowUs) {
    std::lock_guard<std::mutex> lock(mutex_);
    Track* t = find(universe);
    if (!t || !claim(*t, p, source, nowUs)) {
        refused_++;
        return;
    }
    for (size_t i = 0; i < outputs_.size(); i++) {
        Span s = span(outputs_[i], universe);
        if (s.length == 0 || length <= s.from) continue;
        size_t n = std::min<size_t>(s.length, length - s.from);
        memcpy(buffer_.data() + offsets_[i] + s.to, data + s.from, n);
    }
    heard(p, source, nowUs);
    t->received = true;
    if (syncing(nowUs)) return;
    for (const Track& other : tracks_) {
        if (!other.received) return;
    }
    close();
}

void Frames::sync(Protocol p, uint32_t source, uint64_t nowUs) {
    (void)p;
    (void)source;
    syncs_++;
    syncUs_.store(nowUs, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(mutex_);
    if (pending_) close();
}

void Frames::ddp(uint32_t offset, const uint8_t* data, size_t length, bool push, uint32_t source, uint64_t nowUs) {
    std::lock_guard<std::mutex> lock(mutex_);
    bool free = ddpSource_ == 0 || nowUs - ddpHeardUs_ >= kSourceTimeoutUs;
    if ((ddpSource_ != source && !free) || offset >= buffer_.size()) {
        refused_++;
        return;
    }
    ddpSource_ = source;
    ddpHeardUs_ = nowUs;
    memcpy(buffer_.data() + offset, data, std::min<size_t>(length, buffer_.size() - offset));
    heard(Protocol::Ddp, source, nowUs);
    if (push) close();
}

void Frames::close() {
    if (ready_) dropped_++;
    ready_ = true;
    closed_++;
    pending_ = false;
    // The next frame's universes, whether or not the output took this one.
    for (Track& t : tracks_) {
        t.lastFrame = t.received;
        t.received = false;
    }
    if (closeFn_) closeFn_(closeContext_);
}

void Frames::restart() {
    ready_ = false;
    pending_ = false;
}

bool Frames::syncing(uint64_t nowUs) const {
    uint64_t s = syncUs_.load(std::memory_order_relaxed);
    return s != 0 && nowUs - s < kSyncHoldUs;
}

bool Frames::ready(uint64_t nowUs) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ready_ && pending_ && !syncing(nowUs) && nowUs - pendingSinceUs_ >= kCloseAfterUs) close();
    return ready_;
}

size_t Frames::universesReceived() const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t n = 0;
    for (const Track& t : tracks_) {
        if (t.received || t.lastFrame) n++;  // the frame being received, or the last closed
    }
    return n;
}

Frames::Stats Frames::stats() const {
    Stats s;
    s.packets = packets_.load(std::memory_order_relaxed);
    s.refused = refused_.load(std::memory_order_relaxed);
    s.syncs = syncs_.load(std::memory_order_relaxed);
    s.closed = closed_.load(std::memory_order_relaxed);
    s.dropped = dropped_.load(std::memory_order_relaxed);
    s.taken = taken_.load(std::memory_order_relaxed);
    return s;
}

}  // namespace pixled
