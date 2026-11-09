// Combines the GPSDO report and the GPS NMEA into one picture: lock state, alarms and trends.
// Pure C++; the caller supplies a millisecond clock.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

#include "arduino_report.h"
#include "event_log.h"
#include "flight_recorder.h"
#include "history.h"
#include "interference.h"
#include "skymap.h"
#include "stability.h"
#include "nmea.h"
#include "utc_clock.h"

namespace gpsdo {

// Fixed facts about this GPSDO's firmware settings, used for estimates. TIC_Offset and the lock
// rule come from the unit's f2 output and the firmware source (lockPPSfactor 5).
struct GpsdoConstants {
  static constexpr int kTicOffsetNs = 500;
  static constexpr int kLockPpsFactor = 5;
  static constexpr int kLockPpsLimitNs = 100;
  // warmUpTime from f2: seconds after the first PPS before the loop runs. A timer, not a temperature.
  static constexpr int kWarmUpS = 300;
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
  kAlarmGpsSpoof = 1 << 9,       // position jump, time step or uniform signals: GPS not to be trusted
  kAlarmGpsSignalLoss = 1 << 10, // best satellites suddenly 10 dB+ down: jamming or antenna trouble
};

// What the big banner says, in priority order.
enum class Headline : uint8_t { NoData, NoPps, Missing10MHz, RefSuspect, Unlocked, Holdover, WarmUp, Locked };
const char* headlineText(Headline h);

// One trend point covers kTrendStepS seconds of reports: the averages over that block, and
// whether the GPSDO was locked for all of it. Points are final once stored, so a chart of them
// only ever shifts left; nothing already drawn is recomputed.
struct TrendPoint {
  int16_t ns;
  int16_t diffNs;
  uint16_t dac;
  uint8_t lockedAll;  // 1 if every report in the block said Locked
  uint8_t lockedAny;  // 1 if at least one did
  uint8_t valid;      // 0 if no usable reports in the block
};

class Monitor {
public:
  static constexpr uint32_t kSilentMs = 3000;
  static constexpr uint32_t kTrendStepS = 12;       // one stored point per 12 s of reports
  static constexpr size_t kTrendPoints = 150;       // 30 minutes

  // startUs: local microseconds when the line's first byte arrived, or -1 if unknown (read from a
  // backlog), in which case the line is used for everything except timing. For the Nano that marks the
  // PPS edge (the report starts right after it); for NMEA it pairs the sentence with its second.
  // The short forms take it as nowMs, which is fine for tests and replay.
  void onNanoLine(const std::string& line, uint32_t nowMs, int64_t startUs);
  void onNanoLine(const std::string& line, uint32_t nowMs) { onNanoLine(line, nowMs, (int64_t)nowMs * 1000); }
  void onGpsLine(const std::string& line, uint32_t nowMs, int64_t startUs);
  void onGpsLine(const std::string& line, uint32_t nowMs) { onGpsLine(line, nowMs, (int64_t)nowMs * 1000); }
  // A PPS edge timed directly (if the PPS is wired to the display). Preferred over the Nano's report.
  void onPpsEdge(int64_t us) { lastPpsUs_ = us; havePps_ = true; clock_.onSecondMark(us); }
  const UtcClock& clock() const { return clock_; }
  // Delay from the PPS edge to the first byte of the Nano's report line (measured, see README).
  // Measured 2026-11-08 against NTP (stratum 2, 0.03 ms) over Wi-Fi, lowest-delay samples: the
  // CYD read 5.8 ms slow without it. Remeasure directly once the PPS is wired (console reports it).
  static constexpr int64_t kNanoPrintDelayUs = 5800;
  void onUbxSeen() { nmea_.noteUbx(); }
  // Call about once a second: expires satellites and recomputes alarms.
  void tick(uint32_t nowMs);

  const ReportTracker& nano() const { return nano_; }
  // Milliseconds since any line at all from the Nano (data, "No PPS", text); UINT32_MAX if none.
  uint32_t msSinceNanoLine(uint32_t nowMs) const {
    if (!nanoHeard_) return UINT32_MAX;
    return nanoLineStale_ ? std::max<uint32_t>(ageMs(nowMs, lastNanoLineMs_), kSilentMs + 1) : ageMs(nowMs, lastNanoLineMs_);
  }
  // Report lines rejected as damaged (layout changed, or the seconds counter jumped): what is left
  // after a serial buffer overflow can join two half-lines into something that looks valid.
  uint32_t damagedLines() const { return damagedLines_; }
  const NmeaParser& gps() const { return nmea_; }
  void restoreAntenna(const std::string& s) { nmea_.restoreAntenna(s); }
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

  // The latest status line from our modified Nano firmware, if one arrived in the last 3 s.
  bool haveStatus(uint32_t nowMs) const { return haveStatus_ && !statusStale_ && ageMs(nowMs, statusMs_) <= kSilentMs; }
  const StatusLine& status() const { return status_; }
  // PPS pulses the Nano has missed since the display started (from the status line).
  uint32_t missedPpsSinceStart() const { return missedSinceStart_; }

