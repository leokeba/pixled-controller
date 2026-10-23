// DDP, the Distributed Display Protocol (www.3waylabs.com/ddp): pixel data
// at byte offsets, on UDP 4048 (docs/DESIGN.md section 5.1).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace pixled {

struct DdpPacket {
    static constexpr uint16_t kPort = 4048;
    static constexpr uint8_t kDisplay = 1;  // the default output device's id

    bool push = false;   // the last packet of a frame: show it
    bool query = false;  // asks for a reply (not answered)
    bool reply = false;
    uint8_t sequence = 0;  // 1–15, 0 unused
    uint8_t type = 0;      // the data type: what the bytes are (not used: the outputs say)
    uint8_t id = 0;        // the destination
    uint32_t offset = 0;   // in bytes
    const uint8_t* data = nullptr;
    uint16_t length = 0;
};

// False for anything that isn't a version 1 DDP packet, or whose length runs
// past the datagram.
bool parseDdp(const uint8_t* datagram, size_t size, DdpPacket& out);

}  // namespace pixled
