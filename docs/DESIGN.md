# Pixled controller design

Status: phase 0 and the host side of phases 1 and 2's inputs (2026-10-23): board files and their presets, mapping, frames, Art-Net and sACN through TesserKIT's DMX sink, DDP, the controller (render, test modes, the button, loss of signal, brightness) and its tree, 45 host tests passing (clang and GCC 14, ASan and UBSan); the firmware builds for the classic ESP32 (ESP-IDF 6.1-dev, 1.59 MB, 13 % of the slot free) with the strips on Espressif's `led_strip` 3.1.1, not yet on a board. On TesserKIT 969107f (every request of §9 done). Released from GitHub (§8) since 2026-10-23. This is the specification the code follows; when the two disagree, fix one of them. Sections marked *(later)* are committed to but not built yet. Questions still open for Leo are in §12.

## 1. Goals

A Pixled controller drives addressable LED strips from the network: a lighting desk, a media server or TouchDesigner sends pixels over Art-Net, sACN or DDP, and the controller puts them on its outputs, frame after frame, without dropping or tearing. It runs on [TesserKIT](https://github.com/leokeba/TesserKIT) (Ethernet, Wi-Fi, updates, the interface, users, files), and its main targets are GLEDOPTO's Elite controllers with Ethernet (2D, 4D, 6D, 1D2P, 5P: §3). The firmware:

- receives pixel data over **Art-Net 4**, **sACN (E1.31)** and **DDP**, on Ethernet first, Wi-Fi as well;
- maps universes and channels onto its outputs: each output a chip type, a colour order, a pixel count, and where its pixels start in the input;
- drives every output at once, a whole frame shown together, synchronized across controllers when the sender asks for it (ArtSync, sACN's synchronization universe, DDP's push);
- keeps the LEDs and the power supply safe: a master brightness, a current limit, the relay switched off when everything is dark, a defined behaviour when the signal goes;
- describes its wiring (which pins are outputs, the relay, the button) in a board file, JSON, chosen or uploaded, so a new board needs no new firmware (Leo, 2026-10-23);
- is configured, monitored and updated through TesserKIT: TesserUI's interface, `/sys`, GitHub releases, the launcher.

### Non-goals (for now)

- **Effects.** No effects engine, palettes, segments or presets as WLED has: the controller is a bridge from the network to the pixels. Test modes and patterns only (§5.8). Effects *(later)*, if ever.
- **WLED compatibility:** neither its JSON API nor its realtime UDP protocols (WARLS, DRGB, DNRGB). DDP covers what WLED's senders use.
- **Merging sources** (HTP/LTP of two consoles on one universe): one source per universe at a time (§5.1).
- **Audio** (the Elite boards' PDM microphone), IR, and the Elite's spare GPIO as anything but a button input.
- **DMX out** (RS-485) and RDM.
- Chips other than the classic ESP32 for now: the Elite line is ESP32-D0WD with 4 MB and no PSRAM. The design keeps the ESP32-S3 (with a W5500, TesserKIT's `boards::w5500()`) possible, not built.

## 2. Glossary

| Term | Meaning |
|---|---|
| Universe | 512 DMX channels. Art-Net addresses one with a 15-bit port-address (net, sub-net, universe; 0–32767), sACN with 1–63999. |
| Channel | One byte of a universe, numbered 1–512. |
| Chip | The LED driver IC on the strip (WS2812B, SK6812, WS2811, WS2815, TM1814, WS2805...): its bit timing and its channels per pixel. |
| Pixel | What one chip lights: 3 channels (RGB), 4 (RGBW), 5 (RGB + two whites, WS2805). |
| Colour order | The order the chip expects its channels in (GRB for a WS2812B), against the RGB(W) order the input carries. |
| Output | One data pin and the strip behind it. |
| Frame | One image across every output: what the controller shows at once. |
| Sync | A sender's signal that the universes of a frame have all been sent, so every controller shows them now: ArtSync, sACN's synchronization packets, DDP's push flag. |
| Board file | The JSON that says which GPIOs are outputs, the relay and the button (§3.2). |

## 3. Hardware

### 3.1 The Elite controllers

GLEDOPTO's "Elite …-EXMU" controllers share one design: a classic ESP32 (ESP32-D0WD-V3, 4 MB flash, no PSRAM), a LAN8720 Ethernet PHY wired as WLED's "Gledopto Series With Ethernet" (TesserKIT's `boards::gledopto()`: PHY at 1, MDC 23, MDIO 33, clock in on GPIO 0, oscillator power GPIO 5, RMII on 19, 21, 22, 25, 26, 27), a USB-C port on a CH340 (5 V, flashing and the serial API), a relay cutting the outputs' supply, a function button, a PDM microphone, a spare terminal (IO13 on most), a fuse, 15 A in total and 10 A per output.

From Gledopto's manuals (2026-10-23; to check on each board before trusting):

| Board | Model | Outputs (terminal order) | Supply | Other |
|---|---|---|---|---|
| Elite 2D-EXMU | GL-C-616WL | digital: GPIO 16, 2 | 5–24 V | relay 18 (active low), spare IO13 |
| Elite 4D-EXMU | GL-C-618WL | digital: GPIO 16, 12, 4, 2 | 5–24 V | relay 18 (active low), button 17, spare IO13 |
| Elite 6D-EXMU | GL-C-619WL | digital: GPIO 16, 14, 13, 12, 4, 2 | 5–48 V | relay 18 (active low), button 17; Gledopto recommends ≤ 1600 chips |
| Elite 1D2P-EXMU | GL-C-620WL | 1 digital + 2 PWM (pins not documented: read from the board) | 5–24 V | relay, button |
| Elite 5P-EXMU | GL-C-611WL | PWM: GPIO 18, 16, 12, 4, 2 (ballet-photophonique's DESIGN §3) | 5–48 V | button 17, spare IO13; no relay on 18, which is an output |

- **GPIO 12 and 2 are strapping pins** (GPIO 12 high at reset selects 1.8 V flash and the chip doesn't boot): the outputs are attached after boot, and idle low.
- **GPIO 0** is the Ethernet clock input: never an output, never the button, whatever a board file says.
- The digital outputs are level-shifted to 5 V on the board (to check with a scope).

### 3.2 Board files

The firmware knows no board; a board file says what is wired where (Leo, 2026-10-23). It is JSON:

```json
{
  "name": "Gledopto Elite 4D-EXMU",
  "ethernet": "gledopto",
  "outputs": [
    {"gpio": 16, "kind": "digital"}, {"gpio": 12, "kind": "digital"},
    {"gpio": 4,  "kind": "digital"}, {"gpio": 2,  "kind": "digital"}
  ],
  "relay": {"gpio": 18, "activeLow": true},
  "button": {"gpio": 17}
}
```

- `ethernet`: one of TesserKIT's presets (`gledopto`, `wt32-eth01`, `olimex-esp32-poe`, `esp32-ethernet-kit`) or none; a W5500's pins *(later)*. Its pins are reserved, and it is applied at boot: TesserKIT's `net.ethernet(choose)` asks for the board once the settings are loaded (`BoardChoice::ethernet()`). While no board is chosen, the Gledopto's wiring, so a fresh Elite controller is reached on its cable; a board file without `ethernet` has none.
- `outputs`: up to 8, `digital` (a strip's data line) or `pwm` (a low-side channel, §5.7). Their order is the outputs' numbering in the tree.
- `relay`, `button`: optional.
- **Presets** for the boards of §3.1 are compiled in (`board.cpp`, the JSON as text), but the 1D2P's (§12); `/board/preset` chooses one. A **custom** board file is `/board/custom` (stored, ≤ 1023 characters), used when the preset is `custom`. A fresh controller has no board: its outputs do nothing until one is chosen and it restarts; the launcher could choose at install time *(later)*.
- **Validation** before anything is applied, on the classic ESP32's pins: known keys, GPIOs that exist and can drive an output (34–39 are inputs, fit for a button only), not GPIO 0 (a strapping pin and the Ethernet clock), not the flash's (6–11), not the Ethernet preset's (RMII, MDC, MDIO, power, clock), none twice, at most 8 outputs. A refused `/board/custom` changes nothing and says why (`output 1: GPIO 23 is Ethernet's`).
- **A new board applies at the next restart**, which `/board/restartNeeded` says; `/board/active` is the one in use, `/board/error` why there is none. The outputs' settings (§6) are kept, matched by index.

## 4. Architecture

Only what is specific to a pixel controller is here; the rest comes from the libraries, pinned in `deps.lock` like Fluante's and ballet-photophonique's:

- **TesserKIT**: `Device`, `System`, `Network` (Ethernet, Wi-Fi, the access point, mDNS), `Time`, `Firmware` (uploads, GitHub releases, rollback), `Files` (LittleFS: the settings), `Backup`, `Auth`, `Log`, `UdpApi` and `Peers` (other controllers on the network under `/peers`); TesserAPI's serial transport on the console's UART. TesserKIT's `classic` profile set (no Bluetooth: the heap is the pixels'). Its `ArtNet` and `Sacn` services hand the frames over (`DmxSink`, TesserKIT 1273fd5, §9). `PwmOutput` for PWM outputs *(phase 3)*.
- **TesserAPI / TesserUI**: the tree and the interface; `Views` for a first page *(later)*.
- **Espressif's `led_strip`** (the component registry, ^3.1.0): the wire (§5.3).

### 4.1 Pieces of this repository

| Piece | Does | Uses |
|---|---|---|
| `board.cpp`: `parseBoard()`, `BoardSelection` | parses and validates a board file; the presets; the board chosen and the one in use | ArduinoJson, TesserKIT's Ethernet presets |
| `mapping.cpp`: `OutputMap`, `span()` | which bytes of a universe go to which of an output's | nothing |
| `Frames` | the frame being received: one buffer, every output's bytes end to end; one source per universe; closing by completeness, timeout or sync; DDP's offsets | nothing (receivers call it: `DmxInput`; DDP's socket) |
| `DmxInput` | TesserKIT's `DmxSink`: Art-Net's and sACN's frames and syncs into `Frames`, while `/inputs/artnet` or `sacn` is on; the outputs' universes told to both services (`Controller::onUniverses()`) | TesserKIT's DMX services |
| `ddp.cpp`: `parseDdp()` | DDP's header | nothing |
| `Controller` | the settings handed from the tree to the output task; the output task's poll: the frame, or a test pattern, or black on loss, rendered (colour order, reverse, brightness) onto the bus | `Frames`, `PixelBus` |
| `PixelBus` | the platform interface: begin a set of outputs with their chips, write pixels as wire bytes, show them all at once, wait until sent | `led_strip` in `platform/esp/` (§5.3); a fake in the tests keeping the bytes each output was sent |
| `chips.cpp` | each chip's bit timing, channels per pixel, reset; colour orders | nothing |
| `Power` *(phase 2)* | the relay and the current estimate | the board's relay GPIO |
| `tree.cpp` | `/outputs`, `/inputs`, `/power`, `/board` | TesserAPI |
| `platform/esp/` | `espBus()` (`led_strip`), the output task, DDP's socket and task | ESP-IDF, lwIP, FreeRTOS |

Platform code only in `src/pixled/platform/esp/`; everything else is host-tested (§10).

### 4.2 Tasks and the hot path

Pixels don't go through the tree: at 40 frames a second and 24 universes, that is 1,000 datagrams a second, and the API lock is held by HTTP replies for milliseconds.

- **Receive:** the datagrams are copied into `Frames`' buffer in the receiving task (TesserKIT's, §9, priority 12; DDP's own here, the same), under `Frames`' mutex held for one copy (≤ 512 bytes), never the API lock.
- **Show:** a frame closes on a sync, or when every universe the outputs read has arrived, or 25 ms after its first data (§5.4); closing wakes the output task (core 1, priority 10, as TesserKIT's pulse trains learned: on core 0 Wi-Fi held their interrupt up to 600 µs), which also wakes every tick for timeouts and test patterns. It waits for the previous frame to be sent, then renders the buffer under the mutex straight into `led_strip`'s pixels (colour order, reverse, brightness: about 1 ms for 2,700 pixels, to measure), and starts every output.
- **The tree** binds to `Controller::settings`; TesserKIT's tick (every 20 ms, under the API lock) builds a snapshot and, when it differs, hands it to the output task, which applies it at its next poll and redraws the last frame with it. Status values are atomics.

### 4.3 Memory

The classic ESP32 without PSRAM: TesserKIT's `classic-gledopto` build had 88–92 KB free at rest as a gateway (TesserKIT DESIGN §11). The pixels' bytes are held twice: `Frames`' buffer and `led_strip`'s own (it has no double buffer: the output task waits for a frame to be sent before writing the next). Both are at most `kMaxPixelBytes`, 16 KB (about 5,400 RGB pixels, 32 universes): 32 KB in all; outputs past it are cut, and `/outputs/error` says so. At most 2,048 pixels per output and 64 universes. The RMT's memory (8 blocks of 64 symbols on the classic ESP32) is shared between the outputs, more blocks each with fewer outputs (fewer refill interrupts). **Receiving bursts** (measured by tesserkit-6c on a Gledopto, 2026-10-23, TesserKIT's `test/e2e_dmx.py`: 24 universes at 44 frames a second, each frame's universes back to back then a sync, Ethernet at 100 Mbit from a Mac, the interface open): ESP-IDF's defaults lose about two thirds, the EMAC's ten 512-byte DMA buffers (5 frames) and the socket's mailbox (6 datagrams) overflowing at each burst. This firmware's sdkconfig takes its settings: 28 DMA buffers of 768 bytes (21.5 KB of static RAM rather than 5 KB), a mailbox of 32, the CPU at 240 MHz (160 MHz lost a few frames every 20 s). With them six 30 s runs of eight lost nothing (31,680 frames and 1,320 syncs per protocol), the other two 12 and 7 sACN frames (≤ 0.04 %; the sender, Python through a USB adapter, may be the cause: to measure with a console). Heap with every classic service: 40–49 KB free at rest, 14.5 KB at the lowest under that load, before this firmware's pixels (32 KB at most, above): to measure on a board (§10). **With the Wi-Fi station joined beside Ethernet** the heap ran out and that device aborted in one sACN multicast run: keep `/sys/network/wifi/mode` at `fallback` with a DHCP lease on the cable, or `never` (a show network on static addresses with a saved Wi-Fi network would join both). Over Wi-Fi a third of such bursts were lost on the air, about 1 % when the datagrams are spread out: Ethernet for shows.

## 5. Behaviour

### 5.1 Inputs

- **Art-Net 4** (UDP 6454, `/inputs/artnet`): ArtDmx into the universes the outputs read, ArtSync, ArtPoll answered with ArtPollReplies listing those universes as output ports, four a reply, bind indexes 1, 2, 3… Through TesserKIT's `ArtNet`, which owns the port: its tree mappings (`/sys/artnet`) keep working beside the pixels, and its status counts packets and sequence errors.
- **sACN / E1.31** (UDP 5568, `/inputs/sacn`): TesserKIT's `Sacn` joins 239.255.hi.lo for the outputs' universes and the sync addresses it sees, on every interface (again every 5 s and on a new address), unicast too; the highest priority wins a universe, a source is dropped after 2.5 s or on Stream_Terminated, preview packets are ignored. E1.31 sync packets reach `Frames` as ArtSync does.
- Both services hand frames to `DmxInput` in their receiving task (priority 12, either core), the universes being those `Controller::onUniverses()` tells them whenever the outputs' change (up to 64; sACN's start at 1, so universe 0 is Art-Net's only).
- **DDP** (UDP 4048, `/inputs/ddp`): data at a byte offset into one buffer, every output's pixels end to end in output order (a per-output offset *(later)*); the push flag closes the frame; the destination the default display (id 1); queries not answered *(later)*. One source at a time, as below.
- **One source per universe:** the first source to send a universe keeps it until silent for 3 s; another one's packets are refused and counted (`/inputs/status/refused`), not merged. sACN's sources are chosen by priority in TesserKIT before they get here, so any sACN source is taken; Art-Net doesn't take a universe sACN holds, nor the reverse.
- **Interfaces:** every interface *(an `/inputs/interfaces` setting later)*.

### 5.2 Mapping

- Each output has a `universe` and a `channel` (1–512) where its first pixel starts, and its pixel count. Pixels follow channel after channel, into the next universe when one is full.
- **Pixels don't straddle universes** by default: a universe holds ⌊510 / channels per pixel⌋ pixels (170 RGB, 128 RGBW), as Art-Net senders expect; `/outputs/*/split` lets one straddle.
- **Grouping** *(later)*: one input pixel lights n LEDs.
- A universe may feed several outputs; outputs may overlap.
- `/outputs/*/reverse` reverses an output's pixels.

### 5.3 Outputs

- **Chips** (phase 1: WS2812B and its clones, SK6812 RGBW; then WS2811, WS2813/WS2815, TM1814, WS2805, UCS2903, SM16703; §11): a timing (the bit's high times for 0 and 1, the period, the reset), channels per pixel, a colour order setting per output.
- **Every output at once:** a frame starts on every output together and ends after the longest (800 kHz: 30 µs per RGB pixel, 1,000 pixels per output at 33 frames a second; 680 pixels, 4 universes, at 49).
- **The wire is a library's** (Leo, 2026-10-23): chip timings, encoding and the peripheral (RMT, I2S, SPI) come from a maintained, widely used driver, not from code of our own. It sits behind `PixelBus` in `platform/esp/`, so the frames, mapping and sync stay host-tested here and the library can be replaced. Which one is chosen in phase 1 by a probe on a board (§12). What a candidate must do: build in a pure ESP-IDF project (TesserKIT's firmware has no Arduino core) on ESP-IDF 5.5 and 6.x; drive 4–8 outputs from our buffers without copying them per frame; start a frame without blocking the output task, and say when it's done; survive Ethernet's and Wi-Fi's interrupts on the classic ESP32, whose RMT has no DMA (its memory refilled from an interrupt every few pixels: the known source of glitches), where I2S in parallel mode with DMA doesn't have the problem; cover the chips of §5.3.
- **No tearing:** a frame being sent is never written; a frame that closes while the previous is still going out waits, and a newer one closing meanwhile replaces it (the buffer holds the latest bytes; counted: `/outputs/status/dropped`).
- **On `led_strip`** (2026-10-23, the first candidate of §12): an RMT channel per output, the chip's timings as custom ones at 50 ns resolution, the strip's component format the wire's order (colour order is applied before); `led_strip_refresh_async()` on every output in turn, then `led_strip_refresh_async_done()` before the next frame. `CONFIG_RMT_ISR_IRAM_SAFE` keeps the refill interrupt running during flash writes. Outputs start microseconds apart, one call each, not together: to measure.
- **Chips built:** `ws2812` (and the WS2813 and clones), `ws2811` (800 kHz), `ws2815`, `sk6812-rgbw`, their timings from datasheets, to check on a scope. Colour orders: the six of R, G and B, white last.
- **A frame rate cap** (`/outputs/maxFps`, default 0: as fast as frames come) for senders faster than the strips.

### 5.4 Frames and sync

- **With sync** (an ArtSync, an sACN sync packet or a DDP push seen in the last 5 s): a frame closes on the next sync, whatever arrived. Several controllers on one sender then show together.
- **Without:** a frame closes when every universe the outputs read has arrived since the last frame, or 25 ms after the first one arrived (a lost datagram doesn't stall the strips).
- **Measured** (§10): from the sync's arrival to the first bit on the wire.

### 5.5 Loss of signal

- `/inputs/onLoss` (stored): `hold` the last frame (default), `black` after `lossTimeout` (default 3 s), or the `test` pattern. `/inputs/status/state`: `idle` (nothing since boot), `receiving`, `lost`.
- At boot, and whenever the outputs change, the strips are sent black (whatever they held at power-up), then frames as they come.

### 5.6 Brightness and power

- `/power/brightness` (0–100 %, stored, default 100) scales every frame (`v · (b · 2.55 + 1) / 256`: 100 % leaves it as it is), with dithering *(later)*. A change redraws the last frame at once.
- **Current limit** *(phase 2)*: `/power/maxCurrent` (A, default 0: none) against an estimate per chip (`mAPerChannel` at full, `mAIdle`), the frame scaled down when over it; `/power/current` shows the estimate. Gledopto's 15 A total and the user's supply are the user's to set.
- **Relay** *(phase 2)*: on while any output has a lit pixel, off after `/power/relayOffDelay` (default 5 s) of black; always on while `/power/relay` is `on`, never with `off` (`auto` default). The first frame after the relay switches on waits the chips' power-up (100 ms).

### 5.7 PWM outputs

The 1D2P's and 5P's PWM terminals, as DMX dimmers: each a channel (8 or 16 bits) of a universe, its frequency and resolution, through TesserKIT's `PwmOutput`. A colour of several PWM outputs (RGB, CCT) is several channels. Same sources, mapping, loss behaviour and brightness as the pixels. *(Phase 3.)*

### 5.8 Test modes and the button

- `/outputs/test` (not stored), at 30 frames a second, overriding the input while on; off, the last frame shows again. Brightness still applies.
  - **Test modes** (Leo, 2026-10-23): `red`, `green`, `blue`, `white`, `black` on every pixel, and `fade`, every pixel the same colour going round the rainbow once in 6 s. `white` lights every channel of an RGB chip and the white channel alone of an RGBW one, so each mode tests one die.
  - **Patterns**, set from the tree only: `colour` (`/outputs/testColour`, `#rrggbb`, on every pixel) and `gradient` (output n shows n white dots at its start, then a rainbow moving along the strip once in 5 s), to check wiring and counts. `gradient` is also what `onLoss` `test` shows.
- **The function button** (if the board file has one; `activeLow`, default true: pulled up, pressed to ground), TesserKIT's `Button` (bb6a4ed, from this repository's): sampled every 20 ms in TesserKIT's tick through its `Pins`, debounced over 30 ms. Its pin is known once the settings load, after `Button::attach()`'s time, so the firmware calls `update()` from its own tick. A **long press** (held 1 s, told while still held) turns the test modes on, at `red`, or off, from any mode; a **short press** while on goes to the next mode: red, green, blue, white, black, fade, and red again (from `colour` or `gradient`, to red); while off it does nothing. `/outputs/test` follows the button (watched), and the interface's writes the same setting.
- *(Later)* a 10 s hold for TesserKIT's factory reset, as Gledopto's manuals describe WLED's.

## 6. The tree

Persisted nodes are marked *(p)*. Writes to `/outputs`, `/inputs` and `/power` need `User`, `/board` `Admin`; TesserKIT's Auth leaves everyone Admin until an admin account exists.

```
/outputs                label "Outputs", ui.icon "bulb"
  list (p)              ≤ 8, one per digital output of the board, in order: label (≤ 23), chip (ws2812 | ws2811 |
                        ws2815 | sk6812-rgbw), order (rgb … bgr, default grb), pixels (0–2048, default 170),
                        universe (0–63999, default the output's index), channel (1–512), split (advanced), reverse
  gpios                 read-only, advanced: the board's digital outputs ("16, 12, 4, 2")
  maxFps (p)            0–120, advanced
  test                  off | red | green | blue | white | black | fade | colour | gradient, watched (the button)
  testColour            "#rrggbb", while test is colour
  error                 read-only, a warning while not empty: outputs cut to the memory budget, more than the board has
  status/               inline, watched: fps, frames (advanced), dropped
/inputs                 label "Inputs", ui.icon "network"
  artnet (p), sacn (p), ddp (p)   booleans, true: each protocol's pixels (TesserKIT's services, /sys/artnet and
                        /sys/sacn, keep their own switches and mappings)
  onLoss (p)            hold | black | test
  lossTimeout (p)       0.1–600 s, default 3
  status/               watched: state (idle | receiving | lost), syncing, protocol (none | artnet | sacn | ddp),
                        source (address), universes (the outputs' that came in the last frame); packets, refused,
                        syncs (advanced)
/power                  label "Power", ui.icon "bolt"
  brightness (p)        0–100 %
/board                  label "Board", Admin, group "Hardware"
  preset (p)            none | gledopto-elite-2d | -4d | -6d | -5p | custom
  custom (p)            a board file (≤ 1023 characters), validated, while preset is custom
  active                the board in use since boot: its name, or none
  error                 why there is no board
  restartNeeded         hidden; restartWarning, a warning callout while true
/peers                  the other controllers (UdpApi): /peers/*/power/brightness writes to all
/sys                    TesserKIT
```

*(Later, with their phases)*: `/inputs/interfaces`, per-universe sources and sequence errors; `/power/maxCurrent`, `current`, `relay`, `relayOffDelay`; a DDP offset per output.

## 7. Interface

TesserUI's interface, served by the device: its tree view of the nodes above. A first page from `Views` *(later)*: the inputs' state (fps, sync, source), each output (chip, pixels, universe), brightness, the test pattern. No hand-written page.

## 8. Persistence, first flash and updates

- Settings are stored by TesserKIT (LittleFS, `Files`), restored by `Device::begin()`, before the outputs begin.
- The partition table is TesserKIT's `tesserkit_4mb_files.csv`: two 1.75 MB OTA slots, LittleFS (the settings), a core dump. A new image rolls back unless it confirms itself.
- **Releases** as Fluante's (`.github/workflows/release.yml`, 2026-10-23): each push to `main` runs the host tests, then TesserKIT's `firmware.yml` builds `pixled-esp32.bin` and `pixled-esp32-factory.bin` against `deps.lock`; the controllers check daily, install from `/sys/firmware/github`. One image for every board, the board being a file (§3.2). The launcher on GitHub Pages, with TesserUI's flasher and bridge.
- **First flash** over USB at offset 0; Gledopto's WLED is read and kept first (`~/Dev/board-backups/`), to give a board back as it came.

## 9. Requests to other layers

A gap goes to the lowest layer, asked of its agent with Leo's go-ahead; no workarounds here. Sent with Leo's go-ahead on 2026-10-23.

| Request | Layer | For | Status |
|---|---|---|---|
| **DMX for pixels from `ArtNet`:** a sink for raw ArtDmx frames (universe, sequence, data, source) called in the receiving context, for any number of universes beside the tree mappings; ArtSync to the sink; ArtPollReply listing the sink's universes as output ports (bind indexes past four); the socket drained as datagrams arrive (its own task, or woken), not every 20 ms tick; a receive mailbox sized for bursts. | TesserKIT | §5.1, §4.2 | done, TesserKIT 1273fd5 (pinned at 969107f): `DmxService::sink()`, `universes()`, `DmxSink`, `DmxTask`; measured (§4.3), the sdkconfig's buffers ours to set |
| **sACN (E1.31)** as a TesserKIT service beside ArtNet, the same tree mappings and the same raw sink: multicast joins for the universes in use, priority, synchronization, termination, discovery *(later)*. Generic: ballet-photophonique and tiny-heliostat would take sACN too. | TesserKIT | §5.1 | done, 1273fd5: `tesserkit::Sacn` |
| Ethernet chosen at run time from a name (`boards::byName("gledopto")`), for board files | TesserKIT | §3.2 | done, 1273fd5: `ethernetBoard()` calls it |
| **Ethernet's wiring after the settings load:** `net.ethernet()` is called before `dev.begin()`, which is what loads the board's choice; a hook between the load and Network's start (or `ethernet()` taking a function called then) would let the board file decide | TesserKIT | §3.2 | done, TesserKIT 969107f: `net.ethernet(choose)` |
| **A function button** as a TesserKIT class (as `PwmOutput` is): a GPIO, active low with the pull-up or high with the pull-down, sampled every tick, debounced, short presses on release and long ones once while held; perhaps a long hold for the factory reset | TesserKIT | §5.8 | done, TesserKIT bb6a4ed: `tesserkit::Button` (Short, Long, an optional VeryLong), ours removed |
| A validator's refusal with a message it composes (`Check` keeps a `const char*`): `/board/custom` keeps its message in a static string meanwhile | TesserAPI | §3.2 | to consider; not asked |
| A board file's GPIOs checked against what the platform reserves (flash, PSRAM, Ethernet, strapping) | TesserKIT | §3.2 | not asked: here first (mentioned to tesserkit-6c, 2026-10-23) |

## 10. Testing

1. **Host** (`test/`, `cmake -S . -B build && cmake --build build && ctest --test-dir build`, ASan and UBSan; 45 cases as of 2026-10-23, also under GCC 14 in Docker): board files (the presets, every refusal, a refused file leaving the board, the choice, applying at restart); mapping (whole pixels, a starting channel, RGBW, split, none); frames (the layout, closing when complete, after 25 ms, on sync and back without, dropped, one source per universe and its timeout, sACN's sources, DDP's offsets, push and one source, the wake on close, the universes' limit); DDP's header (push, timecode, refusals); the controller on a fake bus (the board's outputs begun, black at boot, a frame in GRB, colour order, reverse and brightness, settings redrawing at once, the bus reconfigured only for chips and counts, RGBW, the memory budget, test patterns and back, loss of signal in each mode, the frame rate cap, more settings than outputs, no board); the tree (declared cleanly, writes applied, refusals, the board's choice, the test modes' names); the test modes (each colour, RGBW's white, fade's colours over time, brightness) and the button's cycle; `DmxInput` (Art-Net's and sACN's frames, the switches, syncs) and the universes told to the services. *(Later)*: the current limit, the relay, persistence across a restart.
2. **Firmware build** with ESP-IDF (local 6.1-dev; CI releases with 5.5), against clean clones of `deps.lock`'s commits.
3. **Throughput on a board** (`test/e2e_pixels.py`, uv): a sender on the Mac (Art-Net, sACN, DDP) at chosen universes and rates, with and without sync, over Ethernet and Wi-Fi, while the interface is polled; the controller's counters (received, sequence errors, frames, dropped) against what was sent; the lowest heap.
4. **The waveform** (a second board capturing the outputs, as TesserKIT's `pulses_probe` captures with the MCPWM, or a logic analyzer): bit timings per chip, frames whole, outputs starting together, no glitch under Ethernet and Wi-Fi load; latency from the sync to the first edge; two controllers' frames against each other.
5. **Strips** on the bench: each chip type, colour orders, the relay, the test pattern.

## 11. Roadmap

| Phase | Content | Acceptance |
|---|---|---|
| 0 *(built, not on a board)* | The repository: host build and tests, the firmware on TesserKIT's classic services, `deps.lock`, `scripts/pin.sh` | Host tests pass; the firmware boots on an Elite board over Ethernet, its `/sys` reachable |
| 1 *(host-tested)* | Art-Net (TesserKIT's sink, §9) and DDP, mapping, frames, WS2812B and SK6812 on every digital output, loss behaviour, test pattern, brightness; `led_strip` measured (§5.3) | 4 outputs × 680 RGB pixels (16 universes) at 40 frames a second over Ethernet with the interface open: no frame dropped by the controller, no glitch on a capture, for 10 minutes |
| 2 *(sACN and syncs host-tested)* | sACN, sync from all three, the other chips, current limit, relay | Two controllers on one sender with sync: frames within 1 ms of each other |
| 3 | PWM outputs (1D2P, 5P), the button's factory reset | The 5P as a 5-channel 16-bit dimmer from Art-Net |
| 4 *(set up)* | Releases and the launcher on GitHub Pages | A release installed from `/sys/firmware/github` |
| *(later)* | Grouping, dithering, the ESP32-S3 with a W5500, ArtAddress and ArtIpProg, sACN discovery, effects | |

## 12. Open questions

- **The driver library** (§5.3). Candidates as of 2026-10-23: Espressif's `led_strip` (ESP-IDF's component registry, 3.x; RMT or SPI backends, one strip per instance, DMA only where the chip's RMT has it: not the classic ESP32); FastLED (very active; RMT through `led_strip` on ESP-IDF 5, and an I2S parallel driver for many strips of one timing on the classic ESP32, but Arduino-first, and much larger than a strip driver now); NeoPixelBus (what WLED ships on these very boards, I2S parallel included; needs the Arduino core); hpwit's I2SClocklessLedDriver (I2S parallel, small, few users). Proposed, and begun (2026-10-23: the firmware is on `led_strip`): measured on a 4D-class board under the phase 1 load; if it glitches, FastLED's or NeoPixelBus's I2S parallel driver, which then decides whether this firmware takes Arduino as a component (flash, and ESP-IDF versions tied to Arduino-ESP32's).
- **The 1D2P's pins** aren't in its manual: read them from a board (or its WLED configuration) before writing its preset.
- **Boards on hand:** the Gledopto boards in TesserKIT's notes are 5P (GL-C-611WL, PWM only) and one of unknown model; a digital Elite (2D, 4D, 6D) is needed for phase 1.
- The device's label "Pixled" (hostname `pixled-xxxx`) is assumed. The repository is public at `leokeba/pixled-controller`, MIT as the siblings (Leo, 2026-10-23).
- **A board at install time:** the launcher could ask which board and write `/board/preset` after flashing, or Gledopto's model could be guessed from WLED's stored configuration before it's erased.

## 13. Decisions

- **On TesserKIT, as an application** (Leo, 2026-10-23): this repository holds only what is specific to driving pixels; Ethernet, Wi-Fi, updates, the interface and users come from TesserKIT, and gaps go there (§9).
- **The board in a JSON file, not in the firmware** (Leo, 2026-10-23): output pins, the relay and the button come from a board file, presets compiled in for the Elite boards, a custom one uploaded; one firmware image for every board.
- **sACN in TesserKIT, beside Art-Net** (Leo, 2026-10-23): every lighting-controlled device gains it, with the same tree mappings; DDP stays here.
- **A library drives the wire** (Leo, 2026-10-23): no LED driver of our own (§5.3).
- **Test modes on the button** (Leo, 2026-10-23): red, green, blue, white, black and a rainbow fade, a long press to turn them on or off, short presses to go through them (§5.8).
- **`led_strip` first** (2026-10-23): ESP-IDF-native, Espressif's, its asynchronous refresh and custom timings since 3.1; replaced behind `PixelBus` if a board shows glitches (§12).
- **Pixels outside the tree** (proposed, 2026-10-23): frames go from the receiving task to the output task through buffers, never through the API or its lock; the tree configures and reports (§4.2).
- **No effects, no WLED API** (proposed, 2026-10-23): a network-to-pixels bridge first (§1).
