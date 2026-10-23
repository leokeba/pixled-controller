// DDP packets (docs/DESIGN.md section 5.1).
#pragma once

#include "rig.h"

using pixled::DdpPacket;
using pixled::parseDdp;

TEST(ddp_parses) {
    // Version 1, push; sequence 5; RGB 8 bits; display; offset 0x01020304;
    // 3 bytes.
    const uint8_t p[] = {0x41, 0x05, 0x0b, 0x01, 0x01, 0x02, 0x03, 0x04, 0x00, 0x03, 10, 20, 30};
    DdpPacket d;
    CHECK(parseDdp(p, sizeof p, d));
    CHECK(d.push);
    CHECK(!d.query);
    CHECK_EQ(d.sequence, 5);
    CHECK_EQ(d.type, 0x0b);
    CHECK_EQ(d.id, DdpPacket::kDisplay);
    CHECK_EQ(d.offset, 0x01020304u);
    CHECK_EQ(d.length, 3);
    CHECK_EQ(d.data[2], 30);
}

TEST(ddp_timecode) {
    const uint8_t p[] = {0x50, 0x01, 0x0b, 0x01, 0, 0, 0, 0, 0x00, 0x01, 9, 9, 9, 9, 42};
    DdpPacket d;
    CHECK(parseDdp(p, sizeof p, d));
    CHECK(!d.push);
    CHECK_EQ(d.length, 1);
    CHECK_EQ(d.data[0], 42);
}

TEST(ddp_refuses) {
    DdpPacket d;
    const uint8_t v2[] = {0x81, 0, 0, 1, 0, 0, 0, 0, 0, 0};
    CHECK(!parseDdp(v2, sizeof v2, d));
    const uint8_t shortHeader[] = {0x41, 0, 0, 1, 0, 0};
    CHECK(!parseDdp(shortHeader, sizeof shortHeader, d));
    const uint8_t overrun[] = {0x41, 0, 0, 1, 0, 0, 0, 0, 0, 4, 1, 2};
    CHECK(!parseDdp(overrun, sizeof overrun, d));
    const uint8_t empty[] = {0x41, 0, 0, 1, 0, 0, 0, 0, 0, 0};
    CHECK(parseDdp(empty, sizeof empty, d));
}
