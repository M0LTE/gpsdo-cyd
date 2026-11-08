// The GPSDO's own stored history, collected from the rolling replay at the end of its report lines.
// Every 300 s the Nano reprints, one entry per second:
//   - 144 five-minute averages (12 hours), kept in RAM, lost when it restarts;
//   - 144 three-hour averages (18 days), kept in EEPROM, each with a lock flag.
// Each entry: TIC average x10 (ns, around TIC_Offset x10), DAC average, oscillator temperature (C).
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace gpsdo {

struct HistoryEntry {
  bool valid = false;
  int32_t ticX10 = 0;
  uint16_t dac = 0;
  float tempC = 0;
  char lock = ' ';  // 3-hour entries: 'L' locked 3 h+, 'U' running but not locked, 'H' hold, 'R' restarted, ' ' unknown
};

class NanoHistory {
public:
  static constexpr int kSize = 144;
  void onTail(const std::vector<std::string>& tail);

  // Oldest to newest, empty entries skipped. fiveMin: 5-minute series; else 3-hour series.
  std::vector<HistoryEntry> ordered(bool fiveMin) const;
  // The same order without building a vector: f(const HistoryEntry&) for each valid entry.
  template <class F>
  void forEach(bool fiveMin, F f) const {
    const auto& a = fiveMin ? five_ : three_;
    int start = fiveMin ? next5_ : next3_;
    if (start < 0) start = 0;
    for (int n = 0; n < kSize; ++n) {
      const HistoryEntry& e = a[(start + n) % kSize];
      if (e.valid) f(e);
    }
  }
  int seen(bool fiveMin) const { return fiveMin ? seen5_ : seen3_; }  // entries received so far
  bool indexKnown(bool fiveMin) const { return fiveMin ? next5_ >= 0 : next3_ >= 0; }

private:
  std::array<HistoryEntry, kSize> five_{}, three_{};
  std::array<bool, kSize> got5_{}, got3_{};
  int next5_ = -1, next3_ = -1;  // the Nano's "Now acquiring value", i.e. the slot written next
  int seen5_ = 0, seen3_ = 0;
  int section_ = 0;               // 5 or 3 after the matching "... averages" heading
};

}  // namespace gpsdo
