#include "arduino_report.h"

#include <climits>
#include <cstdlib>
#include <vector>

namespace gpsdo {
namespace {

std::vector<std::string> splitTabs(const std::string& s) {
  std::vector<std::string> out;
  size_t start = 0;
  for (;;) {
    size_t t = s.find('\t', start);
    if (t == std::string::npos) { out.push_back(s.substr(start)); break; }
    out.push_back(s.substr(start, t - start));
    start = t + 1;
  }
  return out;
}

std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

bool toLong(const std::string& s, long& v) {
  std::string t = trim(s);
  if (t.empty()) return false;
  char* end = nullptr;
  v = std::strtol(t.c_str(), &end, 10);
  return *end == '\0';
}

bool toFloat(const std::string& s, float& v) {
  std::string t = trim(s);
  if (t.empty()) return false;
  char* end = nullptr;
  v = std::strtof(t.c_str(), &end);
  return *end == '\0';
}

LockStatus statusOf(const std::string& f) {
  std::string t = trim(f);
  if (t == "Locked") return LockStatus::Locked;
  if (t == "****") return LockStatus::Unlocked;
  if (t == "Hold") return LockStatus::Hold;
  if (t == "WarmUp") return LockStatus::WarmUp;
  return LockStatus::Unknown;
}

bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

}  // namespace

const char* lockStatusName(LockStatus s) {
  switch (s) {
    case LockStatus::Locked: return "Locked";
    case LockStatus::Unlocked: return "Unlocked";
    case LockStatus::Hold: return "Hold";
    case LockStatus::WarmUp: return "WarmUp";
    default: return "Unknown";
  }
}

ParsedLine parseReportLine(const std::string& raw) {
  ParsedLine p;
  std::string t = trim(raw);
  if (t.empty()) return p;
  if (t == "No PPS") { p.kind = LineKind::NoPps; return p; }
  if (startsWith(t, "Arduino GPSDO")) { p.kind = LineKind::Banner; p.text = t; return p; }

  std::vector<std::string> f = splitTabs(raw);
  // Live status is the 5th field of the data part: index 4, or 6 in 3.7.x where a leading tab
  // and a duty_cycle column come first.
  for (size_t s = 4; s <= 6 && s < f.size(); ++s) {
    LockStatus st = statusOf(f[s]);
    if (st == LockStatus::Unknown) continue;
    ReportLine d;
    long v;
    float x;
    if (!toLong(f[s - 4], v) || v < 0) continue;
    d.time = (uint32_t)v;
    if (trim(f[s - 3]) == "Missing 10MHz?") d.missing10MHz = true;
    else if (toLong(f[s - 3], v)) d.ns = (int32_t)v;
    else continue;
    if (!toLong(f[s - 2], v) || v < 0 || v > 65535) continue;
    d.dac = (uint16_t)v;
    if (!toFloat(f[s - 1], x)) continue;
    d.ocxoC = x;
    d.status = st;
    if (f.size() >= s + 7) {
      long a, b, c, e, g;
      float amb;
      if (toLong(f[s + 1], a) && toLong(f[s + 2], b) && toLong(f[s + 3], c) &&
          toLong(f[s + 4], e) && toLong(f[s + 5], g) && toFloat(f[s + 6], amb)) {
        d.hasDetail = true;
        d.diffNs = (int32_t)a;
        d.filtX10 = (int32_t)b;
        d.tc = (uint16_t)c;
        d.filt = (uint16_t)e;
        d.timer1 = (int32_t)g;
        d.ambientC = amb;
      }
    }
    // The startup revision is also repeated in the rolling tail every 300 s.
    for (size_t k = s + 1; k < f.size(); ++k) {
      std::string tf = trim(f[k]);
      if (startsWith(tf, "Rev.")) { p.text = tf; break; }
    }
    p.kind = LineKind::Data;
    p.data = d;
    return p;
  }

  if (t.find("time\tns\tdac") != std::string::npos) { p.kind = LineKind::Header; p.text = t; return p; }
  if (startsWith(t, "Rev.")) {
    p.kind = LineKind::Revision;
    p.text = trim(splitTabs(t)[0]);
    return p;
  }
  p.kind = LineKind::Other;
  p.text = t;
  return p;
}

void ReportTracker::onLine(const ParsedLine& p, uint32_t nowMs) {
  switch (p.kind) {
    case LineKind::Revision:
      revision_ = p.text;
      break;
    case LineKind::NoPps:
      noPpsSinceData_ = true;
      lastNoPpsMs_ = nowMs;
      break;
    case LineKind::Data: {
      const ReportLine& d = p.data;
      if (!p.text.empty()) revision_ = p.text;
      if (haveData_ && d.time < last_.time) {
        // Nano restarted: its time counter starts again. Lock tracking restarts with it.
        ++restarts_;
        if (wasLocked_) { ++lockLosses_; lastLockLossMs_ = nowMs; }
        wasLocked_ = false;
        lockStartKnown_ = false;
      }
      bool nowLocked = d.status == LockStatus::Locked;
      if (nowLocked && !wasLocked_) {
        lockStartTime_ = d.time;
        lockStartKnown_ = haveData_;  // first line already locked: start unknown
      } else if (!nowLocked && wasLocked_) {
        ++lockLosses_;
        lastLockLossMs_ = nowMs;
      }
      wasLocked_ = nowLocked;
      last_ = d;
      haveData_ = true;
      lastDataMs_ = nowMs;
      noPpsSinceData_ = false;
      break;
    }
    default:
      break;
  }
}

uint32_t ReportTracker::msSinceData(uint32_t nowMs) const {
  return haveData_ ? nowMs - lastDataMs_ : UINT32_MAX;
}

uint32_t ReportTracker::msSinceNoPps(uint32_t nowMs) const {
  return noPpsSinceData_ ? nowMs - lastNoPpsMs_ : UINT32_MAX;
}

uint32_t ReportTracker::secondsLocked() const {
  if (!locked()) return 0;
  return last_.time - lockStartTime_;
}

}  // namespace gpsdo
