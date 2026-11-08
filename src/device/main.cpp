// GPSDO status display firmware for the ESP32-2432S028R "Cheap Yellow Display".
//
// Listens (receive only) to the GPSDO Nano's report and the u-blox NMEA, shows five screens
// (tap anywhere for the next one) and, once set up through the Wi-Fi setup portal, publishes
// state over MQTT. The portal starts when nothing is saved, or when the screen is held at start-up.
//
// USB console (115200 baud):
//   - prints every received line, prefixed "N> " (Nano) or "G> " (GPS)
//   - accepts replay input: lines starting "N " or "G " are fed in as if they came from the
//     Nano or GPS UART (tools/replay.py streams a capture this way), so the screens can be
//     checked without the taps wired.
//   - "setup" starts the Wi-Fi setup portal.
#include <Arduino.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <esp_timer.h>
#include <lvgl.h>

#include "config.h"
#include "line_assembler.h"
#include "monitor.h"
#include "net.h"
#include "ntp_server.h"
#include "portal.h"
#include "settings.h"
#include "ui.h"

using namespace gpsdo;

namespace {

HardwareSerial nanoSerial(1);
HardwareSerial gpsSerial(2);
SPIClass touchSpi(VSPI);
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);

Monitor monitor;
Ui ui;
Net net;
Settings settings;
Portal portal;
LineAssembler nanoLines(256), gpsLines(128), consoleLines(256);

constexpr size_t kDrawBufBytes = 320 * 24 * 2;
uint8_t* drawBuf = nullptr;

uint32_t lastTickMs = 0;
bool wasTouched = false;
uint32_t lastTouchMs = 0;
uint8_t prevGpsByte = 0;
uint32_t lastFastMs = 0;
int64_t gpsLineStartUs = 0;
bool gpsInLine = false;

// Timestamps taken in interrupt-ish context, read by the loop under a lock (64-bit values).
portMUX_TYPE tsMux = portMUX_INITIALIZER_UNLOCKED;
int64_t nanoRxUs = 0, nanoLineStartUs = 0;  // last Nano receive event; first byte of the current line
volatile int64_t ppsRiseUs = 0, ppsFallUs = 0;
volatile uint32_t ppsEdges = 0;

// UART event callback for the Nano: with the RX FIFO threshold at 1 byte it fires right after the
// first byte of a report line, which the Nano sends straight after each PPS edge.
void onNanoRx() {
  int64_t now = esp_timer_get_time();
  portENTER_CRITICAL(&tsMux);
  if (now - nanoRxUs > 300000) nanoLineStartUs = now;  // a gap: this is the start of a new line
  nanoRxUs = now;
  portEXIT_CRITICAL(&tsMux);
}

void IRAM_ATTR onPpsEdge() {
  int64_t now = esp_timer_get_time();
  if (digitalRead(PPS_PIN)) ppsRiseUs = now;
  else ppsFallUs = now;
  ppsEdges = ppsEdges + 1;
}

int64_t nanoStartUs() {
  portENTER_CRITICAL(&tsMux);
  int64_t v = nanoLineStartUs;
  portEXIT_CRITICAL(&tsMux);
  return v;
}

// Which PPS edge is the top of the second: the one just before each Nano report (0 to 60 ms
// earlier), confirmed on 3 reports in a row. 0 = not known, 1 = rising, 2 = falling.
int ppsEdgeKind = 0, ppsVotes = 0, ppsVoteKind = 0;
int64_t lastPpsFedUs = 0;
int64_t delaySum = 0;
int delayCount = 0;
uint32_t lastDelayReportMs = 0;

void judgePps(int64_t nanoStart) {
  portENTER_CRITICAL(&tsMux);
  int64_t r = ppsRiseUs, f = ppsFallUs;
  portEXIT_CRITICAL(&tsMux);
  int kind = 0;
  if (r && nanoStart - r >= 0 && nanoStart - r < 60000) kind = 1;
  else if (f && nanoStart - f >= 0 && nanoStart - f < 60000) kind = 2;
  if (kind && kind == ppsVoteKind) ++ppsVotes;
  else { ppsVoteKind = kind; ppsVotes = kind ? 1 : 0; }
  if (ppsVotes >= 3 && ppsEdgeKind != kind) {
    ppsEdgeKind = kind;
    Serial.printf("# PPS input found on IO%d, %s edge marks the second\n", PPS_PIN, kind == 1 ? "rising" : "falling");
  }
  if (ppsEdgeKind) {
    int64_t e = ppsEdgeKind == 1 ? r : f;
    if (nanoStart - e >= 0 && nanoStart - e < 60000) { delaySum += nanoStart - e; ++delayCount; }
  }
}

uint32_t tickCb() { return millis(); }

void pumpUart(HardwareSerial& port, LineAssembler& lines, bool gps) {
  // Bounded per call so a flood on one input cannot starve the UI.
  for (int n = 0; n < 512 && port.available(); ++n) {
    uint8_t c = (uint8_t)port.read();
    uint32_t now = millis();
    if (gps) {
      if (prevGpsByte == 0xB5 && c == 0x62) monitor.onUbxSeen();
      prevGpsByte = c;
      if (!gpsInLine) { gpsLineStartUs = esp_timer_get_time(); gpsInLine = true; }
    }
    if (!lines.push((char)c)) continue;
    const std::string& l = lines.line();
    Serial.print(gps ? "G> " : "N> ");
    Serial.println(l.c_str());
    if (gps) {
      monitor.onGpsLine(l, now, gpsLineStartUs);
      gpsInLine = false;
    } else {
      int64_t start = nanoStartUs();
      judgePps(start);
      monitor.onNanoLine(l, now, start);
    }
  }
}

