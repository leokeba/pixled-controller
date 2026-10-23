#include "pixled/chips.h"

namespace pixled {

namespace {

// Timings from the datasheets' typical values, inside their tolerances; to
// check on a scope (docs/DESIGN.md section 10). The WS2811 is the 800 kHz
// mode strips use.
constexpr Chip kChips[kChipCount] = {
    {"ws2812", 3, 300, 900, 900, 350, 300},
    {"ws2811", 3, 300, 950, 750, 500, 300},
    {"ws2815", 3, 300, 900, 900, 350, 300},
    {"sk6812-rgbw", 4, 300, 900, 600, 600, 100},
};

}  // namespace

const char* const kChipNames[kChipCount] = {"ws2812", "ws2811", "ws2815", "sk6812-rgbw"};

const Chip& chip(ChipId id) {
    size_t i = static_cast<size_t>(id);
    return kChips[i < kChipCount ? i : 0];
}

Order order(ColourOrder o) {
    switch (o) {
        case ColourOrder::Rgb: return {{0, 1, 2}};
        case ColourOrder::Rbg: return {{0, 2, 1}};
        case ColourOrder::Grb: return {{1, 0, 2}};
        case ColourOrder::Gbr: return {{1, 2, 0}};
        case ColourOrder::Brg: return {{2, 0, 1}};
        case ColourOrder::Bgr: return {{2, 1, 0}};
    }
    return {{0, 1, 2}};
}

}  // namespace pixled
