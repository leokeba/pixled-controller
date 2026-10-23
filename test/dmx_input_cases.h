// Art-Net's and sACN's frames from TesserKIT's DMX services (docs/DESIGN.md
// section 5.1).
#pragma once

#include "rig.h"

using tesserkit::DmxFrame;
using tesserkit::DmxProtocol;
using tesserkit::DmxSync;

namespace {

DmxFrame frameOf(DmxProtocol p, uint16_t universe, const std::vector<uint8_t>& data, uint64_t atUs) {
    DmxFrame f;
    f.protocol = p;
    f.universe = universe;
    f.data = data.data();
    f.length = static_cast<uint16_t>(data.size());
    f.source = 0x0a000001;
    f.atUs = atUs;
    return f;
}

}  // namespace

TEST(dmx_input_frames) {
    Rig r;
    pixled::DmxInput input(r.controller);
    std::vector<uint8_t> red(510, 0);
    red[0] = 255;  // pixel 0's red
    input.frame(frameOf(DmxProtocol::ArtNet, 1, red, r.now));
    r.poll(pixled::Frames::kCloseAfterUs);
    CHECK_EQ(r.bus.pixel(1, 0), "0,255,0");
    CHECK(r.controller.frames().lastProtocol() == pixled::Protocol::ArtNet);
    // sACN, on its own universe.
    input.frame(frameOf(DmxProtocol::Sacn, 2, red, r.now));
    r.poll(pixled::Frames::kCloseAfterUs);
    CHECK_EQ(r.bus.pixel(2, 0), "0,255,0");
    CHECK(r.controller.frames().lastProtocol() == pixled::Protocol::Sacn);
}

TEST(dmx_input_switches) {
    Rig r;
    pixled::DmxInput input(r.controller);
    std::vector<uint8_t> data(510, 9);
    r.controller.settings.artnet = false;
    r.poll();
    input.frame(frameOf(DmxProtocol::ArtNet, 0, data, r.now));
    CHECK_EQ(r.controller.frames().stats().packets, 0u);
    input.frame(frameOf(DmxProtocol::Sacn, 0, data, r.now));
    CHECK_EQ(r.controller.frames().stats().packets, 1u);
    r.controller.settings.sacn = false;
    r.poll();
    input.frame(frameOf(DmxProtocol::Sacn, 0, data, r.now));
    DmxSync s;
    s.protocol = DmxProtocol::Sacn;
    input.sync(s);
    CHECK_EQ(r.controller.frames().stats().packets, 1u);
    CHECK_EQ(r.controller.frames().stats().syncs, 0u);
}

TEST(dmx_input_sync) {
    Rig r;
    pixled::DmxInput input(r.controller);
    DmxSync s;
    s.protocol = DmxProtocol::ArtNet;
    s.atUs = r.now;
    input.sync(s);
    std::vector<uint8_t> data(510, 7);
    for (uint16_t u = 0; u < 4; u++) input.frame(frameOf(DmxProtocol::ArtNet, u, data, r.now));
    // Complete, but waiting for the sync.
    r.poll(pixled::Frames::kCloseAfterUs);
    CHECK_EQ(r.bus.pixel(0, 0), "0,0,0");
    s.atUs = r.now;
    input.sync(s);
    r.poll();
    CHECK_EQ(r.bus.pixel(0, 0), "7,7,7");
}

TEST(controller_tells_its_universes) {
    Rig r(false);
    std::vector<uint16_t> told;
    int calls = 0;
    r.controller.onUniverses([&](const std::vector<uint16_t>& u) {
        told = u;
        calls++;
    });
    r.board.choice.preset = pixled::BoardPreset::Elite4d;
    r.controller.begin(r.board.boot());
    r.poll();
    CHECK_EQ(calls, 1);
    CHECK_EQ(told.size(), 4u);
    CHECK_EQ(told[3], 3);
    r.controller.settings.outputs[0].pixels = 400;  // universes 0–2, 2 shared with output 3
    r.controller.settings.outputs[1].universe = 10;
    r.poll();
    CHECK_EQ(calls, 2);
    CHECK_EQ(told.size(), 5u);  // 0, 1, 2, 3, 10
    CHECK_EQ(told.back(), 10);
    // Brightness changes no universe.
    r.controller.settings.brightness = 10;
    r.poll();
    CHECK_EQ(calls, 2);
}