void pumpConsole() {
  for (int n = 0; n < 512 && Serial.available(); ++n) {
    if (!consoleLines.push((char)Serial.read())) continue;
    const std::string& l = consoleLines.line();
    if (l == "setup" && !portal.active()) { portal.begin(settings); continue; }
    if (l.size() < 2 || l[1] != ' ') continue;
    if (l[0] == 'N') monitor.onNanoLine(l.substr(2), millis());
    else if (l[0] == 'G') monitor.onGpsLine(l.substr(2), millis());
    else if (l[0] == 'S') ui.show((l[2] - '1') % Ui::kScreens);
  }
}

void pollTouch(uint32_t now) {
  // Any tap moves to the next screen, so no touch calibration is needed.
  bool t = touch.tirqTouched() && touch.touched();
  if (t && !wasTouched && now - lastTouchMs > 250) {
    lastTouchMs = now;
    ui.next();
    UiExtras x;
    net.fill(x);
    ui.update(monitor, x, now);
  }
  wasTouched = t;
}

}  // namespace

void setup() {
  Serial.begin(CONSOLE_BAUD);
  Serial.println();
  Serial.println("# gpsdo-cyd starting");

  // Generous buffers: an MQTT reconnect can block the loop for several seconds.
  nanoSerial.setRxBufferSize(4096);
  nanoSerial.begin(NANO_BAUD, SERIAL_8N1, NANO_RX_PIN, -1);
  nanoSerial.setRxFIFOFull(1);
  nanoSerial.onReceive(onNanoRx, false);
  pinMode(PPS_PIN, INPUT_PULLDOWN);  // quiet when nothing is wired to it
  attachInterrupt(digitalPinToInterrupt(PPS_PIN), onPpsEdge, CHANGE);
  gpsSerial.setRxBufferSize(8192);
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, -1);

  touchSpi.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  touch.begin(touchSpi);

  lv_init();
  lv_tick_set_cb(tickCb);
  drawBuf = static_cast<uint8_t*>(heap_caps_malloc(kDrawBufBytes, MALLOC_CAP_DMA));
  lv_display_t* disp = lv_tft_espi_create(TFT_WIDTH, TFT_HEIGHT, drawBuf, kDrawBufBytes);
  lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);  // matches the PCB silkscreen orientation
  ui.begin();

  settings.load();
  // Setup portal: when nothing is saved yet, or when the screen is held while the CYD starts.
  bool held = false;
  for (int i = 0; i < 10; ++i) {
    held = touch.touched();
    if (!held) break;
    delay(50);
  }
  if (!settings.configured() || held) {
    Serial.println(held ? "# screen held at start: setup portal" : "# no Wi-Fi settings saved: setup portal");
    portal.begin(settings);
  } else {
    net.begin(settings);
  }
  Serial.printf("# ready, free heap %u\n", (unsigned)ESP.getFreeHeap());
}

void loop() {
  uint32_t now = millis();
  pumpUart(nanoSerial, nanoLines, false);
  pumpUart(gpsSerial, gpsLines, true);
  pumpConsole();
  if (portal.active()) {
    if (!portal.loop(now)) ui.show(Ui::kStatus);  // timed out: back to normal, Wi-Fi off
  } else {
    pollTouch(now);
    net.loop(monitor, now);
  }
  if (now - lastTickMs >= 1000) {
    lastTickMs = now;
    monitor.tick(now);
    if (portal.active()) {
      ui.showSetup(portal.apSsid().c_str(), portal.apPass().c_str(), "http://192.168.4.1/", portal.status().c_str(),
                   portal.msLeft(now) / 1000);
    } else {
      UiExtras x;
      net.fill(x);
      ui.update(monitor, x, now);
    }
  }
  // Feed the PPS edge to the clock as soon as it is seen, if the PPS input is wired and known.
  if (ppsEdgeKind) {
    portENTER_CRITICAL(&tsMux);
    int64_t e = ppsEdgeKind == 1 ? ppsRiseUs : ppsFallUs;
    portEXIT_CRITICAL(&tsMux);
    if (e != lastPpsFedUs) { lastPpsFedUs = e; monitor.onPpsEdge(e); }
  }
  if (delayCount >= 60 && now - lastDelayReportMs > 60000) {
    lastDelayReportMs = now;
    Serial.printf("# PPS edge to Nano report: mean %.2f ms over %d s\n", delaySum / 1000.0 / delayCount, delayCount);
    delaySum = 0;
    delayCount = 0;
  }
  // Keep the NTP server's view of the clock fresh.
  int64_t nowUs = esp_timer_get_time(), unixUs;
  bool aligned;
  if (monitor.clock().now(nowUs, unixUs, aligned)) {
    bool gpsGood = !(monitor.alarms() & (kAlarmGpsNoFix | kAlarmGpsSilent));
    bool ppsWired = ppsEdgeKind && nowUs - lastPpsFedUs < 2500000;
    NtpServer::setClock(nowUs, unixUs, aligned && gpsGood, ppsWired ? 200 : 5000);
  }
  if (now - lastFastMs >= 50) {
    lastFastMs = now;
    ui.fastUpdate(monitor, nowUs);
  }
  lv_timer_handler();
  delay(5);
}
