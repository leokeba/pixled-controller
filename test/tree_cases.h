// The tree (docs/DESIGN.md section 6): declared cleanly, validated, applied.
#pragma once

#include "rig.h"

using tesser::Status;

TEST(tree_declares_cleanly) {
    Rig r;
    CHECK_EQ(tesser::declarationErrors(), 0);
    CHECK(r.get("/outputs").status == Status::Ok);
    CHECK_EQ(r.get("/outputs/list/1/chip").body, "\"ws2812\"");
    CHECK_EQ(r.get("/outputs/list/0/order").body, "\"grb\"");
    CHECK_EQ(r.get("/outputs/gpios").body, "\"16, 12, 4, 2\"");
    CHECK_EQ(r.get("/inputs/status/state").body, "\"idle\"");
    CHECK_EQ(r.get("/board/active").body, "\"Gledopto Elite 4D-EXMU\"");
    CHECK_EQ(r.get("/board/restartNeeded").body, "false");
}

TEST(tree_outputs_apply) {
    Rig r;
    CHECK(r.set("/outputs/list/0", R"({"pixels":60,"universe":5,"order":"rgb","chip":"sk6812-rgbw"})").status ==
          Status::Ok);
    r.poll();
    CHECK_EQ(r.bus.outputs[0].pixels, 60);
    CHECK(r.bus.outputs[0].chip == ChipId::Sk6812Rgbw);
    std::vector<uint16_t> u = r.controller.frames().universes();
    CHECK_EQ(u[0], 1);
    CHECK_EQ(u.back(), 5);
}

TEST(tree_refuses) {
    Rig r;
    CHECK(r.set("/outputs/list/0/pixels", "2049").status == Status::InvalidValue);
    CHECK(r.set("/outputs/list/0/channel", "0").status == Status::InvalidValue);
    CHECK(r.set("/outputs/list/0/channel", "513").status == Status::InvalidValue);
    CHECK(r.set("/outputs/list/0/chip", "\"apa102\"").status == Status::InvalidValue);
    CHECK(r.set("/outputs/testColour", "\"red\"").status == Status::InvalidValue);
    CHECK(r.set("/outputs/test", "\"fade\"").status == Status::Ok);
    CHECK(r.set("/outputs/test", "\"purple\"").status == Status::InvalidValue);
    CHECK(r.set("/outputs/testColour", "\"#00ff00\"").status == Status::Ok);
    CHECK(r.set("/power/brightness", "101").status == Status::InvalidValue);
    CHECK(r.set("/inputs/onLoss", "\"black\"").status == Status::Ok);
    CHECK(r.set("/inputs/sacn", "false").status == Status::Ok);
    CHECK_EQ(r.get("/inputs/artnet").body, "true");
    CHECK(r.set("/inputs/status/state", "\"lost\"").status == Status::ReadOnly);
}

TEST(tree_board) {
    Rig r;
    CHECK(r.set("/board/custom", R"("{\"outputs\":[{\"gpio\":0}]}")").status == Status::InvalidValue);
    CHECK(r.set("/board/custom", R"("{\"name\":\"Bench\",\"outputs\":[{\"gpio\":4}]}")").status == Status::Ok);
    CHECK_EQ(r.get("/board/restartNeeded").body, "true");
    CHECK(r.set("/board/preset", "\"custom\"").status == Status::Ok);
    // In use until a restart.
    CHECK_EQ(r.get("/board/active").body, "\"Gledopto Elite 4D-EXMU\"");
    CHECK(r.set("/board/preset", "\"gledopto-elite-9d\"").status == Status::InvalidValue);
}
