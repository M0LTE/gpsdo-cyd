// Parser for the per-second report printed by the Lars/PaulV GPSDO Arduino at 9600 baud.
//
// The data line layout, as printed by printDataToSerial():
//   [duty_cycle] time ns dac OCXO status [diff_ns filtX10 tc filt timer1 ambient] [rolling tail]
// duty_cycle exists only in 3.7.x builds (it starts with a tab). The rolling tail is a slow dump
// of stored history that can itself contain "Locked"/"****" words, so it must be ignored.
// The header row does not line up with the data (one label covers two columns) and is only
// printed at startup or on request, so columns are located by anchoring on the live status word.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace gpsdo {

enum class LockStatus : uint8_t { Unknown, Locked, Unlocked, Hold, WarmUp };

struct ReportLine {
  uint32_t time = 0;        // seconds since the Nano started
  bool missing10MHz = false; // "Missing 10MHz?" printed in place of ns
  int32_t ns = 0;           // TIC reading, ns
  uint16_t dac = 0;
  float ocxoC = 0;
  LockStatus status = LockStatus::Unknown;
  bool hasDetail = false;   // false when the Nano is in "less info" mode
  int32_t diffNs = 0;
  int32_t filtX10 = 0;      // filtered TIC x10
  uint16_t tc = 0;          // time constant, s
  uint16_t filt = 0;        // prefilter, s
  int32_t timer1 = 0;
  float ambientC = 0;
};

enum class LineKind : uint8_t {
  Empty,
  Data,          // a per-second report line, see ReportLine
  NoPps,         // " No PPS"
  Banner,        // "Arduino GPSDO with 1ns TIC ..." (startup)
  Revision,      // "Rev. x.y ..." on its own (startup)
  Header,        // the column header row (startup, f1)
  Other,         // help text, command replies and anything else
};

struct ParsedLine {
  LineKind kind = LineKind::Empty;
  ReportLine data;          // valid when kind == Data
  std::string text;         // Revision/Data: revision string if present; Other: the trimmed line
  std::vector<std::string> tail;  // Data: the fields after the per-second values (rolling history replay)
};

// Parse one line (without its line ending; a trailing '\r' is tolerated).
ParsedLine parseReportLine(const std::string& line);

const char* lockStatusName(LockStatus s);

// Tracks the derived lock information from successive parsed lines.
// Times are in milliseconds from any monotonic clock, supplied by the caller.
class ReportTracker {
public:
  void onLine(const ParsedLine& p, uint32_t nowMs);

  bool haveData() const { return haveData_; }
  const ReportLine& last() const { return last_; }
  const std::string& revision() const { return revision_; }

  // Milliseconds since the last valid data line (UINT32_MAX if none yet).
  uint32_t msSinceData(uint32_t nowMs) const;
  // Milliseconds since the last " No PPS" (UINT32_MAX if none since the last data line).
  uint32_t msSinceNoPps(uint32_t nowMs) const;

  bool locked() const { return haveData_ && last_.status == LockStatus::Locked; }
  // Seconds of Nano time locked since the last unlock->lock transition we saw; 0 if not locked.
  // If the display started while already locked, counts from the first line seen
  // (lockedSinceKnown() is then false).
  uint32_t secondsLocked() const;
  bool lockedSinceKnown() const { return lockStartKnown_; }
  uint32_t lockLosses() const { return lockLosses_; }
  uint32_t lastLockLossMs() const { return lastLockLossMs_; }  // valid if lockLosses() > 0
  uint32_t nanoRestarts() const { return restarts_; }           // time counter went backwards

  static bool dacWarning(uint16_t dac) { return dac < 3000 || dac > 62535; }

private:
  bool haveData_ = false;
  ReportLine last_;
  std::string revision_;
  uint32_t lastDataMs_ = 0;
  bool noPpsSinceData_ = false;
  uint32_t lastNoPpsMs_ = 0;
  bool wasLocked_ = false;
  uint32_t lockStartTime_ = 0;  // Nano time of the lock start
  bool lockStartKnown_ = false;
  uint32_t lockLosses_ = 0;
  uint32_t lastLockLossMs_ = 0;
  uint32_t restarts_ = 0;
};

}  // namespace gpsdo
