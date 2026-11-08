// Combines the GPSDO report and the GPS NMEA into one picture: lock state, alarms and trends.
// Pure C++; the caller supplies a millisecond clock.
#pragma once
#include <array>
#include <cstdint>
#include <string>

#include "arduino_report.h"
#include "nmea.h"

namespace gpsdo {

// Fixed facts about this GPSDO's firmware settings, used for estimates. TIC_Offset and the lock
// rule come from the unit's f2 output and the firmware source (lockPPSfactor 5).
struct GpsdoConstants {
  static constexpr int kTicOffsetNs = 500;
  static constexpr int kLockPpsFactor = 5;
  static constexpr int kLockPpsLimitNs = 100;
};

enum Alarm : uint16_t {
  kAlarmNanoSilent = 1 << 0,     // no report line for > 3 s
  kAlarmNoPps = 1 << 1,          // Nano printing "No PPS"
  kAlarmMissing10MHz = 1 << 2,   // Nano printing "Missing 10MHz?"
  kAlarmUnlocked = 1 << 3,       // status not Locked
  kAlarmDacLimit = 1 << 4,       // DAC < 3000 or > 62535
  kAlarmGpsSilent = 1 << 5,      // no valid NMEA for > 3 s
  kAlarmGpsNoFix = 1 << 6,       // GGA quality 0 or GSA mode < 2
  kAlarmGpsFewSats = 1 << 7,     // fewer than 4 satellites used
  kAlarmRefSuspect = 1 << 8,     // GPSDO says Locked but the GPS has no good fix
};

// What the big banner says, in priority order.
enum class Headline : uint8_t { NoData, NoPps, Missing10MHz, RefSuspect, Unlocked, Holdover, WarmUp, Locked };
const char* headlineText(Headline h);

struct TrendPoint {
  int16_t ns;
  int16_t diffNs;
  uint16_t dac;
  uint8_t status;  // LockStatus
  uint8_t valid;
};

class Monitor {
public:
  static constexpr uint32_t kSilentMs = 3000;
  static constexpr uint32_t kTrendStepS = 2;        // one stored point per 2 Nano seconds
  static constexpr size_t kTrendPoints = 900;       // 30 minutes

  void onNanoLine(const std::string& line, uint32_t nowMs);
  void onGpsLine(const std::string& line, uint32_t nowMs);
  void onUbxSeen() { nmea_.noteUbx(); }
  // Call about once a second: expires satellites and recomputes alarms.
  void tick(uint32_t nowMs);

  const ReportTracker& nano() const { return nano_; }
  // Milliseconds since any line at all from the Nano (data, "No PPS", text); UINT32_MAX if none.
  uint32_t msSinceNanoLine(uint32_t nowMs) const { return nanoHeard_ ? nowMs - lastNanoLineMs_ : UINT32_MAX; }
  const NmeaParser& gps() const { return nmea_; }
  bool gpsEverSeen() const { return gpsSeen_; }

  // Start-ups seen while the display has been running: the Nano's banner, and the u-blox's
  // "u-blox AG" boot text. Both at once usually means the GPSDO lost power.
  uint32_t gpsdoRestarts() const { return gpsdoRestarts_; }
  uint32_t gpsRestarts() const { return gpsRestarts_; }
  uint32_t lastRestartMs() const { return lastRestartMs_; }  // valid if either count > 0

  uint16_t alarms() const { return alarms_; }
  Headline headline() const { return headline_; }
  // True only when the GPSDO is locked and the GPS confirms a good fix: the output can be trusted.
  bool referenceOk() const { return referenceOk_; }

  // Filtered TIC minus TIC_Offset, ns, as used by the firmware's lock test. Valid if hasDetail.
  float ticErrorNs() const;
  // While unlocked: the soonest the firmware could declare lock (5 x tc minus seconds since the
  // last unlock), in seconds; 0 if unknown or already due.
  uint32_t earliestLockS() const;

  // Trend ring: index 0 is oldest. count() <= kTrendPoints.
  size_t trendCount() const { return trendCount_; }
  const TrendPoint& trend(size_t i) const {
    return trend_[(trendHead_ + kTrendPoints - trendCount_ + i) % kTrendPoints];
  }

private:
  void addTrend(const ReportLine& d);

  ReportTracker nano_;
  NmeaParser nmea_;
  bool gpsSeen_ = false;
  uint32_t gpsdoRestarts_ = 0, gpsRestarts_ = 0, lastRestartMs_ = 0;
  bool nanoHeard_ = false;
  uint32_t lastNanoLineMs_ = 0;
  bool missing10MHz_ = false;
  uint32_t unlockedSinceNanoTime_ = 0;
  bool haveUnlockStart_ = false;
  uint16_t alarms_ = kAlarmNanoSilent | kAlarmGpsSilent;
  Headline headline_ = Headline::NoData;
  bool referenceOk_ = false;
  std::array<TrendPoint, kTrendPoints> trend_{};
  size_t trendHead_ = 0, trendCount_ = 0;
  uint32_t lastTrendTime_ = 0;
  uint32_t lastNowMs_ = 0;
};

}  // namespace gpsdo
