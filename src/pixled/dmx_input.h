// Art-Net's and sACN's frames into the controller's (docs/DESIGN.md section
// 5.1): TesserKIT's DMX services hand them to this sink.
#pragma once

#include "pixled/controller.h"
#include "tesserkit/dmx.h"

namespace pixled {

// Called in the services' receiving task: one copy into the frames, while
// the protocol's /inputs switch is on.
class DmxInput : public tesserkit::DmxSink {
public:
    explicit DmxInput(Controller& controller) : controller_(controller) {}

    void frame(const tesserkit::DmxFrame& f) override;
    void sync(const tesserkit::DmxSync& s) override;

private:
    bool enabled(tesserkit::DmxProtocol p) const;

    Controller& controller_;
};

}  // namespace pixled
