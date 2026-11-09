// UTC from two imperfect sources, in microseconds of a local monotonic clock:
//  - second marks: the start of each UTC second, from the PPS (or the Nano's PPS-triggered report)
//    but with no idea which second it is;
//  - NMEA time sentences: which second it is, but arriving a variable time after it starts.
// Each time sentence is paired with the latest mark before it (it must be under a second old),
// which ties a UTC second to a local timestamp. Later marks then carry the time forward.
#pragma once
#include <cstdint>

namespace gpsdo {

class UtcClock {
public:
  static constexpr int64_t kNmeaLatencyUs = 100000;  // rough NMEA delay, used only without marks
  static constexpr int64_t kMarkMaxAgeUs = 2500000;  // a mark older than this means no PPS
  static constexpr int64_t kNmeaMaxAgeUs = 5000000;  // NMEA older than this means no GPS time

  void onSecondMark(int64_t us) {
    prevMarkUs_ = markUs_;
    havePrevMark_ = haveMark_;
    markUs_ = us;
    haveMark_ = true;
  }

  // A time sentence for UTC second unixSec whose first byte arrived at sentenceUs.
  void onNmeaTime(int64_t unixSec, int64_t sentenceUs) {
    nmeaSec_ = unixSec;
    nmeaUs_ = sentenceUs;
    haveNmea_ = true;
    // Pair with the latest mark at or before the sentence, if under a second before it.
    int64_t m = -1;
    if (haveMark_ && markUs_ <= sentenceUs) m = markUs_;
    else if (havePrevMark_ && prevMarkUs_ <= sentenceUs) m = prevMarkUs_;
    if (m >= 0 && sentenceUs - m < 1000000) {
      // With an anchor already in place, the second this mark should carry is known from the marks
      // alone. If the GPS says otherwise its time has stepped (or been tampered with).
      if (haveAnchor_ && m > anchorUs_ && m - anchorUs_ < 3600LL * 1000000) {
        int64_t expected = anchorSec_ + (m - anchorUs_ + 500000) / 1000000;
        if (expected != unixSec) ++steps_;
      }
      anchorSec_ = unixSec;
      anchorUs_ = m;
      haveAnchor_ = true;
    }
  }

  // How many times the GPS second has disagreed with the running count of second marks.
  uint32_t steps() const { return steps_; }

  // Current UTC as microseconds since 1970. ppsAligned: the fraction comes from a recent second mark
  // (good to the mark's accuracy) rather than from NMEA alone (good to about 0.1 s).
  bool now(int64_t nowUs, int64_t& unixUs, bool& ppsAligned) const {
    if (!haveNmea_ || nowUs - nmeaUs_ > kNmeaMaxAgeUs) return false;
    ppsAligned = haveAnchor_ && haveMark_ && nowUs - markUs_ <= kMarkMaxAgeUs && markUs_ >= anchorUs_ &&
                 nowUs - anchorUs_ < 3600LL * 1000000;
    if (ppsAligned) {
      // Whole seconds between the anchor's mark and the latest mark, then the fraction since it.
      int64_t secs = (markUs_ - anchorUs_ + 500000) / 1000000;
      unixUs = (anchorSec_ + secs) * 1000000 + (nowUs - markUs_);
    } else {
      unixUs = nmeaSec_ * 1000000 + (nowUs - nmeaUs_) + kNmeaLatencyUs;
    }
    return true;
  }

private:
  int64_t markUs_ = 0, prevMarkUs_ = 0, nmeaUs_ = 0, nmeaSec_ = 0, anchorUs_ = 0, anchorSec_ = 0;
  bool haveMark_ = false, havePrevMark_ = false, haveNmea_ = false, haveAnchor_ = false;
  uint32_t steps_ = 0;
};

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's algorithm).
inline int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int64_t)doe - 719468;
}

}  // namespace gpsdo
