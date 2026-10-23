// Board files (docs/DESIGN.md section 3.2): the presets, and what is refused.
#pragma once

#include "rig.h"

using pixled::Board;
using pixled::BoardChoice;
using pixled::BoardPreset;
using pixled::parseBoard;

namespace {

// The error parseBoard() gives `json`, "" when it takes it.
std::string refusal(const char* json) {
    Board b;
    std::string error;
    return parseBoard(json, b, error) ? "" : error;
}

}  // namespace

TEST(board_presets_parse) {
    for (const pixled::Preset& p : pixled::presets()) {
        Board b;
        std::string error;
        CHECK(parseBoard(p.json, b, error));
        CHECK_EQ(error, "");
        CHECK_EQ(b.ethernet, "gledopto");
    }
    Board b = elite4d();
    CHECK_EQ(b.name, "Gledopto Elite 4D-EXMU");
    CHECK_EQ(b.digitalOutputs(), 4u);
    CHECK_EQ(b.outputs[1].gpio, 12);
    CHECK_EQ(b.relayGpio, 18);
    CHECK(b.relayActiveLow);
    CHECK_EQ(b.buttonGpio, 17);
    CHECK(b.buttonActiveLow);
    std::string err;
    CHECK(parseBoard(R"({"outputs":[],"button":{"gpio":36,"activeLow":false}})", b, err));
    CHECK(!b.buttonActiveLow);
    Board p5;
    std::string error;
    parseBoard(pixled::findPreset("gledopto-elite-5p")->json, p5, error);
    CHECK_EQ(p5.outputs.size(), 5u);
    CHECK_EQ(p5.digitalOutputs(), 0u);
    CHECK(pixled::findPreset("nope") == nullptr);
}

TEST(board_refusals) {
    CHECK_EQ(refusal("[]"), "not a JSON object");
    CHECK_EQ(refusal(R"({"outputs":[],"colour":1})"), "unknown key \"colour\"");
    CHECK_EQ(refusal(R"({"outputs":{}})"), "outputs: a list");
    CHECK_EQ(refusal(R"({"outputs":[{"gpio":0}]})"), "output 1: GPIO 0 is a boot strapping pin and Ethernet's clock");
    CHECK_EQ(refusal(R"({"outputs":[{"gpio":6}]})"), "output 1: GPIO 6 is the flash's");
    CHECK_EQ(refusal(R"({"outputs":[{"gpio":35}]})"), "output 1: GPIO 35 is an input only");
    CHECK_EQ(refusal(R"({"outputs":[{"gpio":24}]})"), "output 1: GPIO 24 doesn't exist");
    CHECK_EQ(refusal(R"({"outputs":[{"gpio":"4"}]})"), "output 1: gpio must be a number");
    CHECK_EQ(refusal(R"({"outputs":[{"gpio":4},{"gpio":4}]})"), "output 2: GPIO 4 is used twice");
    CHECK_EQ(refusal(R"({"outputs":[{"gpio":4,"kind":"analog"}]})"), "output 1: kind is digital or pwm");
    CHECK_EQ(refusal(R"({"ethernet":"lan9999","outputs":[]})"), "ethernet: no preset \"lan9999\"");
    // Ethernet's pins, only with Ethernet.
    CHECK_EQ(refusal(R"({"ethernet":"gledopto","outputs":[{"gpio":23}]})"), "output 1: GPIO 23 is Ethernet's");
    CHECK_EQ(refusal(R"({"ethernet":"gledopto","outputs":[{"gpio":5}]})"), "output 1: GPIO 5 is Ethernet's");
    CHECK_EQ(refusal(R"({"outputs":[{"gpio":23}]})"), "");
    CHECK_EQ(refusal(R"({"outputs":[{"gpio":4}],"relay":{"gpio":4}})"), "relay: GPIO 4 is used twice");
    // A button may be an input-only pin.
    CHECK_EQ(refusal(R"({"outputs":[{"gpio":4}],"button":{"gpio":36}})"), "");
    CHECK_EQ(refusal(R"({"outputs":[{"gpio":2},{"gpio":4},{"gpio":12},{"gpio":13},{"gpio":14},{"gpio":15},
        {"gpio":16},{"gpio":17},{"gpio":18}]})"),
             "outputs: at most 8");
}

TEST(board_refused_leaves_the_board) {
    Board b = elite4d();
    std::string error;
    CHECK(!parseBoard(R"({"outputs":[{"gpio":0}]})", b, error));
    CHECK_EQ(b.digitalOutputs(), 4u);
}

TEST(board_choice) {
    BoardChoice c;
    Board b;
    std::string error;
    CHECK(!c.resolve(b, error));
    CHECK_EQ(error, "no board chosen: /board/preset");
    c.preset = BoardPreset::Custom;
    CHECK(!c.resolve(b, error));
    CHECK_EQ(error, "no custom board file: /board/custom");
    strcpy(c.custom, R"({"name":"Bench","outputs":[{"gpio":4}]})");
    CHECK(c.resolve(b, error));
    CHECK_EQ(b.name, "Bench");
    c.preset = BoardPreset::Elite6d;
    CHECK(c.resolve(b, error));
    CHECK_EQ(b.digitalOutputs(), 6u);
}

TEST(board_ethernet) {
    BoardChoice c;
    tesserkit::EthernetBoard e;
    // No board yet: the Gledopto's.
    CHECK(c.ethernet(e));
    CHECK_EQ(e.mdio, 33);
    c.preset = BoardPreset::Custom;
    strcpy(c.custom, R"({"ethernet":"wt32-eth01","outputs":[{"gpio":4}]})");
    CHECK(c.ethernet(e));
    CHECK_EQ(e.mdio, 18);
    CHECK_EQ(e.power, 16);
    strcpy(c.custom, R"({"outputs":[{"gpio":4}]})");
    CHECK(!c.ethernet(e));
    c.custom[0] = 0;  // a custom board without its file: none
    CHECK(!c.ethernet(e));
}

TEST(board_applies_at_restart) {
    pixled::BoardSelection s;
    s.choice.preset = BoardPreset::Elite2d;
    CHECK(!s.restartNeeded());
    CHECK_EQ(s.boot().digitalOutputs(), 2u);
    CHECK(!s.restartNeeded());
    s.choice.preset = BoardPreset::Elite4d;
    CHECK(s.restartNeeded());
    CHECK_EQ(s.board().digitalOutputs(), 2u);
    s.choice.preset = BoardPreset::Elite2d;
    CHECK(!s.restartNeeded());
}
