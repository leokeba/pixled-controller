// The /outputs, /inputs, /power and /board trees (docs/DESIGN.md section 6).
#pragma once

#include "TesserAPI.h"
#include "pixled/board.h"
#include "pixled/controller.h"

namespace pixled {

void mountTree(tesser::Api& api, Controller& controller, BoardSelection& board);

}  // namespace pixled
