// PC simulator: replays a captured GPSDO report (and optional NMEA) through the real monitor and
// screens, then writes each screen as a PPM image. Lets the look be checked without hardware.
//
// usage: sim <arduino-capture> <out-prefix> [--nmea <nmea-capture>] [--fake-gps] [--seconds N]
//            [--unlock-at N]
// Capture lines are "<ISO UTC time> <line>", as written by tools/capture_serial.py.
// --fake-gps synthesises a plausible sky (clearly not real data) to exercise the GPS screens.
// --unlock-at rewrites the status to "****" from the Nth report line, to preview the unlocked look.
#include <lvgl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "monitor.h"
#include "ui.h"

using namespace gpsdo;

static uint32_t gNowMs = 0;
static uint16_t gFb[320 * 240];
static uint8_t gBuf[320 * 240 * 2];

static uint32_t tickCb() { return gNowMs; }

static void flushCb(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
  const uint16_t* p = reinterpret_cast<const uint16_t*>(px);
  for (int y = a->y1; y <= a->y2; ++y)
    for (int x = a->x1; x <= a->x2; ++x) gFb[y * 320 + x] = *p++;
  lv_display_flush_ready(d);
}

static void writePpm(const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "wb");
  std::fprintf(f, "P6\n320 240\n255\n");
  for (uint16_t c : gFb) {
    uint8_t r = (c >> 11) & 0x1f, g = (c >> 5) & 0x3f, b = c & 0x1f;
    uint8_t rgb[3] = {(uint8_t)(r * 255 / 31), (uint8_t)(g * 255 / 63), (uint8_t)(b * 255 / 31)};
    std::fwrite(rgb, 1, 3, f);
  }
  std::fclose(f);
}

static uint32_t isoToMs(const std::string& s) {
  // 2026-11-08T13:17:40.2387 -> ms of day
  int h, m;
  double sec;
  if (std::sscanf(s.c_str() + 11, "%d:%d:%lf", &h, &m, &sec) != 3) return 0;
  return (uint32_t)((h * 3600 + m * 60) * 1000 + sec * 1000);
}

static std::string withChecksum(const std::string& body) {
  uint8_t x = 0;
  for (char c : body) x ^= (uint8_t)c;
  char b[8];
  std::snprintf(b, sizeof b, "*%02X", x);
  return "$" + body + b;
}

struct FakeSat { const char* talker; int prn; int el; int az; int snr; bool used; };
static const FakeSat kFake[] = {
    {"GP", 5, 62, 290, 46, true},  {"GP", 13, 34, 75, 41, true},  {"GP", 15, 18, 140, 33, true},
    {"GP", 18, 71, 120, 45, true}, {"GP", 20, 8, 320, 22, false}, {"GP", 23, 41, 210, 39, true},
    {"GP", 24, 12, 35, 27, true},  {"GP", 29, 5, 180, -1, false}, {"GP", 46, 28, 165, 37, false},
    {"GA", 2, 55, 250, 44, true},  {"GA", 8, 22, 300, 34, true},  {"GA", 25, 38, 95, 40, true},
    {"GA", 34, 10, 20, 24, false}, {"GL", 70, 47, 60, 38, true},  {"GL", 71, 15, 230, 29, false},
    {"GL", 85, 66, 330, 42, true},
};

