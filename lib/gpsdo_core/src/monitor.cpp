#include "monitor.h"

#include "age.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

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

void Monitor::onNanoLine(const std::string& line, uint32_t nowMs, int64_t startUs) {
  ParsedLine p = parseReportLine(line);
  if (p.kind != LineKind::Empty) { nanoHeard_ = true; lastNanoLineMs_ = nowMs; nanoLineStale_ = false; }
  if (p.kind == LineKind::Data && !plausible(p)) { ++damagedLines_; return; }
  // The f1 help text reprints the start-up banner straight after its first line; that's no restart.
  if (p.kind == LineKind::Other && p.text.rfind("Info and help", 0) == 0) { sawHelp_ = true; lastHelpMs_ = nowMs; }
  if (p.kind == LineKind::Banner && !(sawHelp_ && ageMs(nowMs, lastHelpMs_) < 2000)) {
    ++gpsdoRestarts_;
    lastRestartMs_ = nowMs;
  }
  if (p.kind == LineKind::Status) {
    const StatusLine& st = p.status;
    if (haveMissedBase_ && st.missedPps > lastMissed_) {
      uint32_t n = st.missedPps - lastMissed_;
      missedSinceStart_ += n;
      char b[48];
      std::snprintf(b, sizeof b, "Missed %u GPS pulse%s (%u since start)", (unsigned)(n % 1000), n > 1 ? "s" : "",
                    (unsigned)(missedSinceStart_ % 100000));
      if (eventsArmed_) logEvent(nowMs, 1, b);
    }
    lastMissed_ = st.missedPps;
    haveMissedBase_ = true;
    status_ = st;
    haveStatus_ = true;
    statusStale_ = false;
    statusMs_ = nowMs;
    return;
  }
  bool wasLocked = nano_.locked();
  nano_.onLine(p, nowMs);
  if (p.kind != LineKind::Data) { tick(nowMs); return; }
  const ReportLine& d = p.data;
  nanoDataStale_ = false;
  missing10MHz_ = d.missing10MHz;
  // Use the report as the second mark unless the PPS itself is wired and live.
  // startUs < 0: the line came out of a backlog, so its arrival time says nothing about the second.
  if (startUs >= 0 && !d.missing10MHz && !(havePps_ && startUs - lastPpsUs_ < 2500000))
    clock_.onSecondMark(startUs - kNanoPrintDelayUs);
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
  stability_.add(d.time, d.ns, d.status == LockStatus::Locked && !d.missing10MHz && d.hasDetail);
  history_.onTail(p.tail);
  tick(nowMs);
}

void Monitor::onGpsLine(const std::string& line, uint32_t nowMs, int64_t startUs) {
  if (line.empty() || line[0] != '$') return;
  // Checked before the checksum: after a power cut the boot text can arrive glued to a sentence
  // that was cut off, so the whole line fails its checksum.
  if (line.find("TXT,") != std::string::npos && line.find("u-blox AG") != std::string::npos) {
    ++gpsRestarts_;
    lastRestartMs_ = nowMs;
  }
  uint32_t sets = nmea_.utcSetCount();
  if (!nmea_.parse(line, nowMs)) return;
  gpsSeen_ = true;
  gpsStale_ = false;
  if (startUs >= 0 && nmea_.utcSetCount() != sets && nmea_.utc().dateValid) {
    const UtcTime& u = nmea_.utc();
    int64_t unixSec = daysFromCivil(u.year, u.month, u.day) * 86400 + u.hour * 3600 + u.minute * 60 + u.second;
    clock_.onNmeaTime(unixSec, startUs);
  }
}

