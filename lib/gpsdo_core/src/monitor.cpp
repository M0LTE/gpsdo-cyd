#include "monitor.h"

#include <cmath>

namespace gpsdo {

const char* headlineText(Headline h) {
  switch (h) {
    case Headline::NoData: return "NO DATA";
    case Headline::NoPps: return "NO PPS";
    case Headline::Missing10MHz: return "NO 10 MHz";
    case Headline::RefSuspect: return "GPS LOST";
    case Headline::Unlocked: return "UNLOCKED";
    case Headline::Holdover: return "HOLD";
    case Headline::WarmUp: return "WARM UP";
    case Headline::Locked: return "LOCKED";
  }
  return "?";
}

void Monitor::onNanoLine(const std::string& line, uint32_t nowMs) {
  ParsedLine p = parseReportLine(line);
  if (p.kind != LineKind::Empty) { nanoHeard_ = true; lastNanoLineMs_ = nowMs; }
  bool wasLocked = nano_.locked();
  nano_.onLine(p, nowMs);
  if (p.kind != LineKind::Data) { tick(nowMs); return; }
  const ReportLine& d = p.data;
  missing10MHz_ = d.missing10MHz;
  if (d.status != LockStatus::Locked) {
    if (wasLocked || !haveUnlockStart_ || d.time < unlockedSinceNanoTime_) {
      unlockedSinceNanoTime_ = d.time;
      haveUnlockStart_ = true;
    }
    // The firmware's lock counter also restarts whenever the filtered TIC strays past the limit.
    if (d.hasDetail && std::fabs(d.filtX10 / 10.0f - GpsdoConstants::kTicOffsetNs) > GpsdoConstants::kLockPpsLimitNs)
      unlockedSinceNanoTime_ = d.time;
  } else {
    haveUnlockStart_ = false;
  }
  addTrend(d);
  tick(nowMs);
}

void Monitor::onGpsLine(const std::string& line, uint32_t nowMs) {
  if (line.empty() || line[0] != '$') return;
  if (nmea_.parse(line, nowMs)) gpsSeen_ = true;
}

void Monitor::addTrend(const ReportLine& d) {
  if (trendCount_ && d.time >= lastTrendTime_ && d.time - lastTrendTime_ < kTrendStepS) return;
  lastTrendTime_ = d.time;
  TrendPoint t;
  auto clamp16 = [](int32_t v) -> int16_t { return v > 32767 ? 32767 : v < -32768 ? -32768 : (int16_t)v; };
  t.ns = d.missing10MHz ? 0 : clamp16(d.ns);
  t.diffNs = clamp16(d.diffNs);
  t.dac = d.dac;
  t.status = (uint8_t)d.status;
  t.valid = d.missing10MHz ? 0 : 1;
  trend_[trendHead_] = t;
  trendHead_ = (trendHead_ + 1) % kTrendPoints;
  if (trendCount_ < kTrendPoints) ++trendCount_;
}

void Monitor::tick(uint32_t nowMs) {
  lastNowMs_ = nowMs;
  nmea_.expire(nowMs);
  uint16_t a = 0;
  bool nanoLive = nano_.msSinceData(nowMs) <= kSilentMs;
  // "No PPS" lines also prove the Nano is alive; only total silence is "no data".
  bool nanoHeard = msSinceNanoLine(nowMs) <= kSilentMs;
  if (!nanoHeard) a |= kAlarmNanoSilent;
  if (nano_.msSinceNoPps(nowMs) <= kSilentMs) a |= kAlarmNoPps;
  if (nanoLive && missing10MHz_) a |= kAlarmMissing10MHz;
  if ((nano_.haveData() && !nano_.locked()) || (a & kAlarmNoPps)) a |= kAlarmUnlocked;
  if (nano_.haveData() && ReportTracker::dacWarning(nano_.last().dac)) a |= kAlarmDacLimit;

  bool gpsLive = gpsSeen_ && nowMs - nmea_.lastGoodMs() <= kSilentMs;
  const Fix& f = nmea_.fix();
  if (!gpsLive) {
    a |= kAlarmGpsSilent;
  } else {
    bool noFix = (f.haveGga && f.quality == 0) || (f.haveGsa && f.mode < 2) || (!f.haveGga && !f.haveGsa);
    if (noFix) a |= kAlarmGpsNoFix;
    uint8_t used = f.haveGga ? f.satsUsed : (uint8_t)nmea_.satsUsed(nowMs);
    if (used < 4) a |= kAlarmGpsFewSats;
  }
  bool locked = nanoLive && nano_.locked();
  bool gpsGood = gpsLive && !(a & (kAlarmGpsNoFix | kAlarmGpsFewSats));
  // Only claim "suspect" when we have heard the GPS at all; before the tap is wired it is unknown.
  if (locked && gpsSeen_ && !gpsGood) a |= kAlarmRefSuspect;
  referenceOk_ = locked && gpsGood && !(a & (kAlarmNoPps | kAlarmMissing10MHz));
  alarms_ = a;

  if (!nanoHeard) headline_ = Headline::NoData;
  else if (a & kAlarmNoPps) headline_ = Headline::NoPps;
  else if (a & kAlarmMissing10MHz) headline_ = Headline::Missing10MHz;
  else if (a & kAlarmRefSuspect) headline_ = Headline::RefSuspect;
  else if (!nanoLive) headline_ = Headline::NoData;
  else if (locked) headline_ = Headline::Locked;
  else if (nano_.last().status == LockStatus::Hold) headline_ = Headline::Holdover;
  else if (nano_.last().status == LockStatus::WarmUp) headline_ = Headline::WarmUp;
  else headline_ = Headline::Unlocked;
}

float Monitor::ticErrorNs() const {
  const ReportLine& d = nano_.last();
  return d.filtX10 / 10.0f - GpsdoConstants::kTicOffsetNs;
}

uint32_t Monitor::earliestLockS() const {
  if (!haveUnlockStart_ || !nano_.haveData() || nano_.locked()) return 0;
  const ReportLine& d = nano_.last();
  if (!d.hasDetail || d.tc == 0) return 0;
  uint32_t need = (uint32_t)d.tc * GpsdoConstants::kLockPpsFactor + 1;
  uint32_t done = d.time - unlockedSinceNanoTime_;
  return done >= need ? 0 : need - done;
}

}  // namespace gpsdo
