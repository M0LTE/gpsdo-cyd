#include "history.h"

#include <cstdlib>

namespace gpsdo {
namespace {
bool num(const std::string& s, long& v) {
  if (s.empty()) return false;
  char* e = nullptr;
  v = std::strtol(s.c_str(), &e, 10);
  return *e == 0;
}
bool flt(const std::string& s, float& v) {
  if (s.empty()) return false;
  char* e = nullptr;
  v = std::strtof(s.c_str(), &e);
  return *e == 0;
}
}  // namespace

void NanoHistory::onTail(const std::vector<std::string>& t) {
  if (t.empty()) return;
  const std::string& f0 = t[0];
  if (f0.rfind("Five minute averages", 0) == 0) { section_ = 5; return; }
  if (f0.rfind("Three hour averages", 0) == 0) { section_ = 3; return; }
  if (f0.rfind("Now acquiring value:", 0) == 0) {
    std::string n = f0.substr(20);
    n.erase(0, n.find_first_not_of(' '));
    long v;
    if (!num(n, v) || v < 0 || v >= kSize) return;
    if (section_ == 5) next5_ = (int)v;
    else if (section_ == 3) next3_ = (int)v;
    return;
  }
  long idx, tic, dac;
  float temp = 0;
  if (t.size() < 4 || !num(t[0], idx) || !num(t[1], tic) || !num(t[2], dac)) return;
  bool tempOk = flt(t[3], temp);
  if (idx >= 0 && idx < kSize) {
    HistoryEntry& e = five_[idx];
    e.valid = !(tic == 0 && dac == 0) && tempOk;
    e.ticX10 = (int32_t)tic;
    e.dac = (uint16_t)dac;
    e.tempC = temp;
    if (!got5_[idx]) { got5_[idx] = true; ++seen5_; }
  } else if (idx >= 1000 && idx < 1000 + kSize) {
    int i = (int)(idx - 1000);
    HistoryEntry& e = three_[i];
    // Unused EEPROM slots read 0 or 65535 (and the temperature is then printed raw).
    e.valid = tempOk && dac != 0 && dac != 65535 && temp > -40 && temp < 100;
    e.ticX10 = (int32_t)tic;
    e.dac = (uint16_t)dac;
    e.tempC = temp;
    e.lock = ' ';
    if (t.size() >= 5) {
      const std::string& st = t[4];
      e.lock = st == "Locked" ? 'L' : st == "****" ? 'U' : st == "Hold" ? 'H' : st.rfind("Restarted", 0) == 0 ? 'R' : ' ';
    }
    if (!got3_[i]) { got3_[i] = true; ++seen3_; }
  }
}

std::vector<HistoryEntry> NanoHistory::ordered(bool fiveMin) const {
  const auto& a = fiveMin ? five_ : three_;
  int start = fiveMin ? next5_ : next3_;
  if (start < 0) start = 0;
  std::vector<HistoryEntry> out;
  for (int n = 0; n < kSize; ++n) {
    const HistoryEntry& e = a[(start + n) % kSize];
    if (e.valid) out.push_back(e);
  }
  return out;
}

}  // namespace gpsdo
