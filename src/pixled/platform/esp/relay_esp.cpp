// The board's relay, which switches the strips' supply (docs/DESIGN.md
// section 5.6): a GPIO output.
#include "driver/gpio.h"
#include "pixled/platform/esp/esp.h"

namespace pixled {

bool setRelay(const Board& board, bool on) {
    if (board.relayGpio < 0) return false;
    gpio_num_t pin = static_cast<gpio_num_t>(board.relayGpio);
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    return gpio_set_level(pin, on != board.relayActiveLow ? 1 : 0) == ESP_OK;
}

}  // namespace pixled
