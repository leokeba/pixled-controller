// A controller on the host: a fake bus that keeps the bytes each output was
// sent, the board, the tree, and requests through the API as a client would
// send them.
#pragma once

#include <string.h>

#include <string>
#include <string_view>
#include <vector>

#include <ArduinoJson.h>

#include "Pixled.h"
#include "TesserAPI.h"
#include "check.h"

namespace {

using pixled::BusOutput;
using pixled::ChipId;

// The strips: each output's wire bytes as last written, and what was asked.
struct FakeBus : pixled::PixelBus {
    std::vector<BusOutput> outputs;
    std::vector<std::vector<uint8_t>> wire;
    std::vector<std::vector<uint8_t>> sent;  // as of the last show()
    bool refuse = false;
    int begins = 0, ends = 0, waits = 0, shows = 0;
    bool overrun = false;  // a write past an output's pixels

    bool begin(const std::vector<BusOutput>& o) override {
        begins++;
        if (refuse) return false;
        outputs = o;
        wire.clear();
        for (const BusOutput& b : o) wire.emplace_back(b.pixels * pixled::chip(b.chip).channels, 0);
        sent = wire;
        return true;
    }
    void end() override {
        ends++;
        outputs.clear();
        wire.clear();
    }
    void wait() override { waits++; }
    void write(size_t output, size_t first, const uint8_t* bytes, size_t count) override {
        size_t ch = pixled::chip(outputs[output].chip).channels;
        if ((first + count) * ch > wire[output].size()) {
            overrun = true;
            return;
        }
        memcpy(wire[output].data() + first * ch, bytes, count * ch);
    }
    bool show() override {
        shows++;
        sent = wire;
        return true;
    }
    // Pixel `p` of output `o` as sent, "r,g,b" in wire order.
    std::string pixel(size_t o, size_t p) const {
        size_t ch = pixled::chip(outputs[o].chip).channels;
        std::string s;
        for (size_t k = 0; k < ch; k++) s += (k ? "," : "") + std::to_string(sent[o][p * ch + k]);
        return s;
    }
};

// The Elite 4D's board file.
pixled::Board elite4d() {
    pixled::Board b;
    std::string error;
    pixled::parseBoard(pixled::findPreset("gledopto-elite-4d")->json, b, error);
    return b;
}

struct Rig {
    FakeBus bus;
    pixled::Controller controller{bus};
    pixled::BoardSelection board;
    tesser::Api api;
    uint64_t now = 1000000;

    explicit Rig(bool begin = true) {
        pixled::mountTree(api, controller, board);
        if (begin) {
            board.choice.preset = pixled::BoardPreset::Elite4d;
            controller.begin(board.boot());
            poll();
        }
    }

    // TesserKIT's tick, then the output task.
    void poll(uint64_t advanceUs = 0) {
        now += advanceUs;
        controller.update();
        controller.poll(now);
    }

    // A universe of `channels` bytes, each byte `value`, or counting up from
    // `value` when `count`.
    void dmx(uint16_t universe, size_t channels, uint8_t value, bool count = false, uint32_t source = 0x0a000001,
             pixled::Protocol p = pixled::Protocol::ArtNet) {
        std::vector<uint8_t> data(channels);
        for (size_t i = 0; i < channels; i++) data[i] = static_cast<uint8_t>(count ? value + i : value);
        controller.frames().dmx(p, universe, data.data(), data.size(), source, now);
    }

    struct Reply {
        tesser::Status status;
        std::string body;
    };
    Reply request(tesser::Op op, std::string_view path, std::string_view body = {}) {
        JsonDocument doc;
        if (!body.empty()) deserializeJson(doc, body.data(), body.size());
        tesser::Request req;
        req.op = op;
        req.path = path;
        req.body = doc.as<JsonVariantConst>();
        req.client.authenticated = true;
        tesser::StringReply reply;
        api.handle(req, reply);
        return {reply.status, reply.body};
    }
    Reply get(std::string_view path) { return request(tesser::Op::Get, path); }
    Reply set(std::string_view path, std::string_view body) { return request(tesser::Op::Set, path, body); }
};

}  // namespace
