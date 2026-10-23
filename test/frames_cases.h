// Frames: sources, closing, sync, DDP (docs/DESIGN.md sections 5.1 and 5.4).
#pragma once

#include "rig.h"

using pixled::Frames;
using pixled::Protocol;

namespace {

constexpr uint32_t kConsole = 0x0a000001, kOther = 0x0a000002;

// Two outputs: 170 RGB pixels on universe 0, 200 on universes 1–2.
struct FramesRig {
    Frames f;
    uint64_t now = 1000000;
    std::vector<uint8_t> shown;

    FramesRig() {
        pixled::OutputMap a, b;
        a.pixels = 170;
        a.universe = 0;
        b.pixels = 200;
        b.universe = 1;
        f.configure({a, b});
    }
    void dmx(uint16_t u, uint8_t v, uint32_t source = kConsole, Protocol p = Protocol::ArtNet) {
        std::vector<uint8_t> data(512, v);
        f.dmx(p, u, data.data(), data.size(), source, now);
    }
    bool take() {
        if (!f.ready(now)) return false;
        f.take([this](const uint8_t* b, size_t n) { shown.assign(b, b + n); });
        return true;
    }
};

}  // namespace

TEST(frames_layout) {
    FramesRig r;
    CHECK_EQ(r.f.bytes(), (170u + 200u) * 3);
    std::vector<uint16_t> u = r.f.universes();
    CHECK_EQ(u.size(), 3u);
    CHECK_EQ(u[2], 2);
    r.dmx(1, 7);
    r.now += Frames::kCloseAfterUs;
    CHECK(r.take());
    // Universe 1's bytes start the second output.
    CHECK_EQ(r.shown[509], 0);
    CHECK_EQ(r.shown[510], 7);
    CHECK_EQ(r.shown[510 + 509], 7);
    CHECK_EQ(r.shown[1020], 0);
}

TEST(frames_close_when_complete) {
    FramesRig r;
    r.dmx(0, 1);
    r.dmx(1, 2);
    CHECK(!r.f.ready(r.now));
    r.dmx(2, 3);
    CHECK(r.f.ready(r.now));
    CHECK(r.take());
    CHECK_EQ(r.f.stats().closed, 1u);
    CHECK_EQ(r.f.universesReceived(), 3u);
    // The next frame starts empty.
    r.dmx(0, 4);
    CHECK(!r.f.ready(r.now));
}

TEST(frames_close_after_a_while) {
    // A lost datagram doesn't stall the strips.
    FramesRig r;
    r.dmx(0, 1);
    r.now += Frames::kCloseAfterUs - 1;
    CHECK(!r.f.ready(r.now));
    r.now += 1;
    CHECK(r.take());
    CHECK_EQ(r.shown[0], 1);
}

TEST(frames_dropped) {
    FramesRig r;
    for (uint8_t v = 1; v <= 2; v++) {
        r.dmx(0, v);
        r.dmx(1, v);
        r.dmx(2, v);
    }
    CHECK_EQ(r.f.stats().dropped, 1u);
    CHECK(r.take());
    CHECK_EQ(r.shown[0], 2);  // the latest
}

TEST(frames_on_sync) {
    FramesRig r;
    r.f.sync(Protocol::ArtNet, kConsole, r.now);
    CHECK(r.f.syncing(r.now));
    r.dmx(0, 1);
    r.dmx(1, 1);
    r.dmx(2, 1);
    // Complete, but waiting for the sync; and no timeout.
    r.now += Frames::kCloseAfterUs * 4;
    CHECK(!r.f.ready(r.now));
    r.f.sync(Protocol::ArtNet, kConsole, r.now);
    CHECK(r.take());
    // A sync without new data closes nothing.
    r.f.sync(Protocol::ArtNet, kConsole, r.now);
    CHECK(!r.f.ready(r.now));
    // Syncs stop: frames close as they complete again.
    r.now += Frames::kSyncHoldUs;
    CHECK(!r.f.syncing(r.now));
    r.dmx(0, 2);
    r.dmx(1, 2);
    r.dmx(2, 2);
    CHECK(r.f.ready(r.now));
}

TEST(frames_one_source_per_universe) {
    FramesRig r;
    r.dmx(0, 1, kConsole);
    r.dmx(0, 9, kOther);
    CHECK_EQ(r.f.stats().refused, 1u);
    r.now += Frames::kCloseAfterUs;
    CHECK(r.take());
    CHECK_EQ(r.shown[0], 1);
    // The other takes it once the first is silent; a universe nobody reads
    // is refused.
    r.now += Frames::kSourceTimeoutUs;
    r.dmx(0, 9, kOther);
    r.dmx(7, 9, kOther);
    CHECK_EQ(r.f.stats().refused, 2u);
    r.now += Frames::kCloseAfterUs;
    CHECK(r.take());
    CHECK_EQ(r.shown[0], 9);
    CHECK_EQ(r.f.lastSource(), kOther);
    CHECK(r.f.lastProtocol() == Protocol::ArtNet);
}

TEST(frames_sacn_sources_chosen_upstream) {
    // TesserKIT's Sacn hands the highest priority's frames: any source is
    // taken.
    FramesRig r;
    r.dmx(0, 1, kConsole, Protocol::Sacn);
    r.dmx(0, 2, kOther, Protocol::Sacn);
    CHECK_EQ(r.f.stats().refused, 0u);
    // Art-Net doesn't take a universe sACN holds.
    r.dmx(0, 3, kConsole, Protocol::ArtNet);
    CHECK_EQ(r.f.stats().refused, 1u);
}

TEST(frames_ddp) {
    FramesRig r;
    uint8_t px[6] = {1, 2, 3, 4, 5, 6};
    r.f.ddp(510, px, 6, false, kConsole, r.now);
    CHECK(!r.f.ready(r.now));
    r.f.ddp(0, px, 3, true, kConsole, r.now);
    CHECK(r.take());
    CHECK_EQ(r.shown[0], 1);
    CHECK_EQ(r.shown[510], 1);  // the second output's first pixel
    CHECK_EQ(r.shown[515], 6);
    // Past the end: cut; from beyond it: refused.
    std::vector<uint8_t> big(2000, 8);
    r.f.ddp(1000, big.data(), big.size(), true, kConsole, r.now);
    CHECK(r.take());
    CHECK_EQ(r.shown.back(), 8);
    r.f.ddp(5000, px, 3, true, kConsole, r.now);
    CHECK_EQ(r.f.stats().refused, 1u);
    // One DDP source at a time.
    r.f.ddp(0, px, 3, true, kOther, r.now);
    CHECK_EQ(r.f.stats().refused, 2u);
}

TEST(frames_wake_on_close) {
    FramesRig r;
    int woken = 0;
    r.f.onClose([](void* n) { (*static_cast<int*>(n))++; }, &woken);
    r.dmx(0, 1);
    r.dmx(1, 1);
    CHECK_EQ(woken, 0);
    r.dmx(2, 1);
    CHECK_EQ(woken, 1);
    CHECK(r.take());
    // A timeout closes the next in the output task's own ready().
    r.dmx(0, 2);
    r.now += Frames::kCloseAfterUs;
    CHECK(r.f.ready(r.now));
    CHECK_EQ(woken, 2);
}

TEST(frames_too_many_universes) {
    Frames f;
    pixled::OutputMap m;
    m.pixels = 170 * 65;
    CHECK(!f.configure({m}));
    m.pixels = 170 * 64;
    CHECK(f.configure({m}));
    CHECK_EQ(f.universes().size(), 64u);
}
