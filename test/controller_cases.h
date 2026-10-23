// What the strips are sent (docs/DESIGN.md sections 5.3–5.8).
#pragma once

#include "rig.h"

using pixled::ColourOrder;
using pixled::InputState;
using pixled::OnLoss;
using pixled::TestPattern;

TEST(controller_begins_the_board) {
    Rig r;
    CHECK_EQ(r.controller.settings.outputs.size(), 4u);
    CHECK_EQ(r.bus.begins, 1);
    CHECK_EQ(r.bus.outputs.size(), 4u);
    CHECK_EQ(r.bus.outputs[1].gpio, 12);
    CHECK_EQ(r.bus.outputs[3].pixels, 170);
    // Each output on a universe of its own; dark at once, whatever the strips
    // held at power-up.
    CHECK_EQ(r.controller.settings.outputs[2].universe, 2);
    CHECK_EQ(r.bus.shows, 1);
    CHECK_EQ(r.bus.pixel(0, 0), "0,0,0");
    CHECK(r.controller.state() == InputState::Idle);
    CHECK_EQ(r.controller.error(), "");
}

TEST(controller_shows_a_frame) {
    Rig r;
    // Red on output 1's first pixel: GRB on the wire.
    int shows = r.bus.shows;
    r.dmx(0, 3, 0, true);  // 0, 1, 2
    r.poll(pixled::Frames::kCloseAfterUs);
    CHECK_EQ(r.bus.shows, shows + 1);
    CHECK_EQ(r.bus.pixel(0, 0), "1,0,2");
    CHECK_EQ(r.bus.pixel(1, 0), "0,0,0");
    CHECK(r.controller.state() == InputState::Receiving);
    CHECK(!r.bus.overrun);
    // Nothing new: nothing sent.
    r.poll(1000);
    CHECK_EQ(r.bus.shows, shows + 1);
}

TEST(controller_order_reverse_brightness) {
    Rig r;
    r.controller.settings.outputs[0].order = ColourOrder::Rgb;
    r.controller.settings.outputs[0].reverse = true;
    r.controller.settings.brightness = 50;
    r.poll();
    std::vector<uint8_t> data(510, 0);
    data[0] = 200;    // pixel 0's red
    data[507] = 100;  // pixel 169's red
    r.controller.frames().dmx(pixled::Protocol::ArtNet, 0, data.data(), data.size(), 1, r.now);
    r.poll(pixled::Frames::kCloseAfterUs);
    // 50 %: 128/256.
    CHECK_EQ(r.bus.pixel(0, 0), "50,0,0");
    CHECK_EQ(r.bus.pixel(0, 169), "100,0,0");
}

TEST(controller_settings_show_at_once) {
    // A new brightness redraws the last frame, input or not.
    Rig r;
    r.dmx(0, 3, 255);
    r.poll(pixled::Frames::kCloseAfterUs);
    CHECK_EQ(r.bus.pixel(0, 0), "255,255,255");
    int shows = r.bus.shows;
    r.controller.settings.brightness = 0;
    r.poll();
    CHECK_EQ(r.bus.shows, shows + 1);
    CHECK_EQ(r.bus.pixel(0, 0), "0,0,0");
}

TEST(controller_reconfigures_the_bus) {
    Rig r;
    r.controller.settings.outputs[1].pixels = 300;
    r.controller.settings.outputs[1].chip = ChipId::Sk6812Rgbw;
    r.poll();
    CHECK_EQ(r.bus.begins, 2);
    CHECK_EQ(r.bus.ends, 1);
    CHECK_EQ(r.bus.outputs[1].pixels, 300);
    CHECK(r.bus.outputs[1].chip == ChipId::Sk6812Rgbw);
    // RGBW: 128 pixels a universe; the white channel last.
    std::vector<uint8_t> data(512, 0);
    data[0] = 10, data[1] = 20, data[2] = 30, data[3] = 40;
    r.controller.frames().dmx(pixled::Protocol::ArtNet, 1, data.data(), data.size(), 1, r.now);
    r.poll(pixled::Frames::kCloseAfterUs);
    CHECK_EQ(r.bus.pixel(1, 0), "20,10,30,40");
    // A universe change alone keeps the bus.
    r.controller.settings.outputs[1].universe = 7;
    r.poll();
    CHECK_EQ(r.bus.begins, 2);
}

