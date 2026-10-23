// Where an output's pixels are in the input (docs/DESIGN.md section 5.2).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace pixled {

constexpr uint16_t kUniverseChannels = 512;

struct OutputMap {
    uint16_t pixels = 0;
    uint8_t channels = 3;   // per pixel
    uint16_t universe = 0;  // the first pixel's
    uint16_t channel = 1;   // the first pixel's, 1–512
    // Pixels run on across universes byte by byte, a pixel straddling two
    // where one ends. Off, a universe holds whole pixels only.
    bool split = false;

    size_t bytes() const { return static_cast<size_t>(pixels) * channels; }
    // The universe holding the last pixel's last byte (universe when there
    // are no pixels).
    uint16_t lastUniverse() const;
};

// The bytes of `universe`'s data that go into an output's own bytes:
// data[from, from + length) to bytes[to, to + length). Empty when the
// universe carries none of them.
struct Span {
    size_t to = 0;
    uint16_t from = 0;
    uint16_t length = 0;
};
Span span(const OutputMap& m, uint16_t universe);

}  // namespace pixled
