#include "web_json.h"

#include <cstdio>

namespace gpsdo {
namespace {
void appendEscaped(std::string& o, const char* s) {
  o += '"';
  for (; *s; ++s) {
    char c = *s;
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((unsigned char)c < 0x20) o += ' ';
    else o += c;
  }
  o += '"';
}
}  // namespace

void eventsJson(const Monitor& m, std::string& s) {
  const EventLog& log = m.events();
  s = "[";
  char b[96];
  for (int i = 0; i < log.count(); ++i) {
    const Event& e = log.newest(i);
    std::snprintf(b, sizeof b, "%s{\"t\":%lld,\"up\":%u,\"level\":%u,\"text\":", i ? "," : "", (long long)e.unixSec,
                  (unsigned)e.upS, (unsigned)e.level);
    s += b;
    appendEscaped(s, e.text);
    s += "}";
  }
  s += "]";
}

void trendsJson(const Monitor& m, std::string& s) {
  s = "{\"step_s\":";
  s += std::to_string(Monitor::kTrendStepS);
  s += ",\"points\":[";
  char b[64];
  for (size_t i = 0; i < m.trendCount(); ++i) {
    const TrendPoint& t = m.trend(i);
    if (t.valid)
      std::snprintf(b, sizeof b, "%s[%d,%d,%u,%d]", i ? "," : "", t.ns, t.diffNs, (unsigned)t.dac,
                    t.lockedAll ? 2 : t.lockedAny ? 1 : 0);
    else
      std::snprintf(b, sizeof b, "%snull", i ? "," : "");
    s += b;
  }
  s += "]}";
}

void historyJson(const Monitor& m, std::string& s) {
  const NanoHistory& h = m.history();
  s = "{";
  char b[80];
  for (int pass = 0; pass < 2; ++pass) {
    bool five = pass == 0;
    s += five ? "\"five_min\":[" : ",\"three_hour\":[";
    bool first = true;
    h.forEach(five, [&](const HistoryEntry& e) {
      std::snprintf(b, sizeof b, "%s[%.1f,%u,%.1f,\"%c\"]", first ? "" : ",",
                    e.ticX10 / 10.0 - GpsdoConstants::kTicOffsetNs, (unsigned)e.dac, e.tempC, e.lock);
      s += b;
      first = false;
    });
    s += "]";
  }
  std::snprintf(b, sizeof b, ",\"seen_five_min\":%d,\"seen_three_hour\":%d}", h.seen(true), h.seen(false));
  s += b;
}

void satsJson(const Monitor& m, uint32_t nowMs, std::string& s) {
  const NmeaParser& g = m.gps();
  s = "[";
  char b[120];
  bool first = true;
  for (const auto& sat : g.satellites()) {
    if (!sat.valid || sat.elevation < 0) continue;
    std::snprintf(b, sizeof b, "%s{\"id\":\"%s%02u\",\"el\":%d,\"az\":%d,\"snr\":%d,\"used\":%s}", first ? "" : ",",
                  constellationPrefix(sat.sys), (unsigned)sat.prn, sat.elevation, sat.azimuth, sat.snr,
                  g.isUsed(sat, nowMs) ? "true" : "false");
    s += b;
    first = false;
  }
  s += "]";
}

void accuracyJson(const Monitor& m, std::string& s) {
  s = "{";
  const uint32_t w[3] = {60, 300, 1800};
  char b[48];
  for (int i = 0; i < 3; ++i) {
    float ppb;
    if (m.frequencyPpb(w[i], ppb)) std::snprintf(b, sizeof b, "%s\"%u\":%.4f", i ? "," : "", (unsigned)w[i], ppb);
    else std::snprintf(b, sizeof b, "%s\"%u\":null", i ? "," : "", (unsigned)w[i]);
    s += b;
  }
  s += "}";
}

}  // namespace gpsdo