bool Monitor::plausible(const ParsedLine& p) {
  const ReportLine& d = p.data;
  // The rolling replay at the end of a line has 0, 1, 4 or 5 fields; anything else is two lines joined.
  int tailFields = 0;
  for (const auto& f : p.tail) if (!f.empty()) ++tailFields;
  if (tailFields == 2 || tailFields == 3 || tailFields > 5) return false;
  // The column layout never changes on a running unit.
  if (layoutStatusField_ < 0) {
    if (!d.hasDetail) return true;  // accept, but only a full line establishes the layout
    layoutStatusField_ = d.statusField;
    layoutDetail_ = true;
  } else if (d.statusField != layoutStatusField_ || (layoutDetail_ && !d.hasDetail)) {
    return false;
  }
  // The seconds counter goes up by one per report. A small gap (lost lines) or a restart from near
  // zero is fine; a big jump is held until the next line confirms it.
  if (nano_.haveData()) {
    uint32_t prev = nano_.last().time;
    bool next = d.time > prev && d.time - prev <= 10;
    bool restart = d.time < prev && d.time < 100;
    if (!next && !restart) {
      if (havePending_ && d.time > pendingTime_ && d.time - pendingTime_ <= 10) {
        havePending_ = false;  // two lines agree: the counter really did move on
        return true;
      }
      havePending_ = true;
      pendingTime_ = d.time;
      return false;
    }
  }
  havePending_ = false;
  return true;
}

void Monitor::addTrend(const ReportLine& d) {
  // Close the block when its time is up, or if the Nano's clock went backwards (a restart).
  if (accOpen_ && (d.time < accStart_ || d.time - accStart_ >= kTrendStepS)) {
    TrendPoint t{};
    auto clamp16 = [](int32_t v) -> int16_t { return v > 32767 ? 32767 : v < -32768 ? -32768 : (int16_t)v; };
    t.valid = accN_ > 0;
    if (accN_) {
      t.ns = clamp16(accNs_ / accN_);
      t.diffNs = clamp16(accDiff_ / accN_);
      t.dac = (uint16_t)(accDac_ / accN_);
    }
    t.lockedAll = accSeen_ && accLocked_ == accSeen_;
    t.lockedAny = accLocked_ > 0;
    trend_[trendHead_] = t;
    trendHead_ = (trendHead_ + 1) % kTrendPoints;
    if (trendCount_ < kTrendPoints) ++trendCount_;
    accOpen_ = false;
  }
  if (!accOpen_) {
    accOpen_ = true;
    accStart_ = d.time - d.time % kTrendStepS;
    accNs_ = accDiff_ = 0;
    accDac_ = 0;
    accN_ = accLocked_ = accSeen_ = 0;
  }
  ++accSeen_;
  if (d.status == LockStatus::Locked) ++accLocked_;
  if (!d.missing10MHz) {
    accNs_ += d.ns;
    accDiff_ += d.diffNs;
    accDac_ += d.dac;
    ++accN_;
  }
}

