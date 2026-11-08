// UTC to the hundredth of a second from two imperfect sources:
//  - NMEA says which second it is, but arrives a variable 50 to 200 ms after it starts;
//  - the Nano starts its report a fixed few ms after each PPS edge, so the start of a report
//    line marks the top of the second precisely, but says nothing about which second.
// Combining them gives the second from NMEA and the fraction from the PPS-derived mark.
#pragma once
#include <cstdint>

namespace gpsdo {

class UtcClock {
public:
  // Rough delay from the top of a second to the first NMEA byte of that epoch.
  static constexpr uint32_t kNmeaLatencyMs = 100;
  // A second mark older than this is not used for the fraction.
  static constexpr uint32_t kMarkMaxAgeMs = 2500;

  // Top of a UTC second, local ms (already corrected for the Nano's print delay).
  void onSecondMark(uint32_t ms) { markMs_ = ms; haveMark_ = true; }
  // A NMEA time of day (seconds since midnight) whose sentence burst started at burstStartMs.
  void onNmeaTime(uint32_t secOfDay, uint32_t burstStartMs) {
    nmeaSec_ = secOfDay;
    nmeaMs_ = burstStartMs;
    haveNmea_ = true;
  }

  // Current UTC as milliseconds of the day. ppsAligned says whether the fraction is from the
  // second mark (good to a few ms) or only from NMEA (good to about 0.1 s). False if unknown.
  bool now(uint32_t nowMs, uint32_t& msOfDay, bool& ppsAligned) const {
    if (!haveNmea_ || nowMs - nmeaMs_ > 5000) return false;
    int64_t rough = (int64_t)nmeaSec_ * 1000 + (int64_t)(nowMs - nmeaMs_) + kNmeaLatencyMs;
    ppsAligned = haveMark_ && nowMs - markMs_ <= kMarkMaxAgeMs;
    int64_t t = rough;
    if (ppsAligned) {
      int64_t frac = (int64_t)((nowMs - markMs_) % 1000);
      int64_t sec = (rough - frac + 500) / 1000;  // nearest whole second consistent with the mark
      t = sec * 1000 + frac;
    }
    t %= 86400000LL;
    if (t < 0) t += 86400000LL;
    msOfDay = (uint32_t)t;
    return true;
  }

private:
  uint32_t markMs_ = 0, nmeaMs_ = 0, nmeaSec_ = 0;
  bool haveMark_ = false, haveNmea_ = false;
};

}  // namespace gpsdo
