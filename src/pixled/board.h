// Board files: what is wired where (docs/DESIGN.md section 3.2).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <string_view>
#include <vector>

#include "tesserkit/platform.h"

namespace pixled {

constexpr size_t kMaxOutputs = 8;
// A board file's longest text: /board/custom's capacity.
constexpr size_t kMaxBoardJson = 1024;

struct BoardOutput {
    enum class Kind : uint8_t { Digital, Pwm };
    int8_t gpio = -1;
    Kind kind = Kind::Digital;
};

struct Board {
    std::string name;
    std::string ethernet;  // a preset's name (ethernetNames()), or empty
    std::vector<BoardOutput> outputs;
    int8_t relayGpio = -1;
    bool relayActiveLow = false;
    int8_t buttonGpio = -1;
    bool buttonActiveLow = true;  // to ground when pressed, pulled up

    size_t digitalOutputs() const;
};

// Reads and checks a board file for the classic ESP32. False with `error`
// saying why: `out` is then left as it was.
bool parseBoard(std::string_view json, Board& out, std::string& error);

// The Ethernet presets a board file may name, TesserKIT's.
bool ethernetBoard(std::string_view name, tesserkit::EthernetBoard& out);
const std::vector<const char*>& ethernetNames();

// The compiled-in board files. The order is the tree's (/board/preset, after
// "none").
struct Preset {
    const char* id;
    const char* json;
};
const std::vector<Preset>& presets();
const Preset* findPreset(std::string_view id);

// /board's choice: none, a preset (presets()' order), or a custom file. The
// order of the enumerators is the tree's.
enum class BoardPreset : uint8_t { None, Elite2d, Elite4d, Elite6d, Elite5p, Elite1d2p, Custom };

struct BoardChoice {
    BoardPreset preset = BoardPreset::None;
    char custom[kMaxBoardJson] = {};

    bool operator==(const BoardChoice& o) const;
    bool operator!=(const BoardChoice& o) const { return !(*this == o); }
    // The board chosen. False with `error` when there is none (`out` is then
    // a board without outputs).
    bool resolve(Board& out, std::string& error) const;
    // The Ethernet wiring for Network::ethernet(choose): the chosen board's
    // preset, false for a board without one; the Gledopto's while no board
    // is chosen, so a fresh Elite controller is reached on its cable.
    bool ethernet(tesserkit::EthernetBoard& out) const;
};

// The choice stored and the one in use: a board applies at the next restart
// (docs/DESIGN.md section 3.2).
class BoardSelection {
public:
    BoardChoice choice;  // the tree's, stored

    // Once, after the settings were loaded: the board in use until the next
    // restart.
    const Board& boot();
    const Board& board() const { return board_; }
    const std::string& error() const { return error_; }
    bool restartNeeded() const { return booted_ && choice != atBoot_; }

private:
    BoardChoice atBoot_;
    Board board_;
    std::string error_;
    bool booted_ = false;
};

}  // namespace pixled
