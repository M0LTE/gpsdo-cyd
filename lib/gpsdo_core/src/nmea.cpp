#include "nmea.h"

#include <cstdlib>

namespace gpsdo {
namespace {

constexpr size_t kMaxFields = 24;

int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

bool toInt(const std::string& s, int& v) {
  if (s.empty()) return false;
  char* end = nullptr;
  long x = std::strtol(s.c_str(), &end, 10);
  if (*end != '\0') return false;
  v = (int)x;
  return true;
}

bool toFloat(const std::string& s, float& v) {
  if (s.empty()) return false;
  char* end = nullptr;
  v = std::strtof(s.c_str(), &end);
  return *end == '\0';
}

int two(const std::string& s, size_t at) {
  if (s.size() < at + 2) return -1;
  int a = s[at] - '0', b = s[at + 1] - '0';
  if (a < 0 || a > 9 || b < 0 || b > 9) return -1;
  return a * 10 + b;
}

}  // namespace

const char* constellationPrefix(Constellation c) {
  switch (c) {
    case Constellation::Gps: return "G";
    case Constellation::Sbas: return "S";
    case Constellation::Glonass: return "R";
    case Constellation::Galileo: return "E";
    case Constellation::BeiDou: return "C";
    case Constellation::Qzss: return "J";
    default: return "?";
  }
}

Constellation constellationFromTalker(const std::string& t) {
  if (t == "GP") return Constellation::Gps;
  if (t == "GL") return Constellation::Glonass;
  if (t == "GA") return Constellation::Galileo;
  if (t == "GB" || t == "BD") return Constellation::BeiDou;
  if (t == "GQ" || t == "QZ") return Constellation::Qzss;
  return Constellation::Unknown;
}

Constellation constellationFromSystemId(int id) {
  switch (id) {
    case 1: return Constellation::Gps;
    case 2: return Constellation::Glonass;
    case 3: return Constellation::Galileo;
    case 4: return Constellation::BeiDou;
    case 5: return Constellation::Qzss;
    default: return Constellation::Unknown;
  }
}

Constellation constellationFromUbloxPrn(int prn, uint16_t& sv) {
  sv = (uint16_t)prn;
  if (prn >= 1 && prn <= 32) return Constellation::Gps;
  if (prn >= 33 && prn <= 64) { sv = (uint16_t)(prn + 87); return Constellation::Sbas; }  // S120..S151
  if (prn >= 65 && prn <= 96) { sv = (uint16_t)(prn - 64); return Constellation::Glonass; }
  if (prn >= 152 && prn <= 158) return Constellation::Sbas;
  if (prn >= 193 && prn <= 202) { sv = (uint16_t)(prn - 192); return Constellation::Qzss; }
  if (prn >= 211 && prn <= 246) { sv = (uint16_t)(prn - 210); return Constellation::Galileo; }
  if (prn >= 301 && prn <= 336) { sv = (uint16_t)(prn - 300); return Constellation::Galileo; }
  if (prn >= 159 && prn <= 163) { sv = (uint16_t)(prn - 158); return Constellation::BeiDou; }
  if (prn >= 401 && prn <= 437) { sv = (uint16_t)(prn - 400); return Constellation::BeiDou; }
  return Constellation::Unknown;
}

bool NmeaParser::checksumOk(const std::string& s) {
  if (s.size() < 4 || s[0] != '$') return false;
  size_t star = s.rfind('*');
  if (star == std::string::npos || star + 3 > s.size()) return false;
  uint8_t sum = 0;
  for (size_t i = 1; i < star; ++i) sum ^= (uint8_t)s[i];
  int hi = hexVal(s[star + 1]), lo = hexVal(s[star + 2]);
  if (hi < 0 || lo < 0) return false;
  return sum == (uint8_t)(hi * 16 + lo);
}

bool NmeaParser::parse(const std::string& raw, uint32_t nowMs) {
  std::string s = raw;
  while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
  if (!checksumOk(s)) {
    if (!s.empty() && s[0] == '$') ++badChecksum_;
    return false;
  }
  std::string body = s.substr(1, s.rfind('*') - 1);
  std::string f[kMaxFields];
  size_t n = 0, start = 0;
  for (;;) {
    size_t c = body.find(',', start);
    if (n == kMaxFields) break;
    f[n++] = body.substr(start, c == std::string::npos ? std::string::npos : c - start);
    if (c == std::string::npos) break;
    start = c + 1;
  }
  if (f[0].size() != 5) return false;
  std::string talker = f[0].substr(0, 2), type = f[0].substr(2);
  ++good_;
  lastGoodMs_ = nowMs;
  if (type == "GGA") parseGga(f, n, nowMs);
  else if (type == "GSA") parseGsa(talker, f, n, nowMs);
  else if (type == "GSV") parseGsv(talker, f, n, nowMs);
  else if (type == "RMC") parseRmc(f, n, nowMs);
  else if (type == "ZDA") parseZda(f, n, nowMs);
  else if (type == "TXT") {
    if (n > 4) {
      lastTxt_ = f[4];
      if (lastTxt_.rfind("ANTSTATUS=", 0) == 0) antenna_ = lastTxt_.substr(10);
    }
  }
  return true;
}

bool NmeaParser::parseTime(const std::string& t) {
  int h = two(t, 0), m = two(t, 2), sec = two(t, 4);
  if (h < 0 || m < 0 || sec < 0) return false;
  utc_.hour = (uint8_t)h;
  utc_.minute = (uint8_t)m;
  utc_.second = (uint8_t)sec;
  return true;
}

void NmeaParser::parseGga(const std::string* f, size_t n, uint32_t nowMs) {
  if (n < 10) return;
  int q = 0, used = 0;
  float x;
  fix_.quality = toInt(f[6], q) ? (uint8_t)q : 0;
  fix_.satsUsed = toInt(f[7], used) ? (uint8_t)used : 0;
  if (toFloat(f[8], x)) fix_.hdop = x;
  fix_.haveAltitude = toFloat(f[9], x);
  if (fix_.haveAltitude) fix_.altitudeM = x;
  // ddmm.mmmm / dddmm.mmmm
  auto coord = [](const std::string& v, const std::string& hemi, int degDigits, double& out) {
    if (v.size() < (size_t)degDigits + 2 || hemi.empty()) return false;
    char* end = nullptr;
    double raw = std::strtod(v.c_str(), &end);
    if (*end != '\0') return false;
    double deg = (int)(raw / 100);
    out = deg + (raw - deg * 100) / 60.0;
    if (hemi == "S" || hemi == "W") out = -out;
    return true;
  };
  fix_.haveLatLon = q > 0 && coord(f[2], f[3], 2, fix_.latDeg) && coord(f[4], f[5], 3, fix_.lonDeg);
  fix_.ggaMs = nowMs;
  fix_.haveGga = true;
}

void NmeaParser::parseGsa(const std::string& talker, const std::string* f, size_t n, uint32_t nowMs) {
  if (n < 18) return;
  int mode = 1, sysId = 0;
  if (toInt(f[2], mode)) fix_.mode = (uint8_t)mode;
  float x;
  if (toFloat(f[15], x)) fix_.pdop = x;
  if (toFloat(f[16], x)) fix_.hdop = x;
  if (toFloat(f[17], x)) fix_.vdop = x;
  fix_.gsaMs = nowMs;
  fix_.haveGsa = true;
  Constellation sys = Constellation::Unknown;
  if (n >= 19 && toInt(f[18], sysId)) sys = constellationFromSystemId(sysId);
  if (sys == Constellation::Unknown) sys = constellationFromTalker(talker);
  for (size_t i = 3; i <= 14; ++i) {
    int prn;
    if (!toInt(f[i], prn)) continue;
    uint16_t sv = (uint16_t)prn;
    Constellation c = sys;
    if (c == Constellation::Unknown) c = constellationFromUbloxPrn(prn, sv);
    else if (c == Constellation::Gps && prn >= 33 && prn <= 64) c = constellationFromUbloxPrn(prn, sv);
    else if (c == Constellation::Glonass && prn > 64) sv = (uint16_t)(prn - 64);
    if (c == Constellation::Unknown) continue;
    Satellite* s = findOrAdd(c, sv, nowMs);
    if (!s) continue;
    s->usedMs = nowMs;
    s->everUsed = true;
  }
}

void NmeaParser::parseGsv(const std::string& talker, const std::string* f, size_t n, uint32_t nowMs) {
  if (n < 4) return;
  Constellation sys = constellationFromTalker(talker);
  // Groups of 4 fields from index 4; NMEA 4.10 adds a trailing signal ID, giving n = 4 + 4k + 1.
  size_t groups = (n - 4) / 4;
  for (size_t g = 0; g < groups; ++g) {
    const std::string* q = f + 4 + g * 4;
    int prn;
    if (!toInt(q[0], prn)) continue;
    uint16_t sv = (uint16_t)prn;
    Constellation c = sys;
    if (c == Constellation::Unknown || (c == Constellation::Gps && prn > 32)) {
      c = constellationFromUbloxPrn(prn, sv);
    } else if (c == Constellation::Glonass && prn > 64) {
      sv = (uint16_t)(prn - 64);
    }
    if (c == Constellation::Unknown) continue;
    Satellite* s = findOrAdd(c, sv, nowMs);
    if (!s) continue;
    int v;
    s->elevation = toInt(q[1], v) ? (int8_t)v : -1;
    s->azimuth = toInt(q[2], v) ? (int16_t)v : -1;
    // With several signals per satellite (L1 and L5/E5) keep the best SNR seen this second.
    int snr = toInt(q[3], v) ? v : -1;
    if (nowMs - s->seenMs > 900 || snr > s->snr) s->snr = (int8_t)snr;
    s->seenMs = nowMs;
  }
}

void NmeaParser::parseRmc(const std::string* f, size_t n, uint32_t nowMs) {
  if (n < 10) return;
  if (!parseTime(f[1])) return;
  int d = two(f[9], 0), mo = two(f[9], 2), y = two(f[9], 4);
  utc_.dateValid = d > 0 && mo > 0 && y >= 0;
  if (utc_.dateValid) {
    utc_.day = (uint8_t)d;
    utc_.month = (uint8_t)mo;
    utc_.year = (uint16_t)(2000 + y);
  }
  utc_.valid = f[2] == "A";
  utcMs_ = nowMs;
  ++utcSets_;
}

void NmeaParser::parseZda(const std::string* f, size_t n, uint32_t nowMs) {
  if (n < 5) return;
  if (!parseTime(f[1])) return;
  int d, mo, y;
  utc_.dateValid = toInt(f[2], d) && toInt(f[3], mo) && toInt(f[4], y) && y > 1999;
  if (utc_.dateValid) {
    utc_.day = (uint8_t)d;
    utc_.month = (uint8_t)mo;
    utc_.year = (uint16_t)y;
  }
  utc_.valid = utc_.dateValid;
  utcMs_ = nowMs;
  ++utcSets_;
}

Satellite* NmeaParser::findOrAdd(Constellation sys, uint16_t prn, uint32_t nowMs) {
  Satellite* freeSlot = nullptr;
  Satellite* oldest = nullptr;
  for (auto& s : sats_) {
    if (s.valid && s.sys == sys && s.prn == prn) return &s;
    if (!s.valid && !freeSlot) freeSlot = &s;
    if (s.valid && (!oldest || s.seenMs < oldest->seenMs)) oldest = &s;
  }
  Satellite* slot = freeSlot ? freeSlot : oldest;
  if (!slot) return nullptr;
  *slot = Satellite{};
  slot->valid = true;
  slot->sys = sys;
  slot->prn = prn;
  slot->seenMs = nowMs;
  return slot;
}

void NmeaParser::expire(uint32_t nowMs) {
  for (auto& s : sats_) {
    uint32_t last = s.seenMs > s.usedMs ? s.seenMs : s.usedMs;
    if (s.valid && nowMs - last > kExpireMs) s = Satellite{};
  }
}

size_t NmeaParser::satsInView() const {
  size_t k = 0;
  for (const auto& s : sats_)
    if (s.valid && s.elevation >= 0) ++k;
  return k;
}

size_t NmeaParser::satsUsed(uint32_t nowMs) const {
  size_t k = 0;
  for (const auto& s : sats_)
    if (s.valid && isUsed(s, nowMs)) ++k;
  return k;
}

}  // namespace gpsdo
