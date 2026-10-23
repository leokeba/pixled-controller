// The LED chips an output can drive, and the colour orders (docs/DESIGN.md
// section 5.3).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace pixled {

// The order of the enumerators is the tree's (kChipNames).
enum class ChipId : uint8_t { Ws2812, Ws2811, Ws2815, Sk6812Rgbw };

struct Chip {
    const char* name;
    uint8_t channels;  // per pixel: 3 (RGB) or 4 (RGBW)
    // The bit: its high time for a 0 and for a 1, and the low times after.
    uint16_t t0hNs, t0lNs, t1hNs, t1lNs;
    // The low time that ends a frame and latches it.
    uint16_t resetUs;
};

constexpr size_t kChipCount = 4;
extern const char* const kChipNames[kChipCount];

const Chip& chip(ChipId id);

// The colour order a chip expects its red, green and blue in; white, on a
// four-channel chip, comes last. The order of the enumerators is the tree's.
enum class ColourOrder : uint8_t { Rgb, Rbg, Grb, Gbr, Brg, Bgr };

// For each wire position, the input channel (0 red, 1 green, 2 blue) it
// carries.
struct Order {
    uint8_t from[3];
};
Order order(ColourOrder o);

}  // namespace pixled
