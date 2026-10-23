# Pixled controller

Firmware for addressable LED controllers: pixels received over Art-Net, sACN (E1.31) or DDP, shown on every output at once, a frame at a time. Its main targets are GLEDOPTO's Elite controllers with Ethernet (2D, 4D, 6D, 1D2P, 5P); the board's wiring is a JSON file, so other ESP32 boards need no new firmware.

It is an application of [TesserKIT](https://github.com/leokeba/TesserKIT) (Ethernet, Wi-Fi with a captive portal, updates from this repository's releases, files, backups, users), [TesserAPI](https://github.com/leokeba/TesserAPI) and [TesserUI](https://github.com/leokeba/TesserUI) (the device's tree, reachable over HTTP, WebSocket and USB serial, drawn in the browser).

> **Status:** host-tested, not yet on a board. The specification is [docs/DESIGN.md](docs/DESIGN.md).

## Use

Flash it, choose the board under Board (`/board/preset`: a GLEDOPTO Elite preset, or `custom` with a board file in `/board/custom`), restart, then set each output's chip, colour order, pixel count and first universe under Outputs. Outputs → Test mode checks the strips (red, green, blue, white, black, a rainbow fade) and the wiring (`gradient` numbers each output by dots at its start). The board's function button does the same without a browser: a long press turns test mode on (red) or off, short presses go to the next mode.

A board file says what is wired where:

```json
{"name": "Bench", "ethernet": "gledopto",
 "outputs": [{"gpio": 16}, {"gpio": 2}], "relay": {"gpio": 18, "activeLow": true}, "button": {"gpio": 17}}
```

## Install and update

The [launcher](https://leokeba.github.io/pixled-controller/) reaches a controller by its address or over USB, and installs the firmware on a new board over USB (Chrome or Edge on a computer), then sets up its Wi-Fi. On a Mac, the bridge in each [release](https://github.com/leokeba/pixled-controller/releases/latest) (`pixled-bridge-macos-arm64.zip` for Apple silicon, `-x64.zip` for Intel) lists the controllers on the network and serves the same launcher on localhost. Without either, `pixled-esp32-factory.bin` goes at offset 0 over USB. Before flashing a Gledopto, keep its WLED: `uvx esptool --port <port> --baud 115200 read-flash 0 ALL wled.bin`.

Each push to `main` publishes a release. A controller online checks for a new one daily and shows it under System → Firmware (`/sys/firmware/github`), where `update` installs it over the air; an image that doesn't confirm itself rolls back.

## Build

The libraries are checkouts next to this repository, at the commits [`deps.lock`](deps.lock) pins (`scripts/pin.sh` updates it): TesserAPI, TesserUI, TesserKIT.

```sh
cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure   # host tests
. ~/esp/esp-idf/export.sh
idf.py -C firmware -B /tmp/pixled-idf set-target esp32 build flash   # the firmware
```

A gitignored `firmware/main/secrets.h` may define `WIFI_SSID` and `WIFI_PASSWORD` for development.
