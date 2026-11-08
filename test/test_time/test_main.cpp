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

static const int64_t kDay = daysFromCivil(2026, 11, 8) * 86400;  // 2026-11-08 00:00:00 UTC
static const int64_t kSec = 1000000;

void test_days_from_civil() {
  TEST_ASSERT_EQUAL_INT64(0, daysFromCivil(1970, 1, 1));
  TEST_ASSERT_EQUAL_INT64(1762560000LL, daysFromCivil(2025, 11, 8) * 86400);
}

void test_clock_from_nmea_only() {
  UtcClock c;
  int64_t t;
  bool pps;
  TEST_ASSERT_FALSE(c.now(0, t, pps));
  c.onNmeaTime(kDay + 3600, 10 * kSec);  // 01:00:00 sentence started at local 10 s
  TEST_ASSERT_TRUE(c.now(10 * kSec + 500000, t, pps));
  TEST_ASSERT_FALSE(pps);
  TEST_ASSERT_EQUAL_INT64((kDay + 3600) * kSec + 500000 + UtcClock::kNmeaLatencyUs, t);
  TEST_ASSERT_FALSE(c.now(16 * kSec, t, pps));  // NMEA gone stale
}

void test_clock_pairs_sentence_with_mark() {
  UtcClock c;
  int64_t t;
  bool pps;
  // PPS edge at local 9.950 s; the RMC for that second starts 170 ms later.
  c.onSecondMark(9950000);
  c.onNmeaTime(kDay + 3600, 10120000);
  TEST_ASSERT_TRUE(c.now(10250000, t, pps));
  TEST_ASSERT_TRUE(pps);
  TEST_ASSERT_EQUAL_INT64((kDay + 3600) * kSec + 300000, t);
  // Next mark, before its sentence arrives: time moves on from the mark.
  c.onSecondMark(10950000);
  TEST_ASSERT_TRUE(c.now(11000000, t, pps));
  TEST_ASSERT_EQUAL_INT64((kDay + 3601) * kSec + 50000, t);
  // A sentence that runs late (850 ms after its mark) still pairs with the right second.
  c.onSecondMark(11950000);
  c.onNmeaTime(kDay + 3602, 12800000);
  TEST_ASSERT_TRUE(c.now(12900000, t, pps));
  TEST_ASSERT_EQUAL_INT64((kDay + 3602) * kSec + 950000, t);
}

void test_clock_no_gap_between_bursts() {
  // The case that broke the first version: NMEA every second with no quiet gap. Only the
  // sentence's own start matters now.
  UtcClock c;
  int64_t t;
  bool pps;
  for (int i = 0; i < 30; ++i) {
    c.onSecondMark(i * kSec);
    c.onNmeaTime(kDay + i, i * kSec + 120000);
  }
  TEST_ASSERT_TRUE(c.now(29 * kSec + 400000, t, pps));
  TEST_ASSERT_TRUE(pps);
  TEST_ASSERT_EQUAL_INT64((kDay + 29) * kSec + 400000, t);
}

void test_monitor_feeds_clock_and_warmup() {
  Monitor m;
  m.onNanoLine("12\t-435\t30196\t11.5\tWarmUp\t27\t530\t300\t1\t20932\t1.5\t", 5000, 4990000);
  m.onGpsLine("$GNRMC,132000.00,A,5200.00000,N,00100.00000,W,0.01,,081126,,,A,V*3D", 5150, 5100000);
  int64_t t;
  bool pps;
  TEST_ASSERT_TRUE(m.clock().now(5200000, t, pps));
  TEST_ASSERT_TRUE(pps);
  TEST_ASSERT_EQUAL_INT64((kDay + 13 * 3600 + 20 * 60) * kSec + 210000 + Monitor::kNanoPrintDelayUs, t);
  TEST_ASSERT_EQUAL_UINT32(288, m.warmUpRemainingS());
  TEST_ASSERT_EQUAL_INT(40, m.progressPermille());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_maidenhead);
  RUN_TEST(test_days_from_civil);
  RUN_TEST(test_clock_from_nmea_only);
  RUN_TEST(test_clock_pairs_sentence_with_mark);
  RUN_TEST(test_clock_no_gap_between_bursts);
  RUN_TEST(test_monitor_feeds_clock_and_warmup);
  return UNITY_END();
}
