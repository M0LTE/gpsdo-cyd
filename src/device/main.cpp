// GPSDO status display firmware for the ESP32-2432S028R "Cheap Yellow Display".
//
// Listens (receive only) to the GPSDO Nano's report and the u-blox NMEA, shows four screens
// (tap anywhere for the next one) and, if include/secrets.h exists, publishes state over MQTT.
//
// USB console (115200 baud):
//   - prints every received line, prefixed "N> " (Nano) or "G> " (GPS)
//   - accepts replay input: lines starting "N " or "G " are fed in as if they came from the
//     Nano or GPS UART (tools/replay.py streams a capture this way), so the screens can be
//     checked without the taps wired.
#include <Arduino.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <lvgl.h>

#include "config.h"
#include "line_assembler.h"
#include "monitor.h"
#include "net.h"
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
LineAssembler nanoLines(256), gpsLines(128), consoleLines(256);

constexpr size_t kDrawBufBytes = 320 * 24 * 2;
uint8_t* drawBuf = nullptr;

uint32_t lastTickMs = 0;
bool wasTouched = false;
uint32_t lastTouchMs = 0;
uint8_t prevGpsByte = 0;

uint32_t tickCb() { return millis(); }

void pumpUart(HardwareSerial& port, LineAssembler& lines, bool gps) {
  // Bounded per call so a flood on one input cannot starve the UI.
  for (int n = 0; n < 512 && port.available(); ++n) {
    uint8_t c = (uint8_t)port.read();
    if (gps) {
      if (prevGpsByte == 0xB5 && c == 0x62) monitor.onUbxSeen();
      prevGpsByte = c;
    }
    if (!lines.push((char)c)) continue;
    const std::string& l = lines.line();
    Serial.print(gps ? "G> " : "N> ");
    Serial.println(l.c_str());
    if (gps) monitor.onGpsLine(l, millis());
    else monitor.onNanoLine(l, millis());
  }
}

void pumpConsole() {
  for (int n = 0; n < 512 && Serial.available(); ++n) {
    if (!consoleLines.push((char)Serial.read())) continue;
    const std::string& l = consoleLines.line();
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

  nanoSerial.setRxBufferSize(1024);
  nanoSerial.begin(NANO_BAUD, SERIAL_8N1, NANO_RX_PIN, -1);
  gpsSerial.setRxBufferSize(2048);
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, -1);

  touchSpi.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  touch.begin(touchSpi);

  lv_init();
  lv_tick_set_cb(tickCb);
  drawBuf = static_cast<uint8_t*>(heap_caps_malloc(kDrawBufBytes, MALLOC_CAP_DMA));
  lv_display_t* disp = lv_tft_espi_create(TFT_WIDTH, TFT_HEIGHT, drawBuf, kDrawBufBytes);
  lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_270);
  ui.begin();

  net.begin();
  Serial.printf("# ready, free heap %u\n", (unsigned)ESP.getFreeHeap());
}

void loop() {
  uint32_t now = millis();
  pumpUart(nanoSerial, nanoLines, false);
  pumpUart(gpsSerial, gpsLines, true);
  pumpConsole();
  pollTouch(now);
  net.loop(monitor, now);
  if (now - lastTickMs >= 1000) {
    lastTickMs = now;
    monitor.tick(now);
    UiExtras x;
    net.fill(x);
    ui.update(monitor, x, now);
  }
  lv_timer_handler();
  delay(5);
}
