#include "web_json.h"

#include <cstdio>

#include "age.h"
#include "locator.h"
#include "tempfit.h"

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
  s = "{\"sats\":[";
  char b[140];
  bool first = true;
  unsigned seen[7] = {}, used[7] = {}, inView = 0, inFix = 0;
  for (const auto& sat : g.satellites()) {
    if (!sat.valid || sat.elevation < 0) continue;
    bool u = g.isUsed(sat, nowMs);
    ++seen[(int)sat.sys];
    ++inView;
    if (u) { ++used[(int)sat.sys]; ++inFix; }
    std::snprintf(b, sizeof b, "%s{\"id\":\"%s%02u\",\"el\":%d,\"az\":%d,\"snr\":%d,\"used\":%s}", first ? "" : ",",
                  constellationPrefix(sat.sys), (unsigned)sat.prn, sat.elevation, sat.azimuth, sat.snr, u ? "true" : "false");
    s += b;
    first = false;
  }
  std::snprintf(b, sizeof b, "],\"in_view\":%u,\"in_fix\":%u,\"constellations\":{", inView, inFix);
  s += b;
  static const char* names[7] = {"Other", "GPS", "SBAS", "GLONASS", "Galileo", "BeiDou", "QZSS"};
  first = true;
  for (int i = 0; i < 7; ++i) {
    if (!seen[i]) continue;
    std::snprintf(b, sizeof b, "%s\"%s\":[%u,%u]", first ? "" : ",", names[i], used[i], seen[i]);
    s += b;
    first = false;
  }
  s += "}";
  if (!g.antennaStatus().empty()) {
    s += ",\"antenna\":";
    appendEscaped(s, g.antennaStatus().c_str());
  }
  if (m.top4Snr() >= 0) {
    std::snprintf(b, sizeof b, ",\"best4_snr\":%d", m.top4Snr());
    s += b;
  }
  int lo, hi;
  if (m.top4Range(lo, hi)) {
    std::snprintf(b, sizeof b, ",\"best4_24h\":[%d,%d]", lo, hi);
    s += b;
  }
  s += "}";
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

namespace gpsdo {

void timeJson(const Monitor& m, int64_t nowUs, uint32_t nowMs, std::string& s) {
  char b[200];
  int64_t u;
  bool pps;
  s = "{";
  if (m.clock().now(nowUs, u, pps)) {
    std::snprintf(b, sizeof b, "\"utc_ms\":%lld,\"pps_aligned\":%s", (long long)(u / 1000), pps ? "true" : "false");
    s += b;
  } else {
    s += "\"utc_ms\":null";
  }
  const NmeaParser& g = m.gps();
  const Fix& f = g.fix();
  bool fresh = m.gpsEverSeen() && ageMs(nowMs, g.lastGoodMs()) <= Monitor::kSilentMs;
  if (fresh && f.haveLatLon) {
    std::snprintf(b, sizeof b, ",\"lat\":%.6f,\"lon\":%.6f,\"locator\":\"%s\"", f.latDeg, f.lonDeg,
                  maidenhead(f.latDeg, f.lonDeg, 8).c_str());
    s += b;
    if (f.haveAltitude) {
      std::snprintf(b, sizeof b, ",\"alt_m\":%.1f", f.altitudeM);
      s += b;
    }
  }
  std::snprintf(b, sizeof b, ",\"fix\":\"%s\",\"sats_used\":%u}", f.mode == 3 ? "3D" : f.mode == 2 ? "2D" : "none",
                (unsigned)f.satsUsed);
  s += b;
}

}  // namespace gpsdo

