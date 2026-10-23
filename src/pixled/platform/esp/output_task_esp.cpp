// The output task (docs/DESIGN.md section 4.2): on core 1, away from Wi-Fi
// and lwIP, as TesserKIT's pulse trains learned (its DESIGN section 5.21).
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pixled/platform/esp/esp.h"

namespace pixled {

namespace {

const char* kTag = "pixled";

// Above the device's task and the HTTP server's (5), below lwIP's (18) and
// the DMX receivers' (12), which fill the frames it shows.
constexpr UBaseType_t kPriority = 10;
constexpr uint32_t kStackBytes = 4096;

TaskHandle_t task = nullptr;

void wake(void*) {
    if (task) xTaskNotifyGive(task);
}

void run(void* arg) {
    auto* c = static_cast<Controller*>(arg);
    for (;;) {
        // A closed frame wakes it at once; a tick at most otherwise, for
        // timeouts and test patterns.
        ulTaskNotifyTake(pdTRUE, 1);
        c->poll(static_cast<uint64_t>(esp_timer_get_time()));
    }
}

}  // namespace

bool startOutputTask(Controller& controller) {
    if (task) return true;
    controller.frames().onClose(wake, nullptr);
    if (xTaskCreatePinnedToCore(run, "pixled", kStackBytes, &controller, kPriority, &task, 1) != pdPASS) {
        ESP_LOGE(kTag, "no output task");
        return false;
    }
    return true;
}

}  // namespace pixled
