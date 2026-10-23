#include "pixled/board.h"

#include <string.h>

#include <ArduinoJson.h>

#include "tesserkit/network.h"

namespace pixled {

namespace {

// GLEDOPTO's Elite controllers, from their manuals (2026-10-23): to check on
// each board before trusting (docs/DESIGN.md section 3.1). The 1D2P's pins
// aren't documented yet.
const char kElite2d[] = R"({"name":"Gledopto Elite 2D-EXMU","ethernet":"gledopto",
"outputs":[{"gpio":16},{"gpio":2}],"relay":{"gpio":18,"activeLow":true}})";

const char kElite4d[] = R"({"name":"Gledopto Elite 4D-EXMU","ethernet":"gledopto",
"outputs":[{"gpio":16},{"gpio":12},{"gpio":4},{"gpio":2}],"relay":{"gpio":18,"activeLow":true},"button":{"gpio":17}})";

const char kElite6d[] = R"({"name":"Gledopto Elite 6D-EXMU","ethernet":"gledopto",
"outputs":[{"gpio":16},{"gpio":14},{"gpio":13},{"gpio":12},{"gpio":4},{"gpio":2}],
"relay":{"gpio":18,"activeLow":true},"button":{"gpio":17}})";

const char kElite5p[] = R"({"name":"Gledopto Elite 5P-EXMU","ethernet":"gledopto",
"outputs":[{"gpio":18,"kind":"pwm"},{"gpio":16,"kind":"pwm"},{"gpio":12,"kind":"pwm"},{"gpio":4,"kind":"pwm"},
{"gpio":2,"kind":"pwm"}],"button":{"gpio":17}})";

// The classic ESP32's GPIOs: which exist, and which can drive an output.
bool exists(int g) { return g >= 0 && g <= 39 && g != 20 && g != 24 && !(g >= 28 && g <= 31); }
bool canDrive(int g) { return exists(g) && g < 34; }
// The SPI flash's pins.
bool flash(int g) { return g >= 6 && g <= 11; }

// The pins an Ethernet preset takes: RMII's fixed ones, MDC, MDIO, the
// PHY's power and the clock.
bool ethernetPin(const tesserkit::EthernetBoard& e, int g) {
    static constexpr int kRmii[] = {19, 21, 22, 25, 26, 27};
    for (int p : kRmii) {
        if (g == p) return true;
    }
    return g == e.mdc || g == e.mdio || g == e.power || g == e.clockGpio;
}

}  // namespace

size_t Board::digitalOutputs() const {
    size_t n = 0;
    for (const BoardOutput& o : outputs) {
        if (o.kind == BoardOutput::Kind::Digital) n++;
    }
    return n;
}

const std::vector<const char*>& ethernetNames() {
    static const std::vector<const char*> names = {"gledopto", "wt32-eth01", "olimex-esp32-poe",
                                                   "esp32-ethernet-kit"};
    return names;
}

bool ethernetBoard(std::string_view name, tesserkit::EthernetBoard& out) {
    return tesserkit::boards::byName(std::string(name), out);
}

const std::vector<Preset>& presets() {
    static const std::vector<Preset> list = {
        {"gledopto-elite-2d", kElite2d},
        {"gledopto-elite-4d", kElite4d},
        {"gledopto-elite-6d", kElite6d},
        {"gledopto-elite-5p", kElite5p},
    };
    return list;
}

const Preset* findPreset(std::string_view id) {
    for (const Preset& p : presets()) {
        if (id == p.id) return &p;
    }
    return nullptr;
}

bool BoardChoice::operator==(const BoardChoice& o) const {
    return preset == o.preset && strncmp(custom, o.custom, sizeof custom) == 0;
}

bool BoardChoice::resolve(Board& out, std::string& error) const {
    out = Board();
    if (preset == BoardPreset::None) {
        error = "no board chosen: /board/preset";
        return false;
    }
    if (preset == BoardPreset::Custom) {
        if (!custom[0]) {
            error = "no custom board file: /board/custom";
            return false;
        }
        return parseBoard(std::string_view(custom, strnlen(custom, sizeof custom)), out, error);
    }
    size_t i = static_cast<size_t>(preset) - 1;
    if (i >= presets().size()) {
        error = "unknown preset";
        return false;
    }
    return parseBoard(presets()[i].json, out, error);
}

