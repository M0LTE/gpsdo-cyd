#include <unity.h>

#include <cstdio>
#include <fstream>
#include <string>

#include "monitor.h"

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
  TEST_ASSERT_FALSE(m.frequencyPpb(3600, ppb));  // only 30 minutes are kept
}

void test_events_and_antenna() {
  Monitor m;
  char b[120];
  for (uint32_t t = 0; t < 5; ++t) {
    std::snprintf(b, sizeof b, "%u\t1\t30000\t44.0\tLocked\t0\t5000\t300\t150\t25191\t34.1\t", t);
    m.onNanoLine(b, t * 1000);
  }
  TEST_ASSERT_EQUAL_INT(1, m.events().count());  // "Display started: LOCKED"
  m.onNanoLine("5\t1\t30000\t44.0\t****\t0\t5000\t300\t150\t25191\t34.1\t", 5000);
  TEST_ASSERT_EQUAL_INT(2, m.events().count());
  TEST_ASSERT_EQUAL_STRING("GPSDO not locked", m.events().newest(0).text);
  TEST_ASSERT_EQUAL_UINT8(2, m.events().newest(0).level);
  m.onGpsLine("$GNTXT,01,01,02,ANTSTATUS=SHORT*73", 5100);
  m.tick(5200);
  TEST_ASSERT_EQUAL_STRING("SHORT", m.gps().antennaStatus().c_str());
  TEST_ASSERT_EQUAL_STRING("GPS antenna: SHORT", m.events().newest(0).text);
  // Saving and restoring the log keeps it intact.
  static uint8_t blob[EventLog::kBlobSize];
  m.events().toBlob(blob);
  EventLog copy;
  TEST_ASSERT_TRUE(copy.fromBlob(blob, sizeof blob));
  TEST_ASSERT_EQUAL_INT(m.events().count(), copy.count());
  TEST_ASSERT_EQUAL_STRING(m.events().newest(1).text, copy.newest(1).text);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_history_from_real_capture);
  RUN_TEST(test_frequency_estimate);
  RUN_TEST(test_events_and_antenna);
  return UNITY_END();
}