  // Seconds the GPSDO has been locked. With our Nano firmware's status line this is its own lock
  // counter minus the target, so it survives restarts of the display; known says whether the start
  // of the lock was actually seen (otherwise it is a lower bound).
  uint32_t secondsLocked(uint32_t nowMs, bool& known) const;

  // Filtered TIC minus TIC_Offset, ns, as used by the firmware's lock test. Valid if hasDetail.
  float ticErrorNs() const;
  // While unlocked: the soonest the firmware could declare lock (5 x tc minus seconds since the
  // last unlock), in seconds; 0 if unknown or already due.
  uint32_t earliestLockS() const;
  // Progress towards the next milestone as 0..1000, or -1 when there is nothing to show:
  // during warm-up, the warm-up timer; while unlocked, the 5 x tc good seconds the lock test needs.
  int progressPermille() const;
  // Seconds left of the warm-up timer; 0 if not warming up.
  uint32_t warmUpRemainingS() const;

  const NanoHistory& history() const { return history_; }
  const Stability& stability() const { return stability_; }
  SkyMap& skyMap() { return skyMap_; }
  const SkyMap& skyMap() const { return skyMap_; }
  InterferenceWatch& watch() { return watch_; }
  FlightRecorder& recorder() { return recorder_; }
  const FlightRecorder& recorder() const { return recorder_; }
  const InterferenceWatch& watch() const { return watch_; }
  EventLog& events() { return events_; }
  const EventLog& events() const { return events_; }
  // Add an event stamped with the current UTC if known (used by the firmware for network events).
  void logEvent(uint32_t nowMs, uint8_t level, const char* text);

  // Average frequency error of the 10 MHz over the last `seconds` (a multiple of 12), in parts per
  // billion, from the change in average phase vs the GPS pulse. False until that much is stored.
  bool frequencyPpb(uint32_t seconds, float& ppb) const;

  // Mean signal strength of the four strongest satellites in the fix, now and over 24 hours.
  int top4Snr() const { return top4_; }
  bool top4Range(int& lo, int& hi) const;

  // Trend ring: index 0 is oldest. count() <= kTrendPoints.
  size_t trendCount() const { return trendCount_; }
  const TrendPoint& trend(size_t i) const {
    return trend_[(trendHead_ + kTrendPoints - trendCount_ + i) % kTrendPoints];
  }

private:
  void addTrend(const ReportLine& d);
  void detectEvents(uint32_t nowMs);

  ReportTracker nano_;
  NmeaParser nmea_;
  UtcClock clock_;
  int64_t lastPpsUs_ = 0;
  NanoHistory history_;
  Stability stability_;
  SkyMap skyMap_;
  InterferenceWatch watch_;
  FlightRecorder recorder_;
  uint32_t lastFrMs_ = 0, frFinishedSeen_ = 0;
  bool frPrevGpsGood_ = false;
  uint16_t frPrevAlarms_ = 0;
  void recordSecond(uint32_t nowMs);
  uint32_t lastWatchMs_ = 0, lastSkyMs_ = 0, clockSteps_ = 0;
  bool prevSpoof_ = false, prevSignalLoss_ = false;
  StatusLine status_;
  bool haveStatus_ = false;
  uint32_t statusMs_ = 0, missedSinceStart_ = 0, lastMissed_ = 0;
  bool haveMissedBase_ = false;
  EventLog events_;
  bool eventsArmed_ = false;
  Headline prevHeadline_ = Headline::NoData, headlineCandidate_ = Headline::NoData;
  uint32_t headlineSinceMs_ = 0;
  bool prevGpsGood_ = false, gpsGoodCandidate_ = false;
  uint32_t gpsGoodSinceMs_ = 0;
  std::string prevAntenna_;
  uint32_t prevGpsdoRestarts_ = 0, prevGpsRestarts_ = 0;
  int top4_ = -1;
  std::array<int8_t, 144> top4Hist_{};  // one point per 10 minutes, 24 hours
  size_t top4Head_ = 0, top4Count_ = 0;
  int32_t top4Acc_ = 0;
  uint16_t top4AccN_ = 0;
  uint32_t top4LastMs_ = 0;
  bool havePps_ = false;
  bool gpsSeen_ = false;
  uint32_t lastHelpMs_ = 0;
  bool sawHelp_ = false;
  // Once a source has been silent past kSilentMs it stays stale until new data arrives, so a clock
  // that wraps round can never make old data look fresh again.
  bool nanoDataStale_ = false, nanoLineStale_ = false, gpsStale_ = false, statusStale_ = false;
  // Report line sanity: the layout of the first good line, and a pending line whose time jumped.
  int layoutStatusField_ = -1;
  bool layoutDetail_ = false;
  bool havePending_ = false;
  uint32_t pendingTime_ = 0, damagedLines_ = 0;
  bool plausible(const ParsedLine& p);
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
  // Block being accumulated.
  int32_t accNs_ = 0, accDiff_ = 0;
  uint32_t accDac_ = 0;
  uint16_t accN_ = 0, accLocked_ = 0, accSeen_ = 0;
  uint32_t accStart_ = 0;
  bool accOpen_ = false;
  uint32_t lastNowMs_ = 0;
};

}  // namespace gpsdo