namespace gpsdo {

void stabilityJson(const Monitor& m, int gain, std::string& s) {
  char b[160];
  std::snprintf(b, sizeof b, "{\"samples\":%u,\"adev\":[", (unsigned)m.stability().samples());
  s = b;
  bool first = true;
  for (uint32_t tau : Stability::kTau) {
    double a;
    if (!m.stability().adev(tau, a)) continue;
    std::snprintf(b, sizeof b, "%s[%u,%.3e]", first ? "" : ",", (unsigned)tau, a);
    s += b;
    first = false;
  }
  s += "]";
  TempFit f = fitTemperature(m.history(), gain);
  if (f.points) {
    std::snprintf(b, sizeof b,
                  ",\"temperature\":{\"points\":%d,\"slope_dac_per_c\":%.2f,\"ppb_per_c\":%.4f,\"r2\":%.2f,"
                  "\"mean_c\":%.2f,\"span_c\":%.2f,\"temp_ref\":%ld,\"temp_coeff\":%ld,\"valid\":%s}",
                  f.points, f.slopeDacPerC, f.ppbPerC, f.r2, f.meanTempC, f.tempSpanC, f.suggestedTempRef,
                  f.suggestedTempCoeff, f.valid ? "true" : "false");
    s += b;
  }
  s += "}";
}

void skyMapJson(const Monitor& m, std::string& s) {
  const SkyMap& k = m.skyMap();
  char b[48];
  std::snprintf(b, sizeof b, "{\"samples\":%u,\"cells\":[", (unsigned)k.samples());
  s = b;
  bool first = true;
  for (int e = 0; e < SkyMap::kEl; ++e)
    for (int a = 0; a < SkyMap::kAz; ++a) {
      float v = k.mean(e, a);
      if (v < 0) continue;
      std::snprintf(b, sizeof b, "%s[%d,%d,%.1f,%u]", first ? "" : ",", e, a, v, (unsigned)k.weight(e, a));
      s += b;
      first = false;
    }
  s += "]}";
}

}  // namespace gpsdo

namespace gpsdo {

void recordingsJson(const Monitor& m, std::string& s) {
  const FlightRecorder& r = m.recorder();
  char b[120];
  s = "{\"recording_now\":";
  s += r.recording() ? "true" : "false";
  s += ",\"recordings\":[";
  for (int i = 0; i < r.count(); ++i) {
    const FrRecording& x = r.recording(i);
    std::snprintf(b, sizeof b, "%s{\"index\":%d,\"t\":%u,\"seconds\":%u,\"trigger_index\":%u,\"reason\":", i ? "," : "", i,
                  (unsigned)x.triggerUnixSec, (unsigned)x.count, (unsigned)x.triggerIndex);
    s += b;
    appendEscaped(s, x.reason);
    s += "}";
  }
  s += "]}";
}

void recordingJsonStream(const Monitor& m, int index, const std::function<void(const std::string&)>& emit) {
  const FlightRecorder& r = m.recorder();
  if (index < 0 || index >= r.count()) { emit("null"); return; }
  const FrRecording& x = r.recording(index);
  char b[96];
  std::string s;
  std::snprintf(b, sizeof b, "{\"t\":%u,\"trigger_index\":%u,\"reason\":", (unsigned)x.triggerUnixSec, (unsigned)x.triggerIndex);
  s = b;
  appendEscaped(s, x.reason);
  s += ",\"samples\":[";
  emit(s);
  for (int i = 0; i < x.count; ++i) {
    const FrSample& q = x.s[i];
    std::snprintf(b, sizeof b, "%s[%u,%u,%u,%d,%u,[", i ? "," : "", (unsigned)q.unixSec, q.used, q.quality, q.best4, q.pps);
    s = b;
    for (int k = 0; k < q.n && k < 20; ++k) {
      std::snprintf(b, sizeof b, "%s[\"%c%02u\",%d,%u]", k ? "," : "", (char)(q.sats[k].id >> 8), (unsigned)(q.sats[k].id & 0xFF),
                    q.sats[k].snr, q.sats[k].used);
      s += b;
    }
    s += "]]";
    emit(s);
  }
  emit("]}");
}

}  // namespace gpsdo
