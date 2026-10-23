// The platform interface to the strips (docs/DESIGN.md section 5.3): a
// driver library on the ESP32 (platform/esp/led_strip_bus.cpp), a fake in the
// tests.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <vector>

#include "pixled/chips.h"

namespace pixled {

struct BusOutput {
    int8_t gpio = -1;
    ChipId chip = ChipId::Ws2812;
    uint16_t pixels = 0;

    bool operator==(const BusOutput& o) const { return gpio == o.gpio && chip == o.chip && pixels == o.pixels; }
    bool operator!=(const BusOutput& o) const { return !(*this == o); }
};

// Used from one task (the output task's).
class PixelBus {
public:
    virtual ~PixelBus() = default;
    // Takes these outputs, in place of any before; their pixels dark. False
    // if one can't be taken: none is then.
    virtual bool begin(const std::vector<BusOutput>& outputs) = 0;
    virtual void end() = 0;
    // Waits until the frame being sent has gone out: the pixels may then be
    // written.
    virtual void wait() = 0;
    // `count` pixels of output `output` from pixel `first`, as the wire
    // carries them: the chip's channels each, in its order.
    virtual void write(size_t output, size_t first, const uint8_t* wire, size_t count) = 0;
    // Sends every output's pixels, all at once, without waiting.
    virtual bool show() = 0;
};

}  // namespace pixled
