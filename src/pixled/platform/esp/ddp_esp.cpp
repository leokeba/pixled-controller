// DDP's socket (docs/DESIGN.md section 5.1): a task of its own, blocked on
// the socket, so packets are copied as they arrive.
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "pixled/ddp.h"
#include "pixled/platform/esp/esp.h"

namespace pixled {

namespace {

const char* kTag = "pixled";

// As TesserKIT's DMX receivers: above the output task, below lwIP.
constexpr UBaseType_t kPriority = 12;
constexpr uint32_t kStackBytes = 3072;
// An Ethernet frame's payload.
constexpr size_t kMaxDatagram = 1472;

void run(void* arg) {
    auto* c = static_cast<Controller*>(arg);
    int fd = -1;
    static uint8_t buffer[kMaxDatagram];
    for (;;) {
        if (fd < 0) {
            fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            sockaddr_in addr = {};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(DdpPacket::kPort);
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
            if (fd < 0 || bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
                ESP_LOGW(kTag, "DDP: can't open UDP %u, trying again in 5 s", DdpPacket::kPort);
                if (fd >= 0) close(fd);
                fd = -1;
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
        }
        sockaddr_in from = {};
        socklen_t fromLen = sizeof from;
        int n = recvfrom(fd, buffer, sizeof buffer, 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n <= 0) continue;
        if (!c->ddpEnabled()) continue;
        DdpPacket p;
        if (!parseDdp(buffer, static_cast<size_t>(n), p) || p.query || p.reply || p.id != DdpPacket::kDisplay) {
            continue;
        }
        c->frames().ddp(p.offset, p.data, p.length, p.push, ntohl(from.sin_addr.s_addr),
                        static_cast<uint64_t>(esp_timer_get_time()));
    }
}

}  // namespace

bool startDdp(Controller& controller) {
    static bool started = false;
    if (started) return true;
    if (xTaskCreate(run, "pixled-ddp", kStackBytes, &controller, kPriority, nullptr) != pdPASS) {
        ESP_LOGE(kTag, "no DDP task");
        return false;
    }
    started = true;
    return true;
}

}  // namespace pixled
