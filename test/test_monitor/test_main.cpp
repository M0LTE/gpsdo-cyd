#include <unity.h>

#include <cstdio>
#include <string>

#include "line_assembler.h"
#include "monitor.h"
#include "state_json.h"

using namespace gpsdo;

void setUp() {}
void tearDown() {}

static std::string nanoLine(uint32_t t, const char* status, int filtX10 = 5200, int tc = 300) {
  char b[160];
  std::snprintf(b, sizeof b, "%u\t10\t30246\t44.0\t%s\t3\t%d\t%d\t150\t25191\t34.1\t", t, status, filtX10, tc);
  return b;
}

static const char* kGga = "$GNGGA,132000.00,5200.00000,N,00100.00000,W,1,09,0.95,52.3,M,47.0,M,,*64";
static const char* kGsa = "$GNGSA,A,3,05,13,15,18,,,,,,,,,1.60,0.95,1.29,1*0A";
static const char* kGgaNoFix = "$GNGGA,132000.00,,,,,0,00,99.99,,,,,,*78";

void test_line_assembler() {
  LineAssembler a(8);
  std::string in = "abc\r\n0123456789\nok\n";
  int lines = 0;
  for (char c : in) {
    if (!a.push(c)) continue;
    ++lines;
    if (lines == 1) TEST_ASSERT_EQUAL_STRING("abc", a.line().c_str());
    if (lines == 2) { TEST_ASSERT_EQUAL_STRING("01234567", a.line().c_str()); TEST_ASSERT_TRUE(a.truncated()); }
    if (lines == 3) { TEST_ASSERT_EQUAL_STRING("ok", a.line().c_str()); TEST_ASSERT_FALSE(a.truncated()); }
  }
  TEST_ASSERT_EQUAL(3, lines);
}

void test_no_data_then_locked_without_gps() {
  Monitor m;
  m.tick(0);
  TEST_ASSERT_EQUAL(Headline::NoData, m.headline());
  m.onNanoLine(nanoLine(100, "Locked"), 1000);
  TEST_ASSERT_EQUAL(Headline::Locked, m.headline());
  // GPS not wired yet: not suspect, but not "reference OK" either.
  TEST_ASSERT_FALSE(m.alarms() & kAlarmRefSuspect);
  TEST_ASSERT_FALSE(m.referenceOk());
  m.tick(4100);
  TEST_ASSERT_EQUAL(Headline::NoData, m.headline());
}

void test_reference_ok_needs_gps_fix() {
  Monitor m;
  m.onGpsLine(kGga, 900);
  m.onGpsLine(kGsa, 900);
  m.onNanoLine(nanoLine(100, "Locked"), 1000);
  TEST_ASSERT_TRUE(m.referenceOk());
  TEST_ASSERT_EQUAL(Headline::Locked, m.headline());
  // GPS loses its fix while the GPSDO still says Locked: the failure seen on 8 Nov 2026.
  m.onGpsLine(kGgaNoFix, 2000);
  m.onNanoLine(nanoLine(101, "Locked"), 2000);
  TEST_ASSERT_FALSE(m.referenceOk());
  TEST_ASSERT_TRUE(m.alarms() & kAlarmRefSuspect);
  TEST_ASSERT_EQUAL(Headline::RefSuspect, m.headline());
  // GPS goes silent altogether.
  m.onNanoLine(nanoLine(105, "Locked"), 6000);
  TEST_ASSERT_TRUE(m.alarms() & kAlarmGpsSilent);
  TEST_ASSERT_EQUAL(Headline::RefSuspect, m.headline());
}

void test_no_pps_headline() {
  Monitor m;
  m.onNanoLine(nanoLine(100, "Locked"), 1000);
  m.onNanoLine(" No PPS", 2000);
  m.tick(2100);
  TEST_ASSERT_EQUAL(Headline::NoPps, m.headline());
}

void test_no_pps_after_boot_without_data() {
  // GPSDO just powered: the Nano prints "No PPS" but no report lines yet (seen 2026-11-08).
  Monitor m;
  m.onNanoLine(" No PPS", 1000);
  m.tick(1500);
  TEST_ASSERT_EQUAL(Headline::NoPps, m.headline());
  TEST_ASSERT_FALSE(m.alarms() & kAlarmNanoSilent);
  TEST_ASSERT_FALSE(m.referenceOk());
  m.tick(5000);
  TEST_ASSERT_EQUAL(Headline::NoData, m.headline());
}

void test_earliest_lock_estimate() {
  Monitor m;
  m.onNanoLine(nanoLine(100, "Locked"), 0);
  m.onNanoLine(nanoLine(101, "****"), 1000);
  m.onNanoLine(nanoLine(201, "****"), 2000);
  TEST_ASSERT_EQUAL(Headline::Unlocked, m.headline());
  TEST_ASSERT_EQUAL_UINT32(300 * 5 + 1 - 100, m.earliestLockS());
  // TIC strays out of limits: the firmware's counter restarts, so does the estimate.
  m.onNanoLine(nanoLine(202, "****", 6200), 3000);
  TEST_ASSERT_EQUAL_UINT32(300 * 5 + 1, m.earliestLockS());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.0f, m.ticErrorNs());
}

void test_trend_ring() {
  Monitor m;
  for (uint32_t t = 0; t < 4000; ++t) m.onNanoLine(nanoLine(t, "Locked"), t * 1000);
  TEST_ASSERT_EQUAL(Monitor::kTrendPoints, m.trendCount());
  TEST_ASSERT_EQUAL_UINT16(30246, m.trend(0).dac);
}

void test_state_json() {
  Monitor m;
  m.onNanoLine(nanoLine(100, "Locked"), 1000);
  std::string j = stateJson(m, 1000);
  TEST_ASSERT_TRUE(j.find("\"state\":\"LOCKED\"") != std::string::npos);
  TEST_ASSERT_TRUE(j.find("\"reference_ok\":false") != std::string::npos);
  TEST_ASSERT_TRUE(j.find("\"dac\":30246") != std::string::npos);
  TEST_ASSERT_TRUE(j.find("\"gps_seen\":false") != std::string::npos);
  TEST_ASSERT_TRUE(j.find("\"alarms\":[]") != std::string::npos);
  m.onGpsLine(kGgaNoFix, 2000);
  m.onNanoLine(nanoLine(101, "Locked"), 2000);
  j = stateJson(m, 2000);
  TEST_ASSERT_TRUE(j.find("\"state\":\"GPS LOST\"") != std::string::npos);
  TEST_ASSERT_TRUE(j.find("reference_suspect") != std::string::npos);
  TEST_ASSERT_TRUE(j.front() == '{' && j.back() == '}');
  std::string topic, payload;
  TEST_ASSERT_TRUE(Discovery::entity(0, "gpsdo", "gpsdo_cyd", topic, payload));
  TEST_ASSERT_EQUAL_STRING("homeassistant/binary_sensor/gpsdo_cyd/reference_ok/config", topic.c_str());
  TEST_ASSERT_FALSE(Discovery::entity(Discovery::count(), "gpsdo", "x", topic, payload));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_line_assembler);
  RUN_TEST(test_no_data_then_locked_without_gps);
  RUN_TEST(test_reference_ok_needs_gps_fix);
  RUN_TEST(test_no_pps_headline);
  RUN_TEST(test_no_pps_after_boot_without_data);
  RUN_TEST(test_earliest_lock_estimate);
  RUN_TEST(test_trend_ring);
  RUN_TEST(test_state_json);
  return UNITY_END();
}
