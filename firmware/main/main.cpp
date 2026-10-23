// The pixel controller (docs/DESIGN.md): TesserKIT for Ethernet, Wi-Fi,
// updates, files, accounts, the log and the controllers nearby over UDP; this
// repository's outputs, frames and inputs.
//
// Flash it, choose the board (/board/preset), and send pixels over Art-Net,
// sACN or DDP. A fresh controller has no board: its outputs do nothing until
// one is chosen and it restarts.
//
// For development, a gitignored main/secrets.h may define WIFI_SSID and
// WIFI_PASSWORD, and WIFI_TX_POWER (dBm) for boards with a weak supply.
#include "Pixled.h"
#include "TesserKIT.h"
#include "pixled/platform/esp/esp.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif

namespace {

tesserkit::Device dev("Pixled");
tesserkit::System sys(dev);
tesserkit::Network net(dev);
tesserkit::Time timekeeper(dev);
tesserkit::Firmware firmware(dev);
tesserkit::Files files(dev);    // LittleFS: the settings
tesserkit::Backup backup(dev);
tesserkit::Auth auth(dev);      // open until an admin account exists
tesserkit::UdpApi udp(dev);     // the controllers on the network, and writes to all at once
tesserkit::Peers peers(dev);
tesserkit::ArtNet artnet(dev);  // UDP 6454: the pixels' universes, and /sys/artnet's mappings
tesserkit::Sacn sacn(dev);      // UDP 5568, multicast: the same
tesserkit::Log logs(dev);
// The launcher's USB way: envelopes on the console's UART, one per line, with
// Admin access (physical access). Logs stay at warnings, so that few land in
// the middle of a reply (sdkconfig.defaults).
tesser::UartTransport serialApi(dev.api(), UART_NUM_0);

pixled::Controller controller(pixled::espBus());
pixled::BoardSelection board;
pixled::DmxInput dmxInput(controller);  // both services' frames into the controller's
// The board's function button, sampled every tick: test modes (DESIGN
// section 5.8). Its pin is the board's, known after the settings load.
tesserkit::Button button;
tesserkit::Pins* buttonPins = nullptr;

}  // namespace

extern "C" void app_main() {
#if defined(WIFI_SSID) && defined(WIFI_PASSWORD)
    net.defaultNetwork(WIFI_SSID, WIFI_PASSWORD);
#endif
#if defined(WIFI_TX_POWER)
    net.txPower(WIFI_TX_POWER);
#endif
    // The board's Ethernet, from its file: Network asks once the settings,
    // the board's choice among them, are loaded.
    net.ethernet([](tesserkit::EthernetBoard& e) { return board.choice.ethernet(e); });
    // Updates from this repository's releases (.github/workflows/release.yml),
    // and from any URL.
    firmware.github("leokeba/pixled-controller", "pixled-{target}.bin");

    // The pixels' universes, from the services' receiving tasks, as the
    // outputs read them.
    artnet.sink(&dmxInput);
    sacn.sink(&dmxInput);
    controller.onUniverses([](const std::vector<uint16_t>& universes) {
        artnet.universes(universes);
        sacn.universes(universes);
    });

    auto& api = dev.api();
    pixled::mountTree(api, controller, board);
    // Settings written through the tree, and the button's test modes, reach
    // the output task at its next poll, from TesserKIT's task under the API
    // lock.
    dev.onTick([&api](uint32_t nowMs) {
        tesser::Lock lock(api);
        if (buttonPins) {
            const pixled::Board& b = board.board();
            controller.button(button.update(buttonPins->input(b.buttonGpio) != b.buttonActiveLow, nowMs));
        }
        controller.update();
    });

    dev.cors("https://leokeba.github.io");  // the launcher, reaching it by its address
    dev.begin();  // loads the stored settings, the board's choice among them
    serialApi.begin();
    {
        tesser::Lock lock(api);
        controller.begin(board.boot());
        const pixled::Board& b = board.board();
        if (b.buttonGpio >= 0) {
            tesserkit::Pins* pins = dev.platform().pins ? dev.platform().pins : tesserkit::espPins();
            auto pull = b.buttonActiveLow ? tesserkit::Pins::Pull::Up : tesserkit::Pins::Pull::Down;
            if (pins && pins->inputBegin(b.buttonGpio, pull)) buttonPins = pins;
        }
    }
    pixled::startOutputTask(controller);
    pixled::startDdp(controller);
}