void Monitor::tick(uint32_t nowMs) {
  lastNowMs_ = nowMs;
  nmea_.expire(nowMs);
  uint16_t a = 0;
  bool nanoLive = !nanoDataStale_ && nano_.msSinceData(nowMs) <= kSilentMs;
  if (nano_.haveData() && !nanoLive) nanoDataStale_ = true;
  // "No PPS" lines also prove the Nano is alive; only total silence is "no data".
  bool nanoHeard = !nanoLineStale_ && msSinceNanoLine(nowMs) <= kSilentMs;
  if (nanoHeard_ && !nanoHeard) nanoLineStale_ = true;
  if (haveStatus_ && ageMs(nowMs, statusMs_) > kSilentMs) statusStale_ = true;
  if (!nanoHeard) a |= kAlarmNanoSilent;
  if (nano_.msSinceNoPps(nowMs) <= kSilentMs) a |= kAlarmNoPps;
  if (nanoLive && missing10MHz_) a |= kAlarmMissing10MHz;
  if ((nano_.haveData() && !nano_.locked()) || (a & kAlarmNoPps)) a |= kAlarmUnlocked;
  if (nano_.haveData() && ReportTracker::dacWarning(nano_.last().dac)) a |= kAlarmDacLimit;

  bool gpsLive = gpsSeen_ && !gpsStale_ && ageMs(nowMs, nmea_.lastGoodMs()) <= kSilentMs;
  if (gpsSeen_ && !gpsLive) gpsStale_ = true;
  const Fix& f = nmea_.fix();
  if (!gpsLive) {
    a |= kAlarmGpsSilent;
  } else {
    // Only real fixes count: GPS (1), differential (2), RTK (4, 5). 6 is the receiver guessing
    // ("estimated", dead reckoning) and 0 is none.
    bool goodQuality = f.quality == 1 || f.quality == 2 || f.quality == 4 || f.quality == 5;
    // Until a GGA or GSA has arrived the fix is unknown, not missing: no alarm yet, but not "good"
    // either (gpsGood below needs a known fix). Raising GPS LOST here gave a false red flash at
    // every start-up, because other sentences arrive before the first fix report.
    bool fixKnown = f.haveGga || f.haveGsa;
    bool noFix = (f.haveGga && !goodQuality) || (f.haveGsa && f.mode < 2);
    if (noFix) a |= kAlarmGpsNoFix;
    uint8_t used = f.haveGga ? f.satsUsed : (uint8_t)nmea_.satsUsed(nowMs);
    if (fixKnown && used < 4) a |= kAlarmGpsFewSats;
  }
  // Once a second: interference watch; every 10 s: sky map.
  if (gpsLive && ageMs(nowMs, lastWatchMs_) >= 1000) {
    lastWatchMs_ = nowMs;
    double sum = 0, sum2 = 0;
    int n = 0;
    for (const auto& sat : nmea_.satellites())
      if (sat.valid && sat.snr >= 0 && nmea_.isUsed(sat, nowMs)) { sum += sat.snr; sum2 += sat.snr * sat.snr; ++n; }
    float mean = n ? (float)(sum / n) : 0, sd = n > 1 ? (float)std::sqrt(std::fmax(0.0, sum2 / n - (sum / n) * (sum / n))) : 0;
    const Fix& fx = nmea_.fix();
    bool good3d = !(a & (kAlarmGpsNoFix | kAlarmGpsFewSats)) && fx.haveGga && fx.haveLatLon && fx.mode == 3;
    if (clock_.steps() != clockSteps_) { clockSteps_ = clock_.steps(); watch_.noteTimeStep(nowMs); }
    watch_.update(nowMs, top4_, good3d, fx.latDeg, fx.lonDeg, fx.altitudeM, fx.hdop, mean, sd, n);
  }
  if (gpsLive && ageMs(nowMs, lastSkyMs_) >= 10000) {
    lastSkyMs_ = nowMs;
    for (const auto& sat : nmea_.satellites())
      if (sat.valid && sat.elevation >= 0 && sat.azimuth >= 0 && sat.snr >= 0) skyMap_.add(sat.elevation, sat.azimuth, sat.snr);
  }
  if (gpsSeen_ && watch_.spoofing(nowMs)) a |= kAlarmGpsSpoof;
  if (gpsSeen_ && watch_.jamming()) a |= kAlarmGpsSignalLoss;
  bool locked = nanoLive && nano_.locked();
  bool gpsGood = gpsLive && nmea_.fix().haveGga && !(a & (kAlarmGpsNoFix | kAlarmGpsFewSats | kAlarmGpsSpoof));
  bool gpsUnknownYet = gpsLive && !nmea_.fix().haveGga && !nmea_.fix().haveGsa;
  // Only claim "suspect" when we have heard the GPS at all; before the tap is wired it is unknown.
  if (locked && gpsSeen_ && !gpsGood && !gpsUnknownYet) a |= kAlarmRefSuspect;
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

  // Signal strength of the four strongest satellites in the fix.
  {
    int best[4] = {-1, -1, -1, -1};
    for (const auto& sat : nmea_.satellites()) {
      if (!sat.valid || !nmea_.isUsed(sat, nowMs) || sat.snr < 0) continue;
      int v = sat.snr;
      for (int i = 0; i < 4; ++i)
        if (v > best[i]) { std::swap(v, best[i]); }
    }
    int sum = 0, n = 0;
    for (int b : best) if (b >= 0) { sum += b; ++n; }
    top4_ = n == 4 ? sum / 4 : -1;
    if (top4_ >= 0) { top4Acc_ += top4_; ++top4AccN_; }
    if (ageMs(nowMs, top4LastMs_) >= 600000UL) {
      top4LastMs_ = nowMs;
      if (top4AccN_) {
        top4Hist_[top4Head_] = (int8_t)(top4Acc_ / top4AccN_);
        top4Head_ = (top4Head_ + 1) % top4Hist_.size();
        if (top4Count_ < top4Hist_.size()) ++top4Count_;
      }
      top4Acc_ = 0;
      top4AccN_ = 0;
    }
  }
  detectEvents(nowMs);
  recordSecond(nowMs);
}

