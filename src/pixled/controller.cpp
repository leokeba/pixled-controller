#include "pixled/controller.h"

#include <math.h>
#include <string.h>

namespace pixled {

namespace {

constexpr uint64_t kTestFrameUs = 33333;  // test patterns at 30 frames a second
constexpr uint64_t kGradientPeriodUs = 5000000;
constexpr uint64_t kFadePeriodUs = 6000000;  // once around the colour wheel

// The button's modes, in turn.
constexpr TestPattern kCycle[] = {TestPattern::Red,   TestPattern::Green, TestPattern::Blue,
                                  TestPattern::White, TestPattern::Black, TestPattern::Fade};

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Full saturation and value: `hue` 0–359.
void hueToRgb(uint32_t hue, uint8_t rgb[3]) {
    uint32_t sector = hue / 60, f = (hue % 60) * 255 / 60;
    uint8_t up = static_cast<uint8_t>(f), down = static_cast<uint8_t>(255 - f);
    switch (sector) {
        case 0: rgb[0] = 255, rgb[1] = up, rgb[2] = 0; break;
        case 1: rgb[0] = down, rgb[1] = 255, rgb[2] = 0; break;
        case 2: rgb[0] = 0, rgb[1] = 255, rgb[2] = up; break;
        case 3: rgb[0] = 0, rgb[1] = down, rgb[2] = 255; break;
        case 4: rgb[0] = up, rgb[1] = 0, rgb[2] = 255; break;
        default: rgb[0] = 255, rgb[1] = 0, rgb[2] = down; break;
    }
}

// Pixels to one output of the bus, in its order and brightness, a chunk at a
// time.
class Writer {
public:
    Writer(PixelBus& bus, size_t output, uint8_t channels, ColourOrder o, uint16_t scale)
        : bus_(bus), output_(output), channels_(channels), order_(order(o)), scale_(scale) {}
    ~Writer() { flush(); }

    // A pixel in the input's order: red, green, blue (and white).
    void push(const uint8_t* in) {
        uint8_t* w = chunk_ + count_ * channels_;
        for (int k = 0; k < 3; k++) w[k] = scaled(in[order_.from[k]]);
        if (channels_ == 4) w[3] = scaled(in[3]);
        if (++count_ == kChunk) flush();
    }

private:
    static constexpr size_t kChunk = 32;
    uint8_t scaled(uint8_t v) const { return static_cast<uint8_t>((v * scale_) >> 8); }
    void flush() {
        if (count_ == 0) return;
        bus_.write(output_, first_, chunk_, count_);
        first_ += count_;
        count_ = 0;
    }

