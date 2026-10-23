// The /outputs, /inputs, /power and /board trees (docs/DESIGN.md section 6).
#include "pixled/tree.h"

#include <stdio.h>

namespace pixled {

namespace {

using tesser::Access;
using tesser::Check;
using tesser::Object;
using tesser::Status;

void describeOutput(Object& e, OutputSettings& v) {
    e.value("label", v.label).doc("A name for this output: what it lights");
    e.value("chip", v.chip, {"ws2812", "ws2811", "ws2815", "sk6812-rgbw"})
        .doc("The LED chip on the strip: its timing, and three or four channels per pixel");
    e.value("order", v.order, {"rgb", "rbg", "grb", "gbr", "brg", "bgr"})
        .label("Colour order")
        .doc("The order the chip takes red, green and blue in (white comes last). GRB for a WS2812B; if red "
             "shows green, try another");
    e.value("pixels", v.pixels).range(0, kMaxPixels).doc("Pixels on the strip");
    e.value("universe", v.universe)
        .range(0, 63999)
        .doc("Where the first pixel is: Art-Net's port-address (0–32767) or sACN's universe (1–63999)");
    e.value("channel", v.channel).range(1, 512).doc("The first pixel's first channel in that universe");
    e.value("split", v.split)
        .ui("advanced")
        .doc("Pixels run on across universes byte by byte. Off, a universe holds whole pixels only (170 RGB, "
             "128 RGBW), as most senders expect");
    e.value("reverse", v.reverse).doc("The last pixel first: for a strip fed from its other end");
}

void describeOutputs(Object& o, Controller& c) {
    o.writeAccess(Access::User);
    Settings& s = c.settings;
    o.list("list", s.outputs, describeOutput)
        .maxSize(kMaxOutputs)
        .persist()
        .label("Outputs")
        .doc("One per digital output of the board, in its order");
    o.value("gpios", [&c] {
          std::string list;
          for (const BoardOutput& b : c.board().outputs) {
              if (b.kind != BoardOutput::Kind::Digital) continue;
              list += (list.empty() ? "" : ", ") + std::to_string(b.gpio);
          }
          return list;
      })
        .label("GPIOs")
        .ui("advanced")
        .doc("The board's digital outputs, in the list's order");
    o.value("maxFps", s.maxFps)
        .range(0, 120)
        .unit("fps")
        .persist()
        .label("Frame rate cap")
        .ui("advanced")
        .doc("At most this many frames a second; 0: as fast as they come");
    o.value("test", s.test, {"off", "red", "green", "blue", "white", "black", "fade", "colour", "gradient"})
        .label("Test mode")
        .watch()
        .doc("Overrides the input while on. white: the white channel alone on RGBW chips. fade: every pixel "
             "through the rainbow together, once in 6 s. colour: the test colour. gradient: each output numbered by dots at its start "
             "(output 2 shows two), then a moving rainbow. The board's button: a long press turns red on or "
             "everything off, a short one goes on through green, blue, white, black and fade");
    o.value("testColour", s.testColour)
        .label("Test colour")
        .ui("visibleWhen", "test=colour")
        .validate([](const std::string& v) {
            uint8_t rgb[3];
            return parseColour(v.c_str(), rgb) ? Check::ok() : Check::fail(Status::InvalidValue, "#rrggbb");
        });
    o.value("error", [&c] { return c.error(); }).watch().ui("style", "warning").ui("visibleWhen", "error!=");
    auto& st = o.object("status").label("Status").ui("inline");
    st.value("fps", [&c] { return c.fps(); }).unit("fps").ui("precision", 1).watch().doc("Frames shown a second");
    st.value("frames", [&c] { return c.shown(); }).ui("advanced").watch().doc("Frames shown since boot");
    st.value("dropped", [&c] { return c.frames().stats().dropped; })
        .watch()
        .doc("Frames received but not shown: a newer one came before the strips were free");
}

void describeInputs(Object& o, Controller& c) {
    o.writeAccess(Access::User);
    Settings& s = c.settings;
    o.value("artnet", s.artnet).label("Art-Net").persist().doc("Pixels over Art-Net, on UDP 6454 (/sys/artnet)");
    o.value("sacn", s.sacn).label("sACN").persist().doc("Pixels over sACN (E1.31), on UDP 5568 (/sys/sacn)");
    o.value("ddp", s.ddp).label("DDP").persist().doc("Pixels over DDP, on UDP 4048");
    o.value("onLoss", s.onLoss, {"hold", "black", "test"})
        .label("On loss of signal")
        .persist()
        .doc("When nothing comes for the timeout: keep the last frame, go dark, or show the test pattern");
    o.value("lossTimeout", s.lossTimeout).range(0.1, 600).step(0.1).unit("s").persist().label("Loss timeout");
    auto& st = o.object("status").label("Status");
    st.value("state", [&c] { return c.state(); }, {"idle", "receiving", "lost"})
        .watch()
        .doc("idle: nothing received since boot");
    st.value("syncing", [&c] { return c.syncing(); })
        .watch()
        .doc("Frames shown on the sender's sync packets (ArtSync, sACN sync, DDP push) rather than as they "
             "complete");
    st.value("protocol", [&c] { return c.frames().lastProtocol(); }, {"none", "artnet", "sacn", "ddp"}).watch();
    st.value("source", [&c] {
          uint32_t a = c.frames().lastSource();
          char text[16];
          snprintf(text, sizeof text, "%u.%u.%u.%u", static_cast<unsigned>(a >> 24), static_cast<unsigned>(a >> 16 & 255),
                   static_cast<unsigned>(a >> 8 & 255), static_cast<unsigned>(a & 255));
          return std::string(a ? text : "");
      })
        .watch()
        .doc("The sender's address");
    st.value("universes", [&c] { return static_cast<uint32_t>(c.frames().universesReceived()); })
        .watch()
        .doc("Universes the outputs read that came in the last frame");
    st.value("packets", [&c] { return c.frames().stats().packets; }).ui("advanced").watch();
    st.value("refused", [&c] { return c.frames().stats().refused; })
        .ui("advanced")
        .watch()
        .doc("Packets for universes no output reads, or from a second sender while the first is heard");
    st.value("syncs", [&c] { return c.frames().stats().syncs; }).ui("advanced").watch();
}

void describePower(Object& o, Controller& c) {
    o.writeAccess(Access::User);
    o.value("brightness", c.settings.brightness)
        .range(0, 100)
        .step(1)
        .unit("%")
        .persist()
        .doc("Scales every frame");
}

void describeBoard(Object& o, BoardSelection& b) {
    o.writeAccess(Access::Admin);
    o.value("preset", b.choice.preset,
            {"none", "gledopto-elite-2d", "gledopto-elite-4d", "gledopto-elite-6d", "gledopto-elite-5p",
             "gledopto-elite-1d2p", "custom"})
        .persist()
        .doc("The board's wiring: a GLEDOPTO Elite controller, or custom (a board file below)");
    o.value("custom", b.choice.custom)
        .persist()
        .ui("visibleWhen", "preset=custom")
        .doc("A board file, JSON: name, ethernet, outputs [{gpio, kind}], relay {gpio, activeLow}, button {gpio}")
        .validate([](const std::string& v) {
            // A Check carries a message it doesn't own: this one lives until
            // the next refusal, past the reply (validators run under the API
            // lock).
            static std::string refusal;
            if (v.empty()) return Check::ok();
            Board board;
            if (parseBoard(v, board, refusal)) return Check::ok();
            return Check::fail(Status::InvalidValue, refusal.c_str());
        });
    o.value("active", [&b] { return b.board().name.empty() ? std::string("none") : b.board().name; })
        .doc("The board in use since the last restart");
    o.value("error", [&b] { return b.error(); }).ui("style", "warning").ui("visibleWhen", "error!=");
    o.value("restartNeeded", [&b] { return b.restartNeeded(); }).ui("hidden").watch();
    o.value("restartWarning", "A new board applies after a restart.")
        .label("Restart needed")
        .ui("style", "warning")
        .ui("visibleWhen", "restartNeeded=true");
}

}  // namespace

void mountTree(tesser::Api& api, Controller& controller, BoardSelection& board) {
    describeOutputs(api.object("outputs").label("Outputs").ui("icon", "bulb"), controller);
    describeInputs(api.object("inputs").label("Inputs").ui("icon", "broadcast"), controller);
    describePower(api.object("power").label("Power").ui("icon", "bolt"), controller);
    describeBoard(api.object("board").label("Board").ui("icon", "layers").ui("group", "Hardware"), board);
}

}  // namespace pixled