void Monitor::recordSecond(uint32_t nowMs) {
  if (lastFrMs_ && ageMs(nowMs, lastFrMs_) < 1000) return;
  lastFrMs_ = nowMs;
  FrSample x{};
  int64_t u;
  bool pps;
  x.unixSec = clock_.now((int64_t)nowMs * 1000, u, pps) ? (uint32_t)(u / 1000000) : 0;
  const Fix& f = nmea_.fix();
  x.used = f.satsUsed;
  x.quality = f.quality;
  x.best4 = (int8_t)top4_;
  x.pps = nano_.haveData() && nano_.msSinceData(nowMs) <= 1500 && !(alarms_ & kAlarmNoPps);
  for (const auto& sat : nmea_.satellites()) {
    if (!sat.valid || sat.elevation < 0 || x.n >= 20) continue;
    FrSat& o = x.sats[x.n++];
    o.id = (uint16_t)((uint8_t)constellationPrefix(sat.sys)[0] << 8 | (sat.prn & 0xFF));
    o.snr = sat.snr;
    o.used = nmea_.isUsed(sat, nowMs);
  }
  recorder_.add(x);
  // Start a recording when the GPS drops out: fix lost, PPS stopped, or a sudden signal drop.
  bool gpsGood = gpsSeen_ && !(alarms_ & (kAlarmGpsSilent | kAlarmGpsNoFix | kAlarmGpsFewSats));
  uint16_t rising = alarms_ & ~frPrevAlarms_;
  if (frPrevGpsGood_ && !gpsGood) recorder_.trigger("GPS fix lost", x.unixSec);
  else if (rising & kAlarmNoPps) recorder_.trigger("PPS stopped", x.unixSec);
  else if (rising & kAlarmGpsSignalLoss) recorder_.trigger("signal drop", x.unixSec);
  frPrevGpsGood_ = gpsGood;
  frPrevAlarms_ = alarms_;
  if (recorder_.finished() != frFinishedSeen_) {
    frFinishedSeen_ = recorder_.finished();
    char b[48];
    std::snprintf(b, sizeof b, "GPS dropout recorded (%s)", recorder_.recording(0).reason);
    logEvent(nowMs, 1, b);
  }
}

void Monitor::logEvent(uint32_t nowMs, uint8_t level, const char* text) {
  int64_t u = 0;
  bool pps;
  if (clock_.now((int64_t)nowMs * 1000, u, pps)) u /= 1000000;
  else u = 0;
  events_.add(u, nowMs / 1000, level, text);
}