TEST(controller_memory_budget) {
    Rig r;
    for (auto& o : r.controller.settings.outputs) o.pixels = pixled::kMaxPixels;
    r.poll();
    // 16 KB: two outputs of 2048 RGB pixels, and 1365 on the third.
    CHECK_EQ(r.bus.outputs[2].pixels, 1365);
    CHECK_EQ(r.bus.outputs[3].pixels, 0);
    CHECK_EQ(r.controller.error(), "output 3 cut to 1365 pixels: the controller holds 16 KB of pixels");
}

TEST(controller_test_patterns) {
    Rig r;
    r.controller.settings.test = TestPattern::Colour;
    strcpy(r.controller.settings.testColour, "#ff8000");
    r.poll();
    CHECK_EQ(r.bus.pixel(3, 169), "128,255,0");  // GRB
    int shows = r.bus.shows;
    r.poll(10000);
    CHECK_EQ(r.bus.shows, shows);  // 30 frames a second
    r.poll(30000);
    CHECK_EQ(r.bus.shows, shows + 1);
    // Gradient: output 2 numbered by two dots.
    r.controller.settings.test = TestPattern::Gradient;
    r.poll();
    CHECK_EQ(r.bus.pixel(1, 0), "255,255,255");
    CHECK_EQ(r.bus.pixel(1, 1), "0,0,0");
    CHECK_EQ(r.bus.pixel(1, 2), "255,255,255");
    CHECK(r.bus.pixel(1, 4) != "0,0,0");
    // Off: the last frame again.
    r.controller.settings.test = TestPattern::Off;
    r.poll();
    CHECK_EQ(r.bus.pixel(1, 0), "0,0,0");
}

TEST(controller_test_modes) {
    Rig r;
    r.controller.settings.outputs[1].chip = ChipId::Sk6812Rgbw;
    r.controller.settings.test = TestPattern::Red;
    r.poll();
    CHECK_EQ(r.bus.pixel(0, 0), "0,255,0");  // GRB
    CHECK_EQ(r.bus.pixel(0, 169), "0,255,0");
    CHECK_EQ(r.bus.pixel(1, 0), "0,255,0,0");
    r.controller.settings.test = TestPattern::Blue;
    r.poll();
    CHECK_EQ(r.bus.pixel(3, 5), "0,0,255");
    // White: every channel of an RGB chip, the white one alone of an RGBW.
    r.controller.settings.test = TestPattern::White;
    r.poll();
    CHECK_EQ(r.bus.pixel(0, 0), "255,255,255");
    CHECK_EQ(r.bus.pixel(1, 0), "0,0,0,255");
    r.controller.settings.test = TestPattern::Black;
    r.poll();
    CHECK_EQ(r.bus.pixel(0, 0), "0,0,0");
    CHECK_EQ(r.bus.pixel(1, 0), "0,0,0,0");
    // Fade: every pixel the same colour, around the wheel in 6 s (GRB on
    // the wire).
    r.controller.settings.test = TestPattern::Fade;
    r.poll(6000000 - r.now % 6000000);
    CHECK_EQ(r.bus.pixel(0, 0), "0,255,0");  // red
    CHECK_EQ(r.bus.pixel(3, 169), "0,255,0");
    CHECK_EQ(r.bus.pixel(1, 0), "0,255,0,0");
    r.poll(2000000);
    CHECK_EQ(r.bus.pixel(0, 0), "255,0,0");  // green, at 120°
    r.poll(1000000);
    CHECK_EQ(r.bus.pixel(0, 0), "255,0,255");  // cyan, at 180°
    r.poll(1000000);
    CHECK_EQ(r.bus.pixel(0, 0), "0,0,255");  // blue, at 240°
    // Brightness applies.
    r.controller.settings.test = TestPattern::White;
    r.controller.settings.brightness = 50;
    r.poll();
    CHECK_EQ(r.bus.pixel(0, 0), "128,128,128");
}

