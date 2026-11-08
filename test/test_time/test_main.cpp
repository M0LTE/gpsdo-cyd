#include <unity.h>

#include "locator.h"
#include "monitor.h"
#include "utc_clock.h"

using namespace gpsdo;

void setUp() {}
void tearDown() {}

void test_maidenhead() {
  TEST_ASSERT_EQUAL_STRING("JN58td", maidenhead(48.14666, 11.60833, 6).c_str());  // Munich, the usual worked example
  TEST_ASSERT_EQUAL_STRING("JN58td25", maidenhead(48.14666, 11.60833, 8).c_str());
  TEST_ASSERT_EQUAL_STRING("IO92ma00", maidenhead(52.0, -1.0, 8).c_str());
  TEST_ASSERT_EQUAL_STRING("QF56od51", maidenhead(-33.8688, 151.2093, 8).c_str());
  TEST_ASSERT_EQUAL_STRING("IO", maidenhead(52.0, -1.0, 2).c_str());
}

void test_clock_from_nmea_only() {
  UtcClock c;
  uint32_t t;
  bool pps;
  TEST_ASSERT_FALSE(c.now(0, t, pps));
  c.onNmeaTime(3600, 10000);  // 01:00:00, burst started at local 10000
  TEST_ASSERT_TRUE(c.now(10500, t, pps));
  TEST_ASSERT_FALSE(pps);
  TEST_ASSERT_EQUAL_UINT32(3600000 + 500 + UtcClock::kNmeaLatencyMs, t);
  TEST_ASSERT_FALSE(c.now(16000, t, pps));  // NMEA gone stale
}

void test_clock_aligned_to_pps_mark() {
  UtcClock c;
  uint32_t t;
  bool pps;
  // PPS edge at local 9950; the NMEA burst for that second starts 170 ms later.
  c.onSecondMark(9950);
  c.onNmeaTime(3600, 10120);
  TEST_ASSERT_TRUE(c.now(10250, t, pps));
  TEST_ASSERT_TRUE(pps);
  TEST_ASSERT_EQUAL_UINT32(3600000 + 300, t);
  // Next second, before its NMEA arrives: the mark moves on, the second comes from the old NMEA.
  c.onSecondMark(10950);
  TEST_ASSERT_TRUE(c.now(11000, t, pps));
  TEST_ASSERT_EQUAL_UINT32(3601000 + 50, t);
}

void test_clock_wraps_at_midnight() {
  UtcClock c;
  uint32_t t;
  bool pps;
  c.onSecondMark(1000);
  c.onNmeaTime(86399, 1100);
  TEST_ASSERT_TRUE(c.now(2200, t, pps));
  TEST_ASSERT_EQUAL_UINT32(200, t);
}

void test_monitor_feeds_clock_and_warmup() {
  Monitor m;
  m.onNanoLine("12\t-435\t30196\t11.5\tWarmUp\t27\t530\t300\t1\t20932\t1.5\t", 5000, 4990);
  m.onGpsLine("$GNRMC,132000.00,A,5200.00000,N,00100.00000,W,0.01,,081126,,,A,V*3D", 5150, 5100);
  uint32_t t;
  bool pps;
  TEST_ASSERT_TRUE(m.clock().now(5200, t, pps));
  TEST_ASSERT_TRUE(pps);
  TEST_ASSERT_EQUAL_UINT32((13 * 3600 + 20 * 60) * 1000 + 210, t);
  TEST_ASSERT_EQUAL_UINT32(288, m.warmUpRemainingS());
  TEST_ASSERT_EQUAL_INT(40, m.progressPermille());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_maidenhead);
  RUN_TEST(test_clock_from_nmea_only);
  RUN_TEST(test_clock_aligned_to_pps_mark);
  RUN_TEST(test_clock_wraps_at_midnight);
  RUN_TEST(test_monitor_feeds_clock_and_warmup);
  return UNITY_END();
}
