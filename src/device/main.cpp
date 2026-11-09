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
#include <Preferences.h>
#include <XPT2046_Touchscreen.h>
#include <esp_timer.h>
#include <lvgl.h>

#include "backlight.h"
#include "config.h"
#include "line_assembler.h"
#include "age.h"
#include "monitor.h"
#include "net.h"
#include "ntp_server.h"
#include "portal.h"
#include "settings.h"
#include "watchdog.h"
#include "web.h"
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

// Event log persistence: saved to flash a few seconds after it changes.
uint32_t savedChanges = 0, lastSaveMs = 0;
void loadEvents() {
  Preferences p;
  p.begin("gpsdo", true);
  static uint8_t blob[EventLog::kBlobSize];
  size_t n = p.getBytes("events", blob, sizeof blob);
  p.end();
  if (n == sizeof blob && monitor.events().fromBlob(blob, n)) Serial.printf("# event log restored, %d entries\n", monitor.events().count());
  savedChanges = monitor.events().changes();
}
void saveEventsIfChanged(uint32_t now) {
  if (monitor.events().changes() == savedChanges) return;
  // Save soon after a serious event, otherwise at most once a minute, to spare the flash.
  bool serious = monitor.events().count() && monitor.events().newest(0).level == 2;
  if (ageMs(now, lastSaveMs) < (serious ? 10000u : 60000u)) return;
  lastSaveMs = now;
  savedChanges = monitor.events().changes();
  static uint8_t blob[EventLog::kBlobSize];
  monitor.events().toBlob(blob);
  Preferences p;
  p.begin("gpsdo", false);
  p.putBytes("events", blob, sizeof blob);
  p.end();
}

// Sky map and the learned antenna position: saved hourly, restored at start-up. (The position stays
// in the device's own flash; it is never published.)
uint32_t lastLongSaveMs = 0;
uint32_t savedRecordings = 0;
std::string savedAntenna;  // last antenna report from the GPS, kept across display restarts
void loadLongTerm() {
  Preferences p;
  p.begin("gpsdo", true);
  static uint8_t blob[SkyMap::kBlobSize];
  if (p.getBytes("skymap", blob, sizeof blob) == sizeof blob) monitor.skyMap().fromBlob(blob, sizeof blob);
  double pos[3];
  if (p.getBytes("antpos", pos, sizeof pos) == sizeof pos) monitor.watch().restore(pos[0], pos[1], pos[2]);
  static uint8_t rec[FlightRecorder::kBlobSize];
  if (p.getBytes("flightrec", rec, sizeof rec) == sizeof rec) monitor.recorder().restore(rec, sizeof rec);
  savedAntenna = p.getString("antenna", "").c_str();
  if (!savedAntenna.empty()) monitor.restoreAntenna(savedAntenna);
  p.end();
  savedRecordings = monitor.recorder().finished();
  Serial.printf("# sky map restored, %u readings; antenna position %s\n", (unsigned)monitor.skyMap().samples(),
                monitor.watch().positionLearned() ? "known" : "still to learn (30 min of fixes)");
}
// The newest flight recording is saved once, as soon as it is complete.
void saveRecordingIfNew() {
  if (monitor.recorder().finished() == savedRecordings) return;
  savedRecordings = monitor.recorder().finished();
  const FrRecording& r = monitor.recorder().recording(0);
  Preferences p;
  p.begin("gpsdo", false);
  size_t n = p.putBytes("flightrec", &r, sizeof r);
  p.end();
  Serial.printf("# flight recording saved (%s, %u s)%s\n", r.reason, (unsigned)r.count, n ? "" : " FAILED: flash full?");
}

// Save the antenna report when it changes (rare: the GPS sends it at its start and on a change).
void saveAntennaIfNew() {
  const std::string& a = monitor.gps().antennaStatus();
  if (a.empty() || a == savedAntenna) return;
  savedAntenna = a;
  Preferences p;
  p.begin("gpsdo", false);
  p.putString("antenna", a.c_str());
  p.end();
}

void saveLongTerm(uint32_t now) {
  if (ageMs(now, lastLongSaveMs) < 3600000UL) return;
  lastLongSaveMs = now;
  Preferences p;
  p.begin("gpsdo", false);
  static uint8_t blob[SkyMap::kBlobSize];
  monitor.skyMap().toBlob(blob);
  p.putBytes("skymap", blob, sizeof blob);
  double pos[3];
  if (monitor.watch().learned(pos[0], pos[1], pos[2])) p.putBytes("antpos", pos, sizeof pos);
  p.end();
}

