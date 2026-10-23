#include "pixled/ddp.h"

namespace pixled {

namespace {

constexpr uint8_t kVersionMask = 0xc0, kVersion1 = 0x40;
constexpr uint8_t kTimecode = 0x10, kReply = 0x04, kQuery = 0x02, kPush = 0x01;
constexpr size_t kHeader = 10;

}  // namespace

bool parseDdp(const uint8_t* d, size_t size, DdpPacket& out) {
    if (size < kHeader || (d[0] & kVersionMask) != kVersion1) return false;
    size_t header = (d[0] & kTimecode) ? kHeader + 4 : kHeader;
    if (size < header) return false;
    uint16_t length = static_cast<uint16_t>(d[8] << 8 | d[9]);
    if (header + length > size) return false;
    out.push = d[0] & kPush;
    out.query = d[0] & kQuery;
    out.reply = d[0] & kReply;
    out.sequence = d[1] & 0x0f;
    out.type = d[2];
    out.id = d[3];
    out.offset = static_cast<uint32_t>(d[4]) << 24 | static_cast<uint32_t>(d[5]) << 16 |
                 static_cast<uint32_t>(d[6]) << 8 | d[7];
    out.data = d + header;
    out.length = length;
    return true;
}

}  // namespace pixled
