#include <unity.h>

#include <fstream>
#include <string>

#include "arduino_report.h"

using namespace gpsdo;

void setUp() {}
void tearDown() {}

// A real line from this unit (Rev 3.4): rolling tail shows an old "****" from stored history.
static const char* kTailUnlocked =
    "8753\t21\t30246\t44.0\tLocked\t6\t5209\t300\t150\t25191\t34.1\t1102\t4992\t30228\t43.7\t****\t";

void test_data_line_ignores_tail_status() {
  ParsedLine p = parseReportLine(kTailUnlocked);
  TEST_ASSERT_EQUAL(LineKind::Data, p.kind);
  TEST_ASSERT_EQUAL(LockStatus::Locked, p.data.status);
  TEST_ASSERT_EQUAL_UINT32(8753, p.data.time);
  TEST_ASSERT_EQUAL_INT32(21, p.data.ns);
  TEST_ASSERT_EQUAL_UINT16(30246, p.data.dac);
  TEST_ASSERT_EQUAL_FLOAT(44.0f, p.data.ocxoC);
  TEST_ASSERT_TRUE(p.data.hasDetail);
  TEST_ASSERT_EQUAL_INT32(6, p.data.diffNs);
  TEST_ASSERT_EQUAL_INT32(5209, p.data.filtX10);
  TEST_ASSERT_EQUAL_UINT16(300, p.data.tc);
  TEST_ASSERT_EQUAL_UINT16(150, p.data.filt);
  TEST_ASSERT_EQUAL_INT32(25191, p.data.timer1);
  TEST_ASSERT_EQUAL_FLOAT(34.1f, p.data.ambientC);
}

void test_v371_duty_cycle_layout() {
  ParsedLine p = parseReportLine("\t128\t200\t-5\t40000\t39.5\t****\t-3\t-40\t32\t16\t25000\t21.0\t\r");
  TEST_ASSERT_EQUAL(LineKind::Data, p.kind);
  TEST_ASSERT_EQUAL(LockStatus::Unlocked, p.data.status);
  TEST_ASSERT_EQUAL_UINT32(200, p.data.time);
  TEST_ASSERT_EQUAL_INT32(-5, p.data.ns);
  TEST_ASSERT_EQUAL_UINT16(32, p.data.tc);
}

void test_less_info_mode() {
  ParsedLine p = parseReportLine("500\t12\t30000\t44.1\tLocked\t");
  TEST_ASSERT_EQUAL(LineKind::Data, p.kind);
  TEST_ASSERT_FALSE(p.data.hasDetail);
}

void test_missing_10mhz() {
  ParsedLine p = parseReportLine("77\tMissing 10MHz?\t30000\t44.1\tWarmUp\t0\t0\t300\t150\t0\t20.0\t");
  TEST_ASSERT_EQUAL(LineKind::Data, p.kind);
  TEST_ASSERT_TRUE(p.data.missing10MHz);
  TEST_ASSERT_EQUAL(LockStatus::WarmUp, p.data.status);
}

void test_revision_in_tail() {
  ParsedLine p = parseReportLine(
      "8800\t-4\t30242\t43.9\tLocked\t-20\t5179\t300\t150\t25190\t34.2\tRev. 3.4 paulv mods  ID:0\t");
  TEST_ASSERT_EQUAL(LineKind::Data, p.kind);
  TEST_ASSERT_EQUAL_STRING("Rev. 3.4 paulv mods  ID:0", p.text.c_str());
}

void test_other_kinds() {
  TEST_ASSERT_EQUAL(LineKind::NoPps, parseReportLine(" No PPS").kind);
  TEST_ASSERT_EQUAL(LineKind::Empty, parseReportLine("\r").kind);
  TEST_ASSERT_EQUAL(LineKind::Banner, parseReportLine("Arduino GPSDO with 1ns TIC by Lars Walenius").kind);
  TEST_ASSERT_EQUAL(LineKind::Header,
                    parseReportLine("time\tns\tdac\tOCXO\tstatus\tdiff_ns filtX10\ttc\tfilt\ttimer1\tambient").kind);
  TEST_ASSERT_EQUAL(LineKind::Other, parseReportLine("No valid command").kind);
  // Partial line (cut off by a reset or a dropped byte) must not become data.
  TEST_ASSERT_EQUAL(LineKind::Other, parseReportLine("8753\t21\t302").kind);
  TEST_ASSERT_EQUAL(LineKind::Other, parseReportLine("53\t21\t30246\t44.0\tLock").kind);
}

static ParsedLine data(uint32_t t, LockStatus s) {
  ParsedLine p;
  p.kind = LineKind::Data;
  p.data.time = t;
  p.data.status = s;
  return p;
}

void test_tracker_lock_transitions() {
  ReportTracker tr;
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, tr.msSinceData(0));
  tr.onLine(data(100, LockStatus::Locked), 1000);  // started while locked
  TEST_ASSERT_FALSE(tr.lockedSinceKnown());
  tr.onLine(data(110, LockStatus::Locked), 11000);
  TEST_ASSERT_EQUAL_UINT32(10, tr.secondsLocked());
  tr.onLine(data(111, LockStatus::Unlocked), 12000);
  TEST_ASSERT_EQUAL_UINT32(1, tr.lockLosses());
  TEST_ASSERT_EQUAL_UINT32(12000, tr.lastLockLossMs());
  TEST_ASSERT_EQUAL_UINT32(0, tr.secondsLocked());
  tr.onLine(data(112, LockStatus::Locked), 13000);
  tr.onLine(data(150, LockStatus::Locked), 51000);
  TEST_ASSERT_TRUE(tr.lockedSinceKnown());
  TEST_ASSERT_EQUAL_UINT32(38, tr.secondsLocked());
  TEST_ASSERT_EQUAL_UINT32(4000, tr.msSinceData(55000));
}

