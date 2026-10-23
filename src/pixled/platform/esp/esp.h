// The controller's ESP32 side (docs/DESIGN.md sections 4.2 and 5.3): the
// strips through Espressif's led_strip, the output task, DDP's socket.
#pragma once

#include "pixled/board.h"
#include "pixled/controller.h"
#include "pixled/pixel_bus.h"

namespace pixled {

// The strips on the RMT, a channel per output.
PixelBus& espBus();

// The output task: on core 1, polling the controller whenever a frame
// closes, and every tick for timeouts and test patterns. Once.
bool startOutputTask(Controller& controller);

// The board's relay, switching the strips' supply: on or off, in its
// polarity. False without one.
bool setRelay(const Board& board, bool on);

// DDP on UDP 4048, into the controller's frames while its /inputs/ddp is on.
// Once, after Device::begin().
bool startDdp(Controller& controller);

}  // namespace pixled
