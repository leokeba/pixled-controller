#include "pixled/mapping.h"

namespace pixled {

namespace {

// Whole pixels a universe holds from 0-based channel `start`.
size_t pixelsFrom(uint16_t start, uint8_t channels) {
    return start >= kUniverseChannels ? 0 : (kUniverseChannels - start) / channels;
}

}  // namespace

uint16_t OutputMap::lastUniverse() const {
    if (pixels == 0 || channels == 0) return universe;
    size_t start = channel > 0 ? channel - 1u : 0u;
    if (split) {
        size_t lastByte = start + bytes() - 1;
        return static_cast<uint16_t>(universe + lastByte / kUniverseChannels);
    }
    size_t first = pixelsFrom(static_cast<uint16_t>(start), channels);
    if (pixels <= first) return universe;
    size_t perUniverse = pixelsFrom(0, channels);
    size_t rest = pixels - first;
    return static_cast<uint16_t>(universe + (rest + perUniverse - 1) / perUniverse);
}

Span span(const OutputMap& m, uint16_t universe) {
    Span s;
    if (m.pixels == 0 || m.channels == 0 || universe < m.universe || universe > m.lastUniverse()) return s;
    size_t start = m.channel > 0 ? m.channel - 1u : 0u;
    size_t index = universe - m.universe;
    if (m.split) {
        // The output's bytes as one run from (universe, start).
        size_t first = index * kUniverseChannels;  // in the run, this universe's first byte
        size_t to = first > start ? first - start : 0;
        size_t from = first > start ? 0 : start - first;
        size_t length = kUniverseChannels - from;
        if (to + length > m.bytes()) length = m.bytes() - to;
        s.to = to;
        s.from = static_cast<uint16_t>(from);
        s.length = static_cast<uint16_t>(length);
        return s;
    }
    size_t firstPixels = pixelsFrom(static_cast<uint16_t>(start), m.channels);
    size_t perUniverse = pixelsFrom(0, m.channels);
    size_t pixel = index == 0 ? 0 : firstPixels + (index - 1) * perUniverse;
    size_t count = index == 0 ? firstPixels : perUniverse;
    if (pixel >= m.pixels) return s;
    if (pixel + count > m.pixels) count = m.pixels - pixel;
    s.to = pixel * m.channels;
    s.from = static_cast<uint16_t>(index == 0 ? start : 0);
    s.length = static_cast<uint16_t>(count * m.channels);
    return s;
}

}  // namespace pixled
