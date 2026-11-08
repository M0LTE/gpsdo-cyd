#include "watchdog.h"

#include <Arduino.h>
#include <esp_system.h>
#include <esp_timer.h>

namespace gpsdo {
namespace Watchdog {
namespace {
constexpr int64_t kLimitUs = 30LL * 1000000;
volatile int64_t lastFeedUs = 0;
esp_timer_handle_t timer = nullptr;
// Survives a software restart (not a power cut): set just before the watchdog restarts the CPU.
RTC_NOINIT_ATTR uint32_t firedMagic;
constexpr uint32_t kFired = 0x57444F47;  // "WDOG"
const char* reason = nullptr;

void check(void*) {
  if (esp_timer_get_time() - lastFeedUs > kLimitUs) {
    firedMagic = kFired;
    Serial.println("# watchdog: main loop stuck for 30 s, restarting");
    Serial.flush();
    esp_restart();
  }
}
}  // namespace

void begin() {
  esp_reset_reason_t r = esp_reset_reason();
  if (firedMagic == kFired && r == ESP_RST_SW) reason = "Display restarted by its watchdog (loop stuck)";
  else if (r == ESP_RST_PANIC) reason = "Display restarted after a crash";
  else if (r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT) reason = "Display restarted by the chip's watchdog";
  else if (r == ESP_RST_BROWNOUT) reason = "Display restarted: its USB power dipped";
  firedMagic = 0;
  lastFeedUs = esp_timer_get_time();
  esp_timer_create_args_t a = {};
  a.callback = check;
  a.name = "loopwdt";
  esp_timer_create(&a, &timer);
  esp_timer_start_periodic(timer, 1000000);
  if (reason) Serial.printf("# %s\n", reason);
}

void feed() { lastFeedUs = esp_timer_get_time(); }

const char* lastResetReason() { return reason; }
}  // namespace Watchdog
}  // namespace gpsdo
