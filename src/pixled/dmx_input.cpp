#include "pixled/dmx_input.h"

namespace pixled {

namespace {

Protocol protocol(tesserkit::DmxProtocol p) {
    return p == tesserkit::DmxProtocol::Sacn ? Protocol::Sacn : Protocol::ArtNet;
}

}  // namespace

bool DmxInput::enabled(tesserkit::DmxProtocol p) const {
    return p == tesserkit::DmxProtocol::Sacn ? controller_.sacnEnabled() : controller_.artnetEnabled();
}

void DmxInput::frame(const tesserkit::DmxFrame& f) {
    if (!enabled(f.protocol)) return;
    controller_.frames().dmx(protocol(f.protocol), f.universe, f.data, f.length, f.source, f.atUs);
}

void DmxInput::sync(const tesserkit::DmxSync& s) {
    if (!enabled(s.protocol)) return;
    controller_.frames().sync(protocol(s.protocol), s.source, s.atUs);
}

}  // namespace pixled
