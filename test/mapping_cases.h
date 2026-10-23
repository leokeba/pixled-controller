// Where an output's pixels are in the universes (docs/DESIGN.md section 5.2).
#pragma once

#include "rig.h"

using pixled::OutputMap;
using pixled::span;

namespace {

OutputMap rgb(uint16_t pixels, uint16_t universe, uint16_t channel = 1, bool split = false) {
    OutputMap m;
    m.pixels = pixels;
    m.channels = 3;
    m.universe = universe;
    m.channel = channel;
    m.split = split;
    return m;
}

}  // namespace

TEST(mapping_whole_pixels) {
    // 400 RGB pixels from universe 3: 170, 170 and 60.
    OutputMap m = rgb(400, 3);
    CHECK_EQ(m.lastUniverse(), 5);
    CHECK_EQ(span(m, 2).length, 0);
    pixled::Span s = span(m, 3);
    CHECK_EQ(s.to, 0u);
    CHECK_EQ(s.from, 0);
    CHECK_EQ(s.length, 510);
    s = span(m, 4);
    CHECK_EQ(s.to, 510u);
    CHECK_EQ(s.length, 510);
    s = span(m, 5);
    CHECK_EQ(s.to, 1020u);
    CHECK_EQ(s.length, 180);
    CHECK_EQ(span(m, 6).length, 0);
}

TEST(mapping_starting_channel) {
    // From channel 301: 70 pixels in the first universe (channels 301–510).
    OutputMap m = rgb(100, 0, 301);
    pixled::Span s = span(m, 0);
    CHECK_EQ(s.from, 300);
    CHECK_EQ(s.length, 210);
    s = span(m, 1);
    CHECK_EQ(s.to, 210u);
    CHECK_EQ(s.from, 0);
    CHECK_EQ(s.length, 90);
    CHECK_EQ(m.lastUniverse(), 1);
    // One that fits.
    CHECK_EQ(rgb(70, 0, 301).lastUniverse(), 0);
    CHECK_EQ(rgb(170, 9).lastUniverse(), 9);
    CHECK_EQ(rgb(171, 9).lastUniverse(), 10);
}

TEST(mapping_rgbw) {
    OutputMap m = rgb(300, 0);
    m.channels = 4;
    CHECK_EQ(m.lastUniverse(), 2);  // 128, 128, 44
    CHECK_EQ(span(m, 0).length, 512);
    CHECK_EQ(span(m, 2).to, 1024u);
    CHECK_EQ(span(m, 2).length, 176);
}

TEST(mapping_split) {
    // Byte by byte: 200 RGB pixels are 600 bytes, 512 then 88; pixel 170
    // straddles the two universes.
    OutputMap m = rgb(200, 1, 1, true);
    CHECK_EQ(m.lastUniverse(), 2);
    pixled::Span s = span(m, 1);
    CHECK_EQ(s.length, 512);
    s = span(m, 2);
    CHECK_EQ(s.to, 512u);
    CHECK_EQ(s.from, 0);
    CHECK_EQ(s.length, 88);
    // From channel 500: 13 bytes in the first.
    m = rgb(10, 0, 500, true);
    s = span(m, 0);
    CHECK_EQ(s.from, 499);
    CHECK_EQ(s.length, 13);
    s = span(m, 1);
    CHECK_EQ(s.to, 13u);
    CHECK_EQ(s.length, 17);
}

TEST(mapping_no_pixels) {
    OutputMap m = rgb(0, 4);
    CHECK_EQ(m.lastUniverse(), 4);
    CHECK_EQ(span(m, 4).length, 0);
}