static const char* eventText(Headline h) {
  switch (h) {
    case Headline::Locked: return "GPSDO locked";
    case Headline::Unlocked: return "GPSDO not locked";
    case Headline::NoPps: return "No PPS from the GPS";
    case Headline::Missing10MHz: return "No 10 MHz from the oscillator";
    case Headline::RefSuspect: return "GPS fix lost, GPSDO still says locked";
    case Headline::NoData: return "GPSDO went silent";
    case Headline::WarmUp: return "GPSDO warming up after a start";
    case Headline::Holdover: return "GPSDO in hold mode";
  }
  return "?";
}

void Monitor::detectEvents(uint32_t nowMs) {
  bool gpsGood = gpsSeen_ && !(alarms_ & (kAlarmGpsSilent | kAlarmGpsNoFix | kAlarmGpsFewSats));
  const std::string& ant = nmea_.antennaStatus();
  if (!eventsArmed_) {
    // Take the first real picture as the baseline rather than logging "changes" from start-up.
    if (headline_ == Headline::NoData) return;
    int64_t u;
    bool pps;
    if (!clock_.now((int64_t)nowMs * 1000, u, pps) && nowMs < 60000) return;  // wait for UTC, up to a minute
    eventsArmed_ = true;
    prevHeadline_ = headline_;
    prevGpsGood_ = gpsGood;
    prevAntenna_ = ant;
    prevGpsdoRestarts_ = gpsdoRestarts_;
    prevGpsRestarts_ = gpsRestarts_;
    char b[48];
    std::snprintf(b, sizeof b, "Display started: %s", headlineText(headline_));
    logEvent(nowMs, 1, b);
    return;
  }
  char b[48];
  if (gpsdoRestarts_ != prevGpsdoRestarts_) { prevGpsdoRestarts_ = gpsdoRestarts_; logEvent(nowMs, 2, "GPSDO restarted (power?)"); }
  if (gpsRestarts_ != prevGpsRestarts_) { prevGpsRestarts_ = gpsRestarts_; logEvent(nowMs, 2, "GPS module restarted"); }
  // A new state must hold for 3 s before it is logged, so a flicker cannot flood the log.
  if (headline_ != headlineCandidate_) { headlineCandidate_ = headline_; headlineSinceMs_ = nowMs; }
  if (headline_ != prevHeadline_ && ageMs(nowMs, headlineSinceMs_) >= 3000) {
    uint8_t level = headline_ == Headline::Locked ? 0 : (headline_ == Headline::WarmUp || headline_ == Headline::Holdover) ? 1 : 2;
    logEvent(nowMs, level, eventText(headline_));
    prevHeadline_ = headline_;
  }
  // GPS good/bad must hold for 10 s before it is logged, so a marginal 3-4 satellite sky cannot
  // flood the log (and the flash) with flips.
  if (gpsGood != gpsGoodCandidate_) { gpsGoodCandidate_ = gpsGood; gpsGoodSinceMs_ = nowMs; }
  if (gpsGood != prevGpsGood_ && ageMs(nowMs, gpsGoodSinceMs_) >= 10000) {
    if (gpsGood) std::snprintf(b, sizeof b, "GPS fix good, %u satellites", (unsigned)nmea_.fix().satsUsed);
    else std::snprintf(b, sizeof b, "GPS fix lost or weak (%u satellites)", (unsigned)nmea_.fix().satsUsed);
    logEvent(nowMs, gpsGood ? 0 : 2, b);
    prevGpsGood_ = gpsGood;
  }
  bool spoof = alarms_ & kAlarmGpsSpoof, loss = alarms_ & kAlarmGpsSignalLoss;
  if (spoof != prevSpoof_) {
    if (spoof) {
      logEvent(nowMs, 2, watch_.positionJump() ? "GPS spoofing? Position jumped"
                         : watch_.uniformSignals() ? "GPS spoofing? Signals too uniform"
                                                   : "GPS spoofing? GPS time stepped");
    } else {
      logEvent(nowMs, 0, "GPS spoofing signs cleared");
    }
    prevSpoof_ = spoof;
  }
  if (loss != prevSignalLoss_) {
    logEvent(nowMs, loss ? 2 : 0, loss ? "GPS signals down 10 dB+ (jamming? antenna?)" : "GPS signal levels back to normal");
    prevSignalLoss_ = loss;
  }
  if (ant != prevAntenna_ && !ant.empty()) {
    std::snprintf(b, sizeof b, "GPS antenna: %s", ant.c_str());
    logEvent(nowMs, ant == "OK" ? 0 : 2, b);
    prevAntenna_ = ant;
  }
}