    PixelBus& bus_;
    size_t output_;
    uint8_t channels_;
    Order order_;
    uint16_t scale_;
    uint8_t chunk_[kChunk * 4] = {};
    size_t count_ = 0, first_ = 0;
};

}  // namespace

bool parseColour(const char* text, uint8_t rgb[3]) {
    if (!text || strlen(text) != 7 || text[0] != '#') return false;
    for (int i = 0; i < 3; i++) {
        int hi = hexDigit(text[1 + 2 * i]), lo = hexDigit(text[2 + 2 * i]);
        if (hi < 0 || lo < 0) return false;
        rgb[i] = static_cast<uint8_t>(hi << 4 | lo);
    }
    return true;
}

bool Controller::Config::operator==(const Config& o) const {
    return outputs == o.outputs && minFrameUs == o.minFrameUs && test == o.test &&
           memcmp(testRgb, o.testRgb, 3) == 0 && onLoss == o.onLoss && lossUs == o.lossUs && scale == o.scale;
}

void Controller::begin(const Board& board) {
    board_ = board;
    gpios_.clear();
    for (const BoardOutput& o : board.outputs) {
        if (o.kind == BoardOutput::Kind::Digital) gpios_.push_back(o.gpio);
    }
    // A new output starts on a universe of its own.
    while (settings.outputs.size() < gpios_.size()) {
        OutputSettings o;
        o.universe = static_cast<uint16_t>(settings.outputs.size());
        settings.outputs.push_back(o);
    }
    update();
}

Controller::Config Controller::build(std::string& error) const {
    Config c;
    size_t total = 0;
    size_t n = settings.outputs.size() < gpios_.size() ? settings.outputs.size() : gpios_.size();
    for (size_t i = 0; i < n; i++) {
        const OutputSettings& s = settings.outputs[i];
        OutputConfig o;
        o.bus.gpio = gpios_[i];
        o.bus.chip = s.chip;
        uint8_t channels = chip(s.chip).channels;
        size_t pixels = s.pixels < kMaxPixels ? s.pixels : kMaxPixels;
        size_t room = (kMaxPixelBytes - total) / channels;
        if (pixels > room) {
            if (error.empty()) {
                error = "output " + std::to_string(i + 1) + " cut to " + std::to_string(room) +
                        " pixels: the controller holds " + std::to_string(kMaxPixelBytes / 1024) + " KB of pixels";
            }
            pixels = room;
        }
        total += pixels * channels;
        o.bus.pixels = static_cast<uint16_t>(pixels);
        o.map.pixels = o.bus.pixels;
        o.map.channels = channels;
        o.map.universe = s.universe;
        o.map.channel = s.channel >= 1 && s.channel <= kUniverseChannels ? s.channel : 1;
        o.map.split = s.split;
        o.order = s.order;
        o.reverse = s.reverse;
        c.outputs.push_back(o);
    }
    if (settings.outputs.size() > gpios_.size() && error.empty()) {
        error = "the board has " + std::to_string(gpios_.size()) + " digital outputs: the others aren't driven";
    }
    c.minFrameUs = settings.maxFps > 0 ? static_cast<uint32_t>(1e6f / settings.maxFps) : 0;
    c.test = settings.test;
    parseColour(settings.testColour, c.testRgb);
    c.onLoss = settings.onLoss;
    c.lossUs = static_cast<uint64_t>(settings.lossTimeout * 1e6f);
    float b = settings.brightness < 0 ? 0 : settings.brightness > 100 ? 100 : settings.brightness;
    c.scale = static_cast<uint16_t>(lroundf(b * 2.55f) + 1);
    return c;
}

void Controller::button(tesserkit::Press p) {
    TestPattern& t = settings.test;
    if (p == tesserkit::Press::Long) {
        t = t == TestPattern::Off ? kCycle[0] : TestPattern::Off;
    } else if (p == tesserkit::Press::Short && t != TestPattern::Off) {
        TestPattern next = kCycle[0];
        for (size_t i = 0; i + 1 < sizeof kCycle / sizeof kCycle[0]; i++) {
            if (kCycle[i] == t) next = kCycle[i + 1];
        }
        t = next;
    }
}

void Controller::update() {
    std::string error;
    Config c = build(error);
    artnet_.store(settings.artnet, std::memory_order_relaxed);
    sacn_.store(settings.sacn, std::memory_order_relaxed);
    ddp_.store(settings.ddp, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(configMutex_);
    if (!handed_ || !(c == pending_)) {
        pending_ = std::move(c);
        changed_ = true;
        handed_ = true;
    }
    buildError_ = error;
}

std::string Controller::error() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    return applyError_.empty() ? buildError_ : applyError_;
}

bool Controller::apply(const Config& c) {
    bool outputsChanged = !begun_ || c.outputs.size() != config_.outputs.size();
    for (size_t i = 0; !outputsChanged && i < c.outputs.size(); i++) {
        outputsChanged = c.outputs[i].bus != config_.outputs[i].bus;
    }
    bool mapChanged = outputsChanged;
    for (size_t i = 0; !mapChanged && i < c.outputs.size(); i++) {
        const OutputMap &a = c.outputs[i].map, &b = config_.outputs[i].map;
        mapChanged = a.universe != b.universe || a.channel != b.channel || a.split != b.split;
    }
    if (mapChanged) {
        std::vector<OutputMap> maps;
        for (const OutputConfig& o : c.outputs) maps.push_back(o.map);
        bool ok = frames_.configure(maps);
        std::lock_guard<std::mutex> lock(configMutex_);
        applyError_ = ok ? "" : "the outputs read more than " + std::to_string(Frames::kMaxUniverses) +
                                    " universes: their settings didn't change";
        if (!ok) return false;
        if (onUniverses_) onUniverses_(frames_.universes());
    }
    if (outputsChanged) {
        std::vector<BusOutput> outputs;
        for (const OutputConfig& o : c.outputs) outputs.push_back(o.bus);
        if (begun_) bus_.end();
        begun_ = bus_.begin(outputs);
        blackShown_ = false;
    }
    config_ = c;
    return true;
}

void Controller::poll(uint64_t nowUs) {
    bool take = false;
    Config c;
    {
        std::lock_guard<std::mutex> lock(configMutex_);
        if (changed_) {
            c = pending_;
            changed_ = false;
            take = true;
        }
    }
    // New settings show at once, on the frame last received.
    bool redraw = false;
    if (take) redraw = apply(c);
    if (nowUs - fpsSinceUs_ >= 1000000) {
        fps_.store(fpsSinceUs_ ? static_cast<float>(fpsCount_) * 1e6f / static_cast<float>(nowUs - fpsSinceUs_) : 0,
                   std::memory_order_relaxed);
        fpsSinceUs_ = nowUs;
        fpsCount_ = 0;
    }
    if (!begun_) return;

    uint64_t last = frames_.lastDataUs();
    InputState st = last == 0 ? InputState::Idle
                    : nowUs - last > config_.lossUs ? InputState::Lost
                                                    : InputState::Receiving;
    state_.store(st, std::memory_order_relaxed);
    syncing_.store(frames_.syncing(nowUs), std::memory_order_relaxed);

    bool test = config_.test != TestPattern::Off || (st == InputState::Lost && config_.onLoss == OnLoss::Test);
    if (test) {
        // Frames still come in meanwhile: started over, not shown.
        if (frames_.ready(nowUs)) frames_.take([](const uint8_t*, size_t) {});
        if (redraw || nowUs - lastShowUs_ >= kTestFrameUs) {
            bus_.wait();
            pattern(nowUs, false);
            bus_.show();
            shown(nowUs);
        }
        blackShown_ = false;
        return;
    }
    if (st == InputState::Lost && config_.onLoss == OnLoss::Black) {
        if (!blackShown_ || redraw) {
            bus_.wait();
            pattern(nowUs, true);
            bus_.show();
            shown(nowUs);
            blackShown_ = true;
        }
        return;
    }
    blackShown_ = false;
    bool due = frames_.ready(nowUs);
    if (due && config_.minFrameUs && nowUs - lastShowUs_ < config_.minFrameUs) due = false;
    if (!due && !redraw) return;
    bus_.wait();
    frames_.take([this](const uint8_t* bytes, size_t size) { render(bytes, size); });
    bus_.show();
    shown(nowUs);
}

void Controller::render(const uint8_t* bytes, size_t size) {
    size_t offset = 0;
    for (size_t i = 0; i < config_.outputs.size(); i++) {
        const OutputConfig& o = config_.outputs[i];
        size_t n = o.map.pixels, ch = o.map.channels;
        if (offset + n * ch > size) return;
        Writer w(bus_, i, o.map.channels, o.order, config_.scale);
        for (size_t d = 0; d < n; d++) {
            size_t s = o.reverse ? n - 1 - d : d;
            w.push(bytes + offset + s * ch);
        }
        offset += n * ch;
    }
}

void Controller::pattern(uint64_t nowUs, bool black) {
    // Lost with onLoss test: the gradient.
    TestPattern t = black                              ? TestPattern::Black
                    : config_.test == TestPattern::Off ? TestPattern::Gradient
                                                       : config_.test;
    for (size_t i = 0; i < config_.outputs.size(); i++) {
        const OutputConfig& o = config_.outputs[i];
        size_t n = o.map.pixels;
        Writer w(bus_, i, o.map.channels, o.order, config_.scale);
        // The modes that light every pixel alike. White is the white
        // channel alone on an RGBW chip: each mode tests one die.
        uint8_t px[4] = {0, 0, 0, 0};
        bool rgbw = o.map.channels == 4;
        switch (t) {
            case TestPattern::Red: px[0] = 255; break;
            case TestPattern::Green: px[1] = 255; break;
            case TestPattern::Blue: px[2] = 255; break;
            case TestPattern::White:
                if (rgbw) {
                    px[3] = 255;
                } else {
                    px[0] = px[1] = px[2] = 255;
                }
                break;
            case TestPattern::Fade:
                // Every pixel the same colour, around the wheel.
                hueToRgb(static_cast<uint32_t>((nowUs % kFadePeriodUs) * 360 / kFadePeriodUs), px);
                break;
            case TestPattern::Colour: memcpy(px, config_.testRgb, 3); break;
            default: break;
        }
        uint64_t hue = (nowUs % kGradientPeriodUs) * 360 / kGradientPeriodUs;
        for (size_t d = 0; d < n; d++) {
            if (t == TestPattern::Gradient && d < 2 * (i + 1)) {
                // The output's number in dots at its start: output 2 shows two.
                uint8_t v = d % 2 == 0 ? 255 : 0;
                px[0] = px[1] = px[2] = v;
            } else if (t == TestPattern::Gradient) {
                hueToRgb(static_cast<uint32_t>((d * 360 / n + hue) % 360), px);
            }
            w.push(px);
        }
    }
}

void Controller::shown(uint64_t nowUs) {
    lastShowUs_ = nowUs;
    shown_.fetch_add(1, std::memory_order_relaxed);
    fpsCount_++;
}

}  // namespace pixled