// Beep when the 10 MHz verdict changes (not at start-up).
int lastVerdict = -1;  // 0 unreliable, 1 ok
void beepOnVerdict() {
  if (monitor.headline() == Headline::NoData && lastVerdict < 0) return;
  int v = monitor.referenceOk() ? 1 : 0;
  if (lastVerdict >= 0 && v != lastVerdict) {
    if (v) Beeper::good();
    else Beeper::bad();
  }
  lastVerdict = v;
}
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

// A receive buffer overflow loses bytes mid-line; the next complete line may be two half-lines
// joined together, so it is dropped.
volatile bool nanoOverflow = false, gpsOverflow = false;
void onNanoRxError(hardwareSerial_error_t e) {
  if (e == UART_BUFFER_FULL_ERROR || e == UART_FIFO_OVF_ERROR) nanoOverflow = true;
}
void onGpsRxError(hardwareSerial_error_t e) {
  if (e == UART_BUFFER_FULL_ERROR || e == UART_FIFO_OVF_ERROR) gpsOverflow = true;
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
int64_t lastPpsFedUs = 0, lastPpsSeenUs = 0;
int64_t delaySum = 0;
int delayCount = 0;
uint32_t lastDelayReportMs = 0, lastHeapLogMs = 0;

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
    volatile bool& overflow = gps ? gpsOverflow : nanoOverflow;
    if (overflow || lines.truncated()) {
      overflow = false;
      Serial.println("# dropped a line after a receive overflow");
      if (gps) gpsInLine = false;
      continue;
    }
    // Data read out of a backlog (the loop was held up for a third of a second or more: more than
    // 300 bytes still waiting at 9600 baud) has no useful arrival time, so it gives no timing.
    bool backlog = port.available() > 300;
    if (gps) {
      monitor.onGpsLine(l, now, backlog ? -1 : gpsLineStartUs);
      gpsInLine = false;
    } else {
      int64_t start = backlog ? -1 : nanoStartUs();
      if (start >= 0) judgePps(start);
      monitor.onNanoLine(l, now, start);
    }
  }
}

void pumpConsole() {
  for (int n = 0; n < 512 && Serial.available(); ++n) {
    if (!consoleLines.push((char)Serial.read())) continue;
    const std::string& l = consoleLines.line();
    if (l == "setup" && !portal.active()) { portal.begin(settings); continue; }
    if (l == "test watchdog") { Serial.println("# hanging the loop to test the watchdog"); for (;;) delay(1000); }
    if (l == "heap") {
      Serial.printf("# heap: free %u, largest block %u, lowest free %u\n", (unsigned)ESP.getFreeHeap(),
                    (unsigned)ESP.getMaxAllocHeap(), (unsigned)ESP.getMinFreeHeap());
      continue;
    }
    if (l == "forget position") {  // after moving the antenna: learn its new position from scratch
      monitor.watch() = InterferenceWatch();
      Preferences p;
      p.begin("gpsdo", false);
      p.remove("antpos");
      p.end();
      Serial.println("# antenna position forgotten, relearning over 30 minutes of fixes");
      continue;
    }
    if (l == "clear events") { monitor.events() = EventLog(); Serial.println("# event log cleared"); continue; }
    if (l.size() < 2 || l[1] != ' ') continue;
    if (l[0] == 'N') monitor.onNanoLine(l.substr(2), millis());
    else if (l[0] == 'G') monitor.onGpsLine(l.substr(2), millis());
    else if (l[0] == 'S') {
      UiExtras x;
      net.fill(x);
      ui.switchTo((l[2] - '1') % Ui::kScreens, monitor, x, millis(), esp_timer_get_time());
    }
  }
}

void pollTouch(uint32_t now) {
  // Any tap moves to the next screen, so no touch calibration is needed.
  bool t = touch.tirqTouched() && touch.touched();
  if (t && !wasTouched && now - lastTouchMs > 250) {
    lastTouchMs = now;
    UiExtras x;
    net.fill(x);
    ui.switchTo((ui.current() + 1) % Ui::kScreens, monitor, x, now, esp_timer_get_time());
  }
  wasTouched = t;
}

}  // namespace

