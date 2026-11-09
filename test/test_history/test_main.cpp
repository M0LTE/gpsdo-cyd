#include <unity.h>

#include <cstdio>
#include <fstream>
#include <string>

#include "monitor.h"
#include "web_json.h"

using namespace gpsdo;

void setUp() {}
void tearDown() {}

static Monitor replayCapture() {
  Monitor m;
  std::ifstream in(CAPTURE_DIR "/arduino-20261108.log");
  std::string line;
  uint32_t n = 0;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    size_t sp = line.find(' ');
    if (sp == std::string::npos) continue;
    m.onNanoLine(line.substr(sp + 1), ++n * 1000);
  }
  return m;
}

void test_history_from_real_capture() {
  Monitor m = replayCapture();
  const NanoHistory& h = m.history();
  // 25 minutes of reports cover the 300 s replay cycle several times.
  TEST_ASSERT_EQUAL_INT(144, h.seen(true));
  TEST_ASSERT_EQUAL_INT(144, h.seen(false));
  TEST_ASSERT_TRUE(h.indexKnown(true));
  TEST_ASSERT_TRUE(h.indexKnown(false));
  auto five = h.ordered(true);
  auto three = h.ordered(false);
  TEST_ASSERT_TRUE(five.size() > 20);
  TEST_ASSERT_TRUE(three.size() > 50);
  int locked = 0;
  for (const auto& e : three) {
    TEST_ASSERT_TRUE(e.dac > 20000 && e.dac < 40000);
    TEST_ASSERT_TRUE(e.tempC > 20 && e.tempC < 60);
    if (e.lock == 'L') ++locked;
  }
  TEST_ASSERT_TRUE(locked > 10);
  std::printf("history: %u five-minute and %u three-hour entries, %d of them locked\n", (unsigned)five.size(),
              (unsigned)three.size(), locked);
}

void test_frequency_estimate() {
  Monitor m;
  // Phase drifting +1 ns every 10 s while locked = 0.1 ppb.
  for (uint32_t t = 0; t < 2000; ++t) {
    char b[120];
    std::snprintf(b, sizeof b, "%u\t%d\t30000\t44.0\tLocked\t0\t5000\t300\t150\t25191\t34.1\t", t, (int)(t / 10));
    m.onNanoLine(b, t * 1000);
  }
  float ppb;
  TEST_ASSERT_TRUE(m.frequencyPpb(300, ppb));
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.1f, ppb);
  // The 30-minute figure must be available once 30 minutes are stored (it never was: 150 blocks
  // only span 149 gaps).
  TEST_ASSERT_TRUE(m.frequencyPpb(1800, ppb));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.1f, ppb);
}

void test_events_and_antenna() {
  Monitor m;
  char b[120];
  for (uint32_t t = 0; t < 5; ++t) {
    std::snprintf(b, sizeof b, "%u\t1\t30000\t44.0\tLocked\t0\t5000\t300\t150\t25191\t34.1\t", t);
    m.onNanoLine(b, 60000 + t * 1000);
  }
  TEST_ASSERT_EQUAL_INT(1, m.events().count());  // "Display started: LOCKED"
  // An unlock is logged once it has held for 3 s.
  for (uint32_t t = 5; t <= 9; ++t) {
    std::snprintf(b, sizeof b, "%u\t1\t30000\t44.0\t****\t0\t5000\t300\t150\t25191\t34.1\t", t);
    m.onNanoLine(b, 60000 + t * 1000);
    if (t == 6) TEST_ASSERT_EQUAL_INT(1, m.events().count());
  }
  TEST_ASSERT_EQUAL_INT(2, m.events().count());
  TEST_ASSERT_EQUAL_STRING("GPSDO not locked", m.events().newest(0).text);
  TEST_ASSERT_EQUAL_UINT8(2, m.events().newest(0).level);
  m.onGpsLine("$GNTXT,01,01,02,ANTSTATUS=SHORT*73", 69100);
  m.tick(69200);
  TEST_ASSERT_EQUAL_STRING("SHORT", m.gps().antennaStatus().c_str());
  TEST_ASSERT_EQUAL_STRING("GPS antenna: short circuit", m.events().newest(0).text);
  // Saving and restoring the log keeps it intact.
  static uint8_t blob[EventLog::kBlobSize];
  m.events().toBlob(blob);
  EventLog copy;
  TEST_ASSERT_TRUE(copy.fromBlob(blob, sizeof blob));
  TEST_ASSERT_EQUAL_INT(m.events().count(), copy.count());
  TEST_ASSERT_EQUAL_STRING(m.events().newest(1).text, copy.newest(1).text);
}

void test_web_json() {
  Monitor m = replayCapture();
  std::string h = historyJson(m), t = trendsJson(m), e = eventsJson(m), a = accuracyJson(m);
  TEST_ASSERT_TRUE(h.find("\"three_hour\":[[") != std::string::npos);
  TEST_ASSERT_TRUE(t.find("\"points\":[[") != std::string::npos);
  TEST_ASSERT_EQUAL('[', e[0]);
  TEST_ASSERT_TRUE(a.find("\"1800\":") != std::string::npos);
  // Every document must be valid JSON; checked by brackets balancing here, and by the page in practice.
  for (const std::string* d : {&h, &t, &e, &a}) {
    int depth = 0;
    for (char c : *d) depth += (c == '[' || c == '{') - (c == ']' || c == '}');
    TEST_ASSERT_EQUAL(0, depth);
  }
  std::printf("history json %u bytes, trends json %u bytes\n", (unsigned)h.size(), (unsigned)t.size());
}

void test_tick_before_latest_gps_is_not_silence() {
  // The bug behind the "GPS fix lost / good" flapping: a once-a-second check with a clock reading
  // taken before GPS lines processed in the same pass must not see the GPS as 49 days silent.
  Monitor m;
  m.onNanoLine("100\t1\t30000\t44.0\tLocked\t0\t5000\t300\t150\t25191\t34.1\t", 60000);
  m.onGpsLine("$GNGGA,132000.00,5200.00000,N,00100.00000,W,1,09,0.95,52.3,M,47.0,M,,*64", 61050);
  m.onGpsLine("$GNGSA,A,3,05,13,15,18,,,,,,,,,1.60,0.95,1.29,1*0A", 61060);
  m.tick(61000);  // earlier than the GPS lines above
  TEST_ASSERT_FALSE(m.alarms() & kAlarmGpsSilent);
  TEST_ASSERT_FALSE(m.alarms() & kAlarmGpsNoFix);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_history_from_real_capture);
  RUN_TEST(test_frequency_estimate);
  RUN_TEST(test_events_and_antenna);
  RUN_TEST(test_web_json);
  RUN_TEST(test_tick_before_latest_gps_is_not_silence);
  return UNITY_END();
}
