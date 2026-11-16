#include "watchdog.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>
#include <esp_core_dump.h>
#include <esp_timer.h>

#include <cstdio>
#include <cstring>
#include <new>
#include <string>

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
char crashReason[47];
std::string crashText;

// Keeps only plain characters, so a corrupt dump cannot break the JSON.
std::string safe(const char* p, size_t n) {
  std::string o;
  for (size_t i = 0; i < n && p[i]; i++) o += (isalnum((unsigned char)p[i]) || p[i] == '_' || p[i] == '-') ? p[i] : '?';
  return o;
}

// Reads the core dump the chip saved to flash at its last crash (it survives restarts and app
// flashes until the next crash overwrites it). Decode the addresses with addr2line against the
// firmware.elf of the build that crashed. A dump already reported at an earlier start is marked
// "new":false and not named in the event log, so a crash whose dump failed to save is not blamed
// on an older one.
void readCoreDump() {
  if (esp_core_dump_image_check() != ESP_OK) return;
  esp_core_dump_summary_t* s = new (std::nothrow) esp_core_dump_summary_t;
  if (!s) return;
  if (esp_core_dump_get_summary(s) == ESP_OK) {
    uint32_t id = 2166136261u;  // FNV-1a over the crash point and backtrace
    auto mix = [&id](uint32_t v) { for (int k = 0; k < 4; k++) { id ^= (v >> (8 * k)) & 0xff; id *= 16777619u; } };
    mix(s->exc_tcb);
    mix(s->exc_pc);
    for (uint32_t i = 0; i < s->exc_bt_info.depth && i < 16; i++) mix(s->exc_bt_info.bt[i]);
    Preferences p;
    bool fresh = true;
    if (p.begin("crash", false)) {
      fresh = p.getUInt("seen", 0) != id;
      if (fresh) p.putUInt("seen", id);
      p.end();
    }
    std::string task = safe(s->exc_task, sizeof(s->exc_task));
    char b[48];
    crashText = "{\"new\":";
    crashText += fresh ? "true" : "false";
    crashText += ",\"task\":\"" + task;
    snprintf(b, sizeof(b), "\",\"pc\":\"0x%08x\",\"cause\":%u,", (unsigned)s->exc_pc, (unsigned)s->ex_info.exc_cause);
    crashText += b;
    snprintf(b, sizeof(b), "\"vaddr\":\"0x%08x\",\"elf_sha256\":\"", (unsigned)s->ex_info.exc_vaddr);
    crashText += b;
    crashText += safe(reinterpret_cast<const char*>(s->app_elf_sha256), sizeof(s->app_elf_sha256));
    crashText += s->exc_bt_info.corrupted ? "\",\"backtrace_corrupted\":true,\"backtrace\":\"" : "\",\"backtrace\":\"";
    for (uint32_t i = 0; i < s->exc_bt_info.depth && i < 16; i++) {
      snprintf(b, sizeof(b), i ? " 0x%08x" : "0x%08x", (unsigned)s->exc_bt_info.bt[i]);
      crashText += b;
    }
    crashText += "\"}";
    if (fresh) snprintf(crashReason, sizeof(crashReason), "Display crashed in %.11s, see /api/crash", task.c_str());
  }
  delete s;
}

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
  readCoreDump();
  if (firedMagic == kFired && r == ESP_RST_SW) reason = "Display restarted by its watchdog (loop stuck)";
  else if (r == ESP_RST_PANIC) reason = crashReason[0] ? crashReason : "Display restarted after a crash";
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
  if (!crashText.empty()) Serial.printf("# last crash: %s\n", crashText.c_str());
}

void feed() { lastFeedUs = esp_timer_get_time(); }

const char* lastResetReason() { return reason; }

const std::string& lastCrash() { return crashText; }
}  // namespace Watchdog
}  // namespace gpsdo
