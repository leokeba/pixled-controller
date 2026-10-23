// PixelBus on Espressif's led_strip (docs/DESIGN.md section 5.3): one RMT
// channel per output, the chip's timings given as custom ones, the colour
// order already applied (the strip's own format is the wire's order).
#include <vector>

#include "esp_log.h"
#include "led_strip.h"
#include "pixled/platform/esp/esp.h"
#include "sdkconfig.h"
#include "soc/soc_caps.h"

namespace pixled {

namespace {

const char* kTag = "pixled";

// RMT ticks of 50 ns: every chip's times are multiples of it.
constexpr uint32_t kRmtHz = 20000000;

// The RMT's memory blocks a transmitting channel may take (the macro's name
// moved between ESP-IDF 5 and 6): the classic ESP32's eight channels share
// eight blocks; the S3's four transmitters have one each to share.
#if CONFIG_IDF_TARGET_ESP32
constexpr size_t kTxBlocks = 8;
#elif CONFIG_IDF_TARGET_ESP32S3
constexpr size_t kTxBlocks = 4;
#else
constexpr size_t kTxBlocks = 2;
#endif

class LedStripBus : public PixelBus {
public:
    bool begin(const std::vector<BusOutput>& outputs) override {
        end();
        size_t used = 0;
        for (const BusOutput& o : outputs) used += o.pixels > 0;
        // The RMT's memory shared between the outputs: more blocks each, fewer
        // refill interrupts.
        size_t blocks = used ? kTxBlocks / used : 1;
        if (blocks == 0) blocks = 1;
        for (const BusOutput& o : outputs) {
            led_strip_handle_t h = nullptr;
            const Chip& c = chip(o.chip);
            if (o.pixels > 0) {
                led_strip_config_t config = {};
                config.strip_gpio_num = o.gpio;
                config.max_leds = o.pixels;
                config.led_model = LED_MODEL_CUSTOM;
                config.color_component_format = c.channels == 4 ? LED_STRIP_COLOR_COMPONENT_FMT_RGBW
                                                                : LED_STRIP_COLOR_COMPONENT_FMT_RGB;
                config.timings.t0h_ns = c.t0hNs;
                config.timings.t0l_ns = c.t0lNs;
                config.timings.t1h_ns = c.t1hNs;
                config.timings.t1l_ns = c.t1lNs;
                config.timings.reset_us = c.resetUs;
                led_strip_rmt_config_t rmt = {};
                rmt.clk_src = RMT_CLK_SRC_DEFAULT;
                rmt.resolution_hz = kRmtHz;
                rmt.mem_block_symbols = blocks * SOC_RMT_MEM_WORDS_PER_CHANNEL;
                esp_err_t err = led_strip_new_rmt_device(&config, &rmt, &h);
                if (err != ESP_OK) {
                    ESP_LOGE(kTag, "output on GPIO %d: %s", o.gpio, esp_err_to_name(err));
                    end();
                    return false;
                }
                led_strip_clear(h);
            }
            strips_.push_back(h);
            channels_.push_back(c.channels);
        }
        return true;
    }

    void end() override {
        wait();
        for (led_strip_handle_t h : strips_) {
            if (h) led_strip_del(h);
        }
        strips_.clear();
        channels_.clear();
    }

    void wait() override {
        if (!sending_) return;
        for (led_strip_handle_t h : strips_) {
            if (h) led_strip_refresh_async_done(h);
        }
        sending_ = false;
    }

    void write(size_t output, size_t first, const uint8_t* wire, size_t count) override {
        if (output >= strips_.size() || !strips_[output]) return;
        led_strip_handle_t h = strips_[output];
        uint8_t ch = channels_[output];
        for (size_t i = 0; i < count; i++, wire += ch) {
            uint32_t index = static_cast<uint32_t>(first + i);
            if (ch == 4) {
                led_strip_set_pixel_rgbw(h, index, wire[0], wire[1], wire[2], wire[3]);
            } else {
                led_strip_set_pixel(h, index, wire[0], wire[1], wire[2]);
            }
        }
    }

    bool show() override {
        bool ok = true;
        for (led_strip_handle_t h : strips_) {
            if (h && led_strip_refresh_async(h) != ESP_OK) ok = false;
        }
        sending_ = true;
        return ok;
    }

private:
    std::vector<led_strip_handle_t> strips_;  // null for an output without pixels
    std::vector<uint8_t> channels_;
    bool sending_ = false;
};

}  // namespace

PixelBus& espBus() {
    static LedStripBus bus;
    return bus;
}

}  // namespace pixled
