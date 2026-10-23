// The frame being received: what the receivers write into, what the output
// task shows (docs/DESIGN.md sections 4.2, 5.1 and 5.4).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <atomic>
#include <mutex>
#include <vector>

#include "pixled/mapping.h"

namespace pixled {

enum class Protocol : uint8_t { None, ArtNet, Sacn, Ddp };

// Thread-safe: receivers call dmx(), sync() and ddp() from their tasks, the
// output task ready() and take(). Each holds one lock for one copy.
class Frames {
public:
    // Without sync, a frame closes this long after its first data.
    static constexpr uint64_t kCloseAfterUs = 25000;
    // A sync seen this recently: frames close on syncs only.
    static constexpr uint64_t kSyncHoldUs = 5000000;
    // A universe's source keeps it until silent this long.
    static constexpr uint64_t kSourceTimeoutUs = 3000000;
    static constexpr size_t kMaxUniverses = 64;

    struct Stats {
        uint32_t packets = 0;   // data packets taken
        uint32_t refused = 0;   // from a second source, or past the outputs
        uint32_t syncs = 0;
        uint32_t closed = 0;    // frames closed
        uint32_t dropped = 0;   // closed again before the output took them
        uint32_t taken = 0;     // frames the output took
    };

    // The outputs, in order: their bytes end to end in one buffer, which DDP
    // addresses directly. Clears it (black) and every source. False if
    // their universes are more than kMaxUniverses (nothing changes then).
    bool configure(const std::vector<OutputMap>& outputs);

    // The universes the outputs read, in order.
    std::vector<uint16_t> universes() const;
    size_t bytes() const;

    // Called as each frame closes, under the lock, from the task that closed
    // it: to wake the output task. Before any receiver starts.
    void onClose(void (*fn)(void*), void* context) {
        closeFn_ = fn;
        closeContext_ = context;
    }

    // Receivers. `source` is the sender's address; `nowUs` the arrival.
    // sACN's sources are chosen upstream (by priority): any is taken.
    void dmx(Protocol p, uint16_t universe, const uint8_t* data, size_t length, uint32_t source, uint64_t nowUs);
    void sync(Protocol p, uint32_t source, uint64_t nowUs);
    // DDP: `length` bytes at `offset` into the outputs' bytes; `push` shows
    // them.
    void ddp(uint32_t offset, const uint8_t* data, size_t length, bool push, uint32_t source, uint64_t nowUs);

    // The output task: true when a frame is ready to show (closed, or its
    // time without sync has passed).
    bool ready(uint64_t nowUs);
    // Gives the frame's bytes to `render` under the lock, then starts the
    // next frame. `render(const uint8_t* bytes, size_t size)`.
    template <class F>
    void take(F&& render) {
        std::lock_guard<std::mutex> lock(mutex_);
        render(buffer_.data(), buffer_.size());
        restart();
        taken_++;
    }

    Stats stats() const;
    // When data was last taken (0: never), and how.
    uint64_t lastDataUs() const { return lastDataUs_.load(std::memory_order_relaxed); }
    Protocol lastProtocol() const { return lastProtocol_.load(std::memory_order_relaxed); }
    uint32_t lastSource() const { return lastSource_.load(std::memory_order_relaxed); }
    // Whether frames close on syncs (one seen in the last kSyncHoldUs).
    bool syncing(uint64_t nowUs) const;
    // Universes with data in the frame being received or the last closed.
    size_t universesReceived() const;

private:
    struct Track {
        uint16_t universe = 0;
        Protocol protocol = Protocol::None;
        uint32_t source = 0;
        uint64_t heardUs = 0;
        bool received = false;   // in the frame being received
        bool lastFrame = false;  // in the last frame closed
    };

    Track* find(uint16_t universe);
    // A source may write `t`: its own, or a new one once the other is silent.
    bool claim(Track& t, Protocol p, uint32_t source, uint64_t nowUs);
    void close();
    void restart();
    void heard(Protocol p, uint32_t source, uint64_t nowUs);

    mutable std::mutex mutex_;
    std::vector<OutputMap> outputs_;
    std::vector<size_t> offsets_;
    std::vector<uint8_t> buffer_;
    std::vector<Track> tracks_;  // by universe
    // DDP's one source.
    uint32_t ddpSource_ = 0;
    uint64_t ddpHeardUs_ = 0;

    void (*closeFn_)(void*) = nullptr;
    void* closeContext_ = nullptr;

    bool pending_ = false;  // data since the last frame
    bool ready_ = false;
    uint64_t pendingSinceUs_ = 0;
    std::atomic<uint64_t> syncUs_{0};

    std::atomic<uint64_t> lastDataUs_{0};
    std::atomic<Protocol> lastProtocol_{Protocol::None};
    std::atomic<uint32_t> lastSource_{0};
    std::atomic<uint32_t> packets_{0}, refused_{0}, syncs_{0}, closed_{0}, dropped_{0}, taken_{0};
};

}  // namespace pixled