bool BoardChoice::ethernet(tesserkit::EthernetBoard& out) const {
    if (preset == BoardPreset::None) return ethernetBoard("gledopto", out);
    Board b;
    std::string error;
    return resolve(b, error) && !b.ethernet.empty() && ethernetBoard(b.ethernet, out);
}

const Board& BoardSelection::boot() {
    atBoot_ = choice;
    error_.clear();
    choice.resolve(board_, error_);
    booted_ = true;
    return board_;
}

bool parseBoard(std::string_view json, Board& out, std::string& error) {
    JsonDocument doc;
    if (deserializeJson(doc, json.data(), json.size()) != DeserializationError::Ok || !doc.is<JsonObjectConst>()) {
        error = "not a JSON object";
        return false;
    }
    JsonObjectConst root = doc.as<JsonObjectConst>();
    for (JsonPairConst kv : root) {
        std::string_view k = kv.key().c_str();
        if (k != "name" && k != "ethernet" && k != "outputs" && k != "relay" && k != "button") {
            error = "unknown key \"" + std::string(k) + "\"";
            return false;
        }
    }

    Board b;
    b.name = root["name"] | "";
    tesserkit::EthernetBoard eth;
    bool hasEthernet = false;
    if (!root["ethernet"].isNull()) {
        if (!root["ethernet"].is<const char*>()) {
            error = "ethernet: a preset's name";
            return false;
        }
        b.ethernet = root["ethernet"].as<const char*>();
        if (!b.ethernet.empty()) {
            if (!ethernetBoard(b.ethernet, eth)) {
                error = "ethernet: no preset \"" + b.ethernet + "\"";
                return false;
            }
            hasEthernet = true;
        }
    }

    std::vector<int> taken;
    // A GPIO for `what`: one that exists, drives an output when `drive`, and
    // isn't the flash's, Ethernet's or already taken.
    auto pin = [&](JsonVariantConst v, const char* what, bool drive, int8_t& to) {
        if (!v.is<int>()) {
            error = std::string(what) + ": gpio must be a number";
            return false;
        }
        int g = v.as<int>();
        std::string name = std::string(what) + ": GPIO " + std::to_string(g);
        if (!exists(g)) {
            error = name + " doesn't exist";
            return false;
        }
        if (drive && !canDrive(g)) {
            error = name + " is an input only";
            return false;
        }
        if (flash(g)) {
            error = name + " is the flash's";
            return false;
        }
        if (g == 0 || (hasEthernet && ethernetPin(eth, g))) {
            error = name + (g == 0 ? " is a boot strapping pin and Ethernet's clock" : " is Ethernet's");
            return false;
        }
        for (int t : taken) {
            if (t == g) {
                error = name + " is used twice";
                return false;
            }
        }
        taken.push_back(g);
        to = static_cast<int8_t>(g);
        return true;
    };

    JsonVariantConst outputs = root["outputs"];
    if (!outputs.is<JsonArrayConst>()) {
        error = "outputs: a list";
        return false;
    }
    if (outputs.size() > kMaxOutputs) {
        error = "outputs: at most " + std::to_string(kMaxOutputs);
        return false;
    }
    for (JsonVariantConst o : outputs.as<JsonArrayConst>()) {
        std::string what = "output " + std::to_string(b.outputs.size() + 1);
        if (!o.is<JsonObjectConst>()) {
            error = what + ": an object";
            return false;
        }
        BoardOutput bo;
        if (!pin(o["gpio"], what.c_str(), true, bo.gpio)) return false;
        std::string_view kind = o["kind"] | "digital";
        if (kind == "pwm") {
            bo.kind = BoardOutput::Kind::Pwm;
        } else if (kind != "digital") {
            error = what + ": kind is digital or pwm";
            return false;
        }
        b.outputs.push_back(bo);
    }
    if (!root["relay"].isNull()) {
        if (!pin(root["relay"]["gpio"], "relay", true, b.relayGpio)) return false;
        b.relayActiveLow = root["relay"]["activeLow"] | false;
    }
    if (!root["button"].isNull()) {
        if (!pin(root["button"]["gpio"], "button", false, b.buttonGpio)) return false;
        b.buttonActiveLow = root["button"]["activeLow"] | true;
    }
    out = std::move(b);
    return true;
}

}  // namespace pixled
