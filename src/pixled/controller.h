// The controller: its settings (the tree's), the frames received, and what
// the output task shows (docs/DESIGN.md sections 4.2 and 5).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "pixled/board.h"
#include "pixled/chips.h"
#include "pixled/frames.h"
#include "pixled/pixel_bus.h"
#include "tesserkit/button.h"

namespace pixled {

// The pixels' bytes for every output together, a frame's buffer: about 5,400
// RGB pixels, 32 universes (docs/DESIGN.md section 4.3).
constexpr size_t kMaxPixelBytes = 16384;
constexpr uint16_t kMaxPixels = 2048;  // per output

enum class OnLoss : uint8_t { Hold, Black, Test };
// The order of the enumerators is the tree's. Red to Fade are the button's
// modes, in the order it cycles them (docs/DESIGN.md section 5.8).
enum class TestPattern : uint8_t { Off, Red, Green, Blue, White, Black, Fade, Colour, Gradient };
enum class InputState : uint8_t { Idle, Receiving, Lost };

// An element of /outputs/list.
struct OutputSettings {
    char label[24] = {};
    ChipId chip = ChipId::Ws2812;
    ColourOrder order = ColourOrder::Grb;
    uint16_t pixels = 170;
    uint16_t universe = 0;
    uint16_t channel = 1;
    bool split = false;
    bool reverse = false;
};

// What the tree binds to: written under the API lock.
struct Settings {
    std::vector<OutputSettings> outputs;
    float maxFps = 0;  // 0: as fast as frames come
    TestPattern test = TestPattern::Off;
    char testColour[8] = "#ffffff";
    bool artnet = true;
    bool sacn = true;
    bool ddp = true;
    OnLoss onLoss = OnLoss::Hold;
    float lossTimeout = 3;  // s
    float brightness = 100;  // %
};

// "#rrggbb".
bool parseColour(const char* text, uint8_t rgb[3]);

class Controller {
public:
    explicit Controller(PixelBus& bus) : bus_(bus) {}

    Settings settings;

    // Once, after the settings were loaded: the board's digital outputs get
    // the list's elements, in order (elements added as needed).
    void begin(const Board& board);
    // Under the API lock (TesserKIT's tick): hands changed settings to the
    // output task.
    void update();
    // The board's button, under the API lock: a long press turns the test
    // modes on (red first) or off, a short one goes to the next mode.
    void button(tesserkit::Press p);
    // The output task, often (a closed frame wakes it): applies new
    // settings, then shows what is due.
    void poll(uint64_t nowUs);

    Frames& frames() { return frames_; }
    bool artnetEnabled() const { return artnet_.load(std::memory_order_relaxed); }
    bool sacnEnabled() const { return sacn_.load(std::memory_order_relaxed); }
    bool ddpEnabled() const { return ddp_.load(std::memory_order_relaxed); }
    // Told the universes the outputs read whenever they change, from the
    // output task: for the DMX services' sink (TesserKIT's universes()).
    // Before the output task starts.
    void onUniverses(std::function<void(const std::vector<uint16_t>&)> fn) { onUniverses_ = std::move(fn); }

    // For the tree.
    InputState state() const { return state_.load(std::memory_order_relaxed); }
    float fps() const { return fps_.load(std::memory_order_relaxed); }
    bool syncing() const { return syncing_.load(std::memory_order_relaxed); }
    uint32_t shown() const { return shown_.load(std::memory_order_relaxed); }
    // Why some outputs aren't driven as asked ("" when they are).
    std::string error() const;
    const Board& board() const { return board_; }

private:
    struct OutputConfig {
        BusOutput bus;
        OutputMap map;
        ColourOrder order = ColourOrder::Grb;
        bool reverse = false;
        bool operator==(const OutputConfig& o) const {
            return bus == o.bus && map.universe == o.map.universe && map.channel == o.map.channel &&
                   map.split == o.map.split && order == o.order && reverse == o.reverse;
        }
    };
    struct Config {
        std::vector<OutputConfig> outputs;
        uint32_t minFrameUs = 0;
        TestPattern test = TestPattern::Off;
        uint8_t testRgb[3] = {255, 255, 255};
        OnLoss onLoss = OnLoss::Hold;
        uint64_t lossUs = 3000000;
        uint16_t scale = 256;  // brightness: v * scale >> 8
        bool operator==(const Config& o) const;
    };

    Config build(std::string& error) const;
    bool apply(const Config& c);
    void render(const uint8_t* bytes, size_t size);
    void pattern(uint64_t nowUs, bool black);
    void shown(uint64_t nowUs);

    PixelBus& bus_;
    Frames frames_;
    Board board_;
    std::vector<int8_t> gpios_;  // the board's digital outputs

    // Handed from update() to poll().
    mutable std::mutex configMutex_;
    Config pending_;
    bool handed_ = false;
    bool changed_ = false;
    std::string buildError_;  // update()'s
    std::string applyError_;  // poll()'s

    // The output task's.
    Config config_;
    bool begun_ = false;
    bool blackShown_ = false;
    uint64_t lastShowUs_ = 0;
    uint64_t fpsSinceUs_ = 0;
    uint32_t fpsCount_ = 0;

    std::function<void(const std::vector<uint16_t>&)> onUniverses_;
    std::atomic<bool> artnet_{true};
    std::atomic<bool> sacn_{true};
    std::atomic<bool> ddp_{true};
    std::atomic<InputState> state_{InputState::Idle};
    std::atomic<float> fps_{0};
    std::atomic<bool> syncing_{false};
    std::atomic<uint32_t> shown_{0};
};

}  // namespace pixled