bool Monitor::frequencyPpb(uint32_t seconds, float& ppb) const {
  size_t w = seconds / kTrendStepS;
  if (w == 0 || trendCount_ < w + 1) return false;
  const TrendPoint& a = trend(trendCount_ - 1 - w);
  const TrendPoint& b = trend(trendCount_ - 1);
  if (!a.valid || !b.valid || !a.lockedAll || !b.lockedAll) return false;
  ppb = (float)(b.ns - a.ns) / (float)(w * kTrendStepS);
  return true;
}

bool Monitor::top4Range(int& lo, int& hi) const {
  if (!top4Count_) return false;
  lo = 127;
  hi = -1;
  for (size_t i = 0; i < top4Count_; ++i) {
    int v = top4Hist_[(top4Head_ + top4Hist_.size() - 1 - i) % top4Hist_.size()];
    lo = std::min(lo, v);
    hi = std::max(hi, v);
  }
  return true;
}

float Monitor::ticErrorNs() const {
  if (haveStatus(lastNowMs_)) return (float)status_.ticErrNs;
  const ReportLine& d = nano_.last();
  return d.filtX10 / 10.0f - GpsdoConstants::kTicOffsetNs;
}

uint32_t Monitor::earliestLockS() const {
  if (nano_.haveData() && !nano_.locked() && haveStatus(lastNowMs_) && status_.mode == 'R')
    return status_.lockCount > status_.lockTarget ? 0 : status_.lockTarget + 1 - status_.lockCount;
  if (!haveUnlockStart_ || !nano_.haveData() || nano_.locked()) return 0;
  const ReportLine& d = nano_.last();
  if (!d.hasDetail || d.tc == 0) return 0;
  uint32_t need = (uint32_t)d.tc * GpsdoConstants::kLockPpsFactor + 1;
  uint32_t done = d.time - unlockedSinceNanoTime_;
  return done >= need ? 0 : need - done;
}

}  // namespace gpsdo

namespace gpsdo {

uint32_t Monitor::warmUpRemainingS() const {
  if (!nano_.haveData() || nano_.last().status != LockStatus::WarmUp) return 0;
  uint32_t t = nano_.last().time;
  return t >= (uint32_t)GpsdoConstants::kWarmUpS ? 0 : GpsdoConstants::kWarmUpS - t;
}

int Monitor::progressPermille() const {
  if (!nano_.haveData()) return -1;
  const ReportLine& d = nano_.last();
  if (d.status == LockStatus::WarmUp) {
    uint32_t t = d.time > (uint32_t)GpsdoConstants::kWarmUpS ? GpsdoConstants::kWarmUpS : d.time;
    return (int)(t * 1000 / GpsdoConstants::kWarmUpS);
  }
  if (d.status == LockStatus::Unlocked && haveStatus(lastNowMs_) && status_.lockTarget)
    return (int)(std::min<uint32_t>(status_.lockCount, status_.lockTarget) * 1000 / status_.lockTarget);
  if (d.status == LockStatus::Unlocked && d.hasDetail && d.tc) {
    uint32_t need = (uint32_t)d.tc * GpsdoConstants::kLockPpsFactor + 1;
    uint32_t left = earliestLockS();
    return (int)((need - (left > need ? need : left)) * 1000 / need);
  }
  return -1;
}

}  // namespace gpsdo