TEST(controller_button) {
    Rig r;
    using tesserkit::Press;
    // Short presses do nothing while the test modes are off.
    r.controller.button(Press::Short);
    CHECK(r.controller.settings.test == TestPattern::Off);
    r.controller.button(Press::Long);
    CHECK(r.controller.settings.test == TestPattern::Red);
    const TestPattern cycle[] = {TestPattern::Green, TestPattern::Blue, TestPattern::White,
                                 TestPattern::Black, TestPattern::Fade,  TestPattern::Red};
    for (TestPattern t : cycle) {
        r.controller.button(Press::Short);
        CHECK(r.controller.settings.test == t);
    }
    r.poll();
    CHECK_EQ(r.bus.pixel(2, 0), "0,255,0");
    r.controller.button(Press::None);
    CHECK(r.controller.settings.test == TestPattern::Red);
    r.controller.button(Press::Long);
    CHECK(r.controller.settings.test == TestPattern::Off);
    // From a pattern set in the tree, a short press goes to red.
    r.controller.settings.test = TestPattern::Gradient;
    r.controller.button(Press::Short);
    CHECK(r.controller.settings.test == TestPattern::Red);
}

TEST(controller_loss_of_signal) {
    Rig r;
    r.controller.settings.onLoss = OnLoss::Black;
    r.controller.settings.lossTimeout = 1;
    r.dmx(0, 3, 9);
    r.poll(pixled::Frames::kCloseAfterUs);
    CHECK_EQ(r.bus.pixel(0, 0), "9,9,9");
    r.poll(900000);
    CHECK(r.controller.state() == InputState::Receiving);
    r.poll(200000);
    CHECK(r.controller.state() == InputState::Lost);
    CHECK_EQ(r.bus.pixel(0, 0), "0,0,0");
    int shows = r.bus.shows;
    r.poll(100000);
    CHECK_EQ(r.bus.shows, shows);  // black once
    // Back.
    r.dmx(0, 3, 5);
    r.poll(pixled::Frames::kCloseAfterUs);
    CHECK_EQ(r.bus.pixel(0, 0), "5,5,5");
    // Hold: the last frame stays.
    r.controller.settings.onLoss = OnLoss::Hold;
    r.poll(2000000);
    CHECK(r.controller.state() == InputState::Lost);
    CHECK_EQ(r.bus.pixel(0, 0), "5,5,5");
    // Test while lost.
    r.controller.settings.onLoss = OnLoss::Test;
    r.poll();
    CHECK_EQ(r.bus.pixel(0, 0), "255,255,255");
}

TEST(controller_frame_rate_cap) {
    Rig r;
    r.controller.settings.maxFps = 10;
    r.poll();
    for (int i = 0; i < 10; i++) {
        r.dmx(0, 3, static_cast<uint8_t>(i));
        r.poll(pixled::Frames::kCloseAfterUs);
    }
    // 250 ms at 10 frames a second: three shown (the settings' redraw, then
    // every 100 ms), the latest data each time.
    CHECK(r.bus.shows <= 4);
    CHECK(r.controller.shown() >= 2);
}

TEST(controller_more_settings_than_outputs) {
    Rig r;
    r.controller.settings.outputs.push_back(pixled::OutputSettings());
    r.poll();
    CHECK_EQ(r.bus.outputs.size(), 4u);
    CHECK_EQ(r.controller.error(), "the board has 4 digital outputs: the others aren't driven");
}

TEST(controller_without_a_board) {
    Rig r(false);
    r.controller.begin(r.board.boot());
    r.poll();
    CHECK_EQ(r.bus.outputs.size(), 0u);
    CHECK_EQ(r.board.error(), "no board chosen: /board/preset");
    r.dmx(0, 3, 1);
    r.poll(pixled::Frames::kCloseAfterUs);
    CHECK(!r.bus.overrun);
}