void setup() {
  Serial.begin(CONSOLE_BAUD);
  Serial.println();
  Serial.println("# gpsdo-cyd starting");
  Watchdog::begin();

  // Modest buffers: lines that pile up while the loop is held up are dropped anyway (no timing,
  // overflow detection), and heap is precious once Wi-Fi traffic arrives.
  nanoSerial.setRxBufferSize(1024);
  nanoSerial.begin(NANO_BAUD, SERIAL_8N1, NANO_RX_PIN, -1);
  nanoSerial.setRxFIFOFull(1);
  nanoSerial.onReceive(onNanoRx, false);
  nanoSerial.onReceiveError(onNanoRxError);
  pinMode(PPS_PIN, INPUT_PULLDOWN);  // quiet when nothing is wired to it
  attachInterrupt(digitalPinToInterrupt(PPS_PIN), onPpsEdge, CHANGE);
  gpsSerial.setRxBufferSize(2048);
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, -1);
  gpsSerial.onReceiveError(onGpsRxError);

  touchSpi.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  touch.begin(touchSpi);

  Serial.printf("# heap before screens %u\n", (unsigned)ESP.getFreeHeap());
  lv_init();
  lv_tick_set_cb(tickCb);
  drawBuf = static_cast<uint8_t*>(heap_caps_malloc(kDrawBufBytes, MALLOC_CAP_DMA));
  lv_display_t* disp = lv_tft_espi_create(TFT_WIDTH, TFT_HEIGHT, drawBuf, kDrawBufBytes);
  lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);  // matches the PCB silkscreen orientation
  ui.begin();
  Serial.printf("# heap after screens %u\n", (unsigned)ESP.getFreeHeap());

  Backlight::begin();
  Beeper::begin();
  loadEvents();
  loadLongTerm();
  lastLongSaveMs = millis();
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
    Web::begin(monitor, net);
  }
  Serial.printf("# ready, free heap %u, largest block %u\n", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
}

bool resetReasonLogged = false;

void loop() {
  Watchdog::feed();
  pumpUart(nanoSerial, nanoLines, false);
  pumpUart(gpsSerial, gpsLines, true);
  pumpConsole();
  // Read the clock after handling input, so nothing processed above is timestamped later than now.
  uint32_t now = millis();
  if (portal.active()) {
    if (!portal.loop(now)) ui.show(Ui::kStatus);  // timed out: back to normal, Wi-Fi off
  } else {
    pollTouch(now);
    net.loop(monitor, now);
    Web::loop();
  }
  Backlight::loop(now);
  Beeper::loop(now);
  if (now - lastTickMs >= 1000) {
    lastTickMs = now;
    monitor.tick(now);
    // Log an unexpected restart once the log has a UTC time to put on it (or after a minute).
    if (!resetReasonLogged && Watchdog::lastResetReason() && (monitor.events().count() > 0 || now > 60000)) {
      monitor.logEvent(now, 2, Watchdog::lastResetReason());
      resetReasonLogged = true;
    }
    beepOnVerdict();
    saveEventsIfChanged(now);
    saveLongTerm(now);
    saveRecordingIfNew();
    saveAntennaIfNew();
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
    if (e != lastPpsSeenUs) {
      lastPpsSeenUs = e;
      // Only a whole number of seconds (+-2 ms) after the last good edge counts; anything else is
      // noise (the input sits next to a transmitter). Ten seconds without a good edge: forget it.
      int64_t gap = e - lastPpsFedUs;
      int64_t off = (gap + 500000) % 1000000 - 500000;
      if (!lastPpsFedUs || (gap > 500000 && off > -2000 && off < 2000)) {
        lastPpsFedUs = e;
        monitor.onPpsEdge(e);
      }
    }
    if (lastPpsFedUs && esp_timer_get_time() - lastPpsFedUs > 10000000) {
      Serial.println("# PPS input lost, back to the Nano's report for the second mark");
      ppsEdgeKind = 0;
      ppsVotes = 0;
      lastPpsFedUs = 0;
    }
  }
  if (ageMs(now, lastHeapLogMs) >= 60000) {
    lastHeapLogMs = now;
    Serial.printf("# heap: free %u, largest block %u, lowest free %u\n", (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMaxAllocHeap(), (unsigned)ESP.getMinFreeHeap());
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
    // A freshly started u-blox may be whole seconds out until it has learned the leap-second count
    // (up to 12.5 minutes), so time is not served as synced for 13 minutes after a GPS restart.
    bool settled = !monitor.gpsRestarts() || ageMs(now, monitor.lastRestartMs()) > 13UL * 60000;
    NtpServer::setClock(nowUs, unixUs, aligned && gpsGood && settled, ppsWired ? 200 : 5000);
  } else {
    NtpServer::setUnsynced();
  }
  if (now - lastFastMs >= 50) {
    lastFastMs = now;
    ui.fastUpdate(monitor, nowUs);
  }
  lv_timer_handler();
  delay(5);
}