void test_tracker_no_pps_and_restart() {
  ReportTracker tr;
  tr.onLine(data(500, LockStatus::Locked), 0);
  ParsedLine np;
  np.kind = LineKind::NoPps;
  tr.onLine(np, 1500);
  TEST_ASSERT_EQUAL_UINT32(500, tr.msSinceNoPps(2000));
  // Nano reset: header re-printed, then time starts again.
  tr.onLine(parseReportLine("Arduino GPSDO with 1ns TIC by Lars Walenius"), 3000);
  tr.onLine(data(1, LockStatus::WarmUp), 4000);
  TEST_ASSERT_EQUAL_UINT32(1, tr.nanoRestarts());
  TEST_ASSERT_EQUAL_UINT32(1, tr.lockLosses());
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, tr.msSinceNoPps(5000));
}

void test_dac_warning() {
  TEST_ASSERT_TRUE(ReportTracker::dacWarning(2999));
  TEST_ASSERT_FALSE(ReportTracker::dacWarning(3000));
  TEST_ASSERT_FALSE(ReportTracker::dacWarning(62535));
  TEST_ASSERT_TRUE(ReportTracker::dacWarning(62536));
}

// Replays the real capture (lines prefixed with an ISO receive time) and checks every line parses.
void test_real_capture() {
  std::ifstream in(CAPTURE_DIR "/arduino-20261108.log");
  if (!in) TEST_IGNORE_MESSAGE("capture not present");
  ReportTracker tr;
  std::string line;
  uint32_t n = 0, dataLines = 0, lastTime = 0;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    size_t sp = line.find(' ');
    std::string body = sp == std::string::npos ? "" : line.substr(sp + 1);
    ParsedLine p = parseReportLine(body);
    tr.onLine(p, n * 1000);
    ++n;
    if (p.kind == LineKind::Data) {
      ++dataLines;
      if (dataLines > 1) TEST_ASSERT_EQUAL_UINT32(lastTime + 1, p.data.time);
      lastTime = p.data.time;
    }
  }
  TEST_ASSERT_GREATER_THAN_UINT32(1000, dataLines);
  TEST_ASSERT_EQUAL_STRING("Rev. 3.4 paulv mods  ID:0", tr.revision().c_str());
  TEST_ASSERT_EQUAL_UINT32(0, tr.lockLosses());
  TEST_ASSERT_TRUE(tr.locked());
}

void test_status_line() {
  ParsedLine p = parseReportLine("$GPSDO,1,4,0,1500,96,48,0,4,W,0,30196,70*24\r");
  TEST_ASSERT_EQUAL(LineKind::Status, p.kind);
  TEST_ASSERT_EQUAL_UINT32(4, p.status.time);
  TEST_ASSERT_EQUAL_UINT32(1500, p.status.lockTarget);
  TEST_ASSERT_EQUAL_INT32(96, p.status.ticErrNs);
  TEST_ASSERT_EQUAL_INT32(48, p.status.freqErrNs);
  TEST_ASSERT_EQUAL('W', p.status.mode);
  TEST_ASSERT_EQUAL_UINT32(70, p.status.restarts);
  // Damaged: wrong checksum, or cut short.
  TEST_ASSERT_EQUAL(LineKind::Other, parseReportLine("$GPSDO,1,4,0,1500,96,48,0,4,W,0,30196,70*25").kind);
  TEST_ASSERT_EQUAL(LineKind::Other, parseReportLine("$GPSDO,1,4,0,1500,96,48,0,4,W").kind);
}

void test_first_boot_capture_with_status() {
  std::ifstream in(CAPTURE_DIR "/arduino-cyd1-firstboot-20261108.log");
  if (!in) TEST_IGNORE_MESSAGE("capture not present");
  std::string line;
  int status = 0, data = 0;
  while (std::getline(in, line)) {
    size_t sp = line.find(' ');
    if (sp == std::string::npos) continue;
    ParsedLine p = parseReportLine(line.substr(sp + 1));
    if (p.kind == LineKind::Status) ++status;
    if (p.kind == LineKind::Data) ++data;
  }
  TEST_ASSERT_EQUAL(data, status);
  TEST_ASSERT_TRUE(status >= 15);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_data_line_ignores_tail_status);
  RUN_TEST(test_v371_duty_cycle_layout);
  RUN_TEST(test_less_info_mode);
  RUN_TEST(test_missing_10mhz);
  RUN_TEST(test_revision_in_tail);
  RUN_TEST(test_other_kinds);
  RUN_TEST(test_tracker_lock_transitions);
  RUN_TEST(test_tracker_no_pps_and_restart);
  RUN_TEST(test_dac_warning);
  RUN_TEST(test_real_capture);
  RUN_TEST(test_status_line);
  RUN_TEST(test_first_boot_capture_with_status);
  return UNITY_END();
}