static void fakeGps(Monitor& m, uint32_t nowMs, int hh, int mm, int ss) {
  char b[160];
  std::snprintf(b, sizeof b, "GNRMC,%02d%02d%02d.00,A,5200.00000,N,00100.00000,W,0.01,,081126,,,A,V", hh, mm, ss);
  m.onGpsLine(withChecksum(b), nowMs);
  std::snprintf(b, sizeof b, "GNGGA,%02d%02d%02d.00,5200.00000,N,00100.00000,W,1,12,0.78,52.3,M,47.0,M,,", hh, mm, ss);
  m.onGpsLine(withChecksum(b), nowMs);
  for (const char* t : {"GP", "GA", "GL"}) {
    std::string body = std::string(t) + "GSV,1,1,00";
    std::string gsa = "GNGSA,A,3";
    int used = 0;
    for (const auto& s : kFake) {
      if (std::strcmp(s.talker, t)) continue;
      char g[32];
      // Wobble the SNR a little so a replay looks alive.
      int snr = s.snr < 0 ? -1 : s.snr + (int)((nowMs / 1000 + s.prn) % 3) - 1;
      if (snr < 0) std::snprintf(g, sizeof g, ",%02d,%02d,%03d,", s.prn, s.el, s.az);
      else std::snprintf(g, sizeof g, ",%02d,%02d,%03d,%02d", s.prn, s.el, s.az, snr);
      body += g;
      if (s.used) { gsa += ","; gsa += std::to_string(s.prn); ++used; }
    }
    for (; used < 12; ++used) gsa += ",";
    const char* sys = !std::strcmp(t, "GP") ? "1" : !std::strcmp(t, "GL") ? "2" : "3";
    gsa += ",1.10,0.78,0.77,";
    gsa += sys;
    m.onGpsLine(withChecksum(body), nowMs);
    m.onGpsLine(withChecksum(gsa), nowMs);
  }
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: sim <arduino-capture> <out-prefix> [--nmea F] [--fake-gps] [--seconds N] [--unlock-at N]\n");
    return 2;
  }
  std::string cap = argv[1], out = argv[2], nmeaPath;
  bool fake = false;
  long seconds = -1, unlockAt = -1;
  for (int i = 3; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--fake-gps")) fake = true;
    else if (!std::strcmp(argv[i], "--nmea") && i + 1 < argc) nmeaPath = argv[++i];
    else if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = std::atol(argv[++i]);
    else if (!std::strcmp(argv[i], "--unlock-at") && i + 1 < argc) unlockAt = std::atol(argv[++i]);
  }

  lv_init();
  lv_tick_set_cb(tickCb);
  lv_display_t* disp = lv_display_create(320, 240);
  lv_display_set_flush_cb(disp, flushCb);
  lv_display_set_buffers(disp, gBuf, nullptr, sizeof gBuf, LV_DISPLAY_RENDER_MODE_FULL);

  Monitor mon;
  Ui ui;
  ui.begin();

  struct Ev { uint32_t ms; bool gps; std::string line; };
  std::vector<Ev> evs;
  auto load = [&](const std::string& path, bool gps) {
    std::ifstream in(path);
    std::string l;
    while (std::getline(in, l)) {
      if (l.empty() || l[0] == '#') continue;
      size_t sp = l.find(' ');
      if (sp == std::string::npos) continue;
      evs.push_back({isoToMs(l.substr(0, sp)), gps, l.substr(sp + 1)});
    }
  };
  load(cap, false);
  if (!nmeaPath.empty()) load(nmeaPath, true);
  std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) { return a.ms < b.ms; });
  if (evs.empty()) { std::fprintf(stderr, "no lines in capture\n"); return 1; }

  uint32_t t0 = evs.front().ms;
  uint32_t lastSec = 0;
  long dataLines = 0;
  for (const Ev& e : evs) {
    gNowMs = e.ms - t0 + 1000;
    if (seconds >= 0 && gNowMs > (uint32_t)seconds * 1000) break;
    if (e.gps) { mon.onGpsLine(e.line, gNowMs); continue; }
    std::string line = e.line;
    ParsedLine p = parseReportLine(line);
    if (p.kind == LineKind::Data) {
      ++dataLines;
      if (unlockAt >= 0 && dataLines >= unlockAt) {
        size_t pos = line.find("\tLocked\t");
        if (pos != std::string::npos) line.replace(pos, 8, "\t****\t");
      }
    }
    if (fake && gNowMs / 1000 != lastSec) {
      lastSec = gNowMs / 1000;
      uint32_t s = e.ms / 1000;
      fakeGps(mon, gNowMs, (int)(s / 3600) % 24, (int)(s / 60) % 60, (int)s % 60);
    }
    mon.onNanoLine(line, gNowMs);
  }
  gNowMs += 200;
  mon.tick(gNowMs);
  UiExtras x;
  x.wifiEnabled = true;
  x.wifiConnected = true;
  x.mqttConnected = true;
  // Example network details (documentation addresses, not real ones).
  x.ssid = "example-wifi";
  x.ip = "192.0.2.41";
  x.mac = "48:AF:F3:00:00:00";
  x.rssi = -61;
  x.channel = 6;
  x.mqttHost = "homeassistant.local";
  x.mqttIp = "192.0.2.10";
  x.mqttPort = 1883;
  x.deviceId = "gpsdo_cyd_000000";
  x.publishes = 152;
  x.lastPublishAgeS = 4;
  x.mqttUpS = 1520;
  for (int s = 0; s < Ui::kScreens; ++s) {
    ui.show(s);
    ui.update(mon, x, gNowMs);
    ui.fastUpdate(mon, (int64_t)gNowMs * 1000);
    lv_refr_now(disp);
    char path[512];
    std::snprintf(path, sizeof path, "%s-%d.ppm", out.c_str(), s + 1);
    writePpm(path);
    std::printf("%s\n", path);
  }
  return 0;
}
