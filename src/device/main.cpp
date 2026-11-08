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
#include <lvgl.h>

#include "config.h"
#include "line_assembler.h"
#include "monitor.h"
#include "net.h"
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
uint32_t nanoLineStartMs = 0, gpsBurstStartMs = 0, lastGpsByteMs = 0, lastFastMs = 0;
bool nanoInLine = false;

uint32_t tickCb() { return millis(); }

void pumpUart(HardwareSerial& port, LineAssembler& lines, bool gps) {
  // Bounded per call so a flood on one input cannot starve the UI.
  for (int n = 0; n < 512 && port.available(); ++n) {
    uint8_t c = (uint8_t)port.read();
    uint32_t now = millis();
    if (gps) {
      if (prevGpsByte == 0xB5 && c == 0x62) monitor.onUbxSeen();
      prevGpsByte = c;
      // A gap marks the start of the next once-a-second NMEA burst.
      if (now - lastGpsByteMs > 300) gpsBurstStartMs = now;
      lastGpsByteMs = now;
    } else if (!nanoInLine && c != '\r' && c != '\n') {
      // First byte of a report line: the Nano starts printing right after each PPS edge.
      // The UART buffer can delay our read, so this is only as good as the loop is quick.
      nanoLineStartMs = now;
      nanoInLine = true;
    }
    if (!lines.push((char)c)) continue;
    const std::string& l = lines.line();
    Serial.print(gps ? "G> " : "N> ");
    Serial.println(l.c_str());
    if (gps) monitor.onGpsLine(l, now, gpsBurstStartMs);
    else {
      monitor.onNanoLine(l, now, nanoLineStartMs);
      nanoInLine = false;
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
  if (now - lastFastMs >= 50) {
    lastFastMs = now;
    ui.fastUpdate(monitor, now);
  }
  lv_timer_handler();
  delay(5);
}
