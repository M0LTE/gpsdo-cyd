#include <unity.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "interference.h"
#include "skymap.h"
#include "stability.h"
#include "tempfit.h"
#include "utc_clock.h"

using namespace gpsdo;

void setUp() {}
void tearDown() {}

void test_adev_basics() {
  Stability s;
  double a;
  TEST_ASSERT_FALSE(s.adev(1, a));
  // A steady frequency offset (phase ramp) has zero Allan deviation.
  for (uint32_t t = 0; t < 100; ++t) s.add(t, (int32_t)(t * 3), true);
  TEST_ASSERT_TRUE(s.adev(1, a));
  TEST_ASSERT_FLOAT_WITHIN(1e-15, 0.0, a);
  // Phase alternating +-10 ns: second difference 40 ns, sigma(1 s) = sqrt(1600/2) ns.
  Stability t2;
  for (uint32_t t = 0; t < 100; ++t) t2.add(t, (t & 1) ? -10 : 10, true);
  TEST_ASSERT_TRUE(t2.adev(1, a));
  TEST_ASSERT_FLOAT_WITHIN(1e-12, std::sqrt(800.0) * 1e-9, a);
  // A gap in the seconds or an unlocked second starts the run again.
  t2.add(200, 0, true);
  TEST_ASSERT_EQUAL(1, t2.samples());
  t2.add(201, 0, false);
  TEST_ASSERT_EQUAL(0, t2.samples());
}

void test_temperature_fit() {
  NanoHistory h;
  h.onTail({"Three hour averages: TIC+DAC+temp"});
  h.onTail({"Now acquiring value: 0"});
  for (int i = 0; i < 40; ++i) {
    double temp = 42.0 + (i % 10) * 0.25;
    int dac = (int)std::lround(30000 + 50 * temp);
    char t[16], d[16], c[16];
    std::snprintf(t, sizeof t, "%d", 1000 + i);
    std::snprintf(d, sizeof d, "%d", dac);
    std::snprintf(c, sizeof c, "%.1f", temp);
    h.onTail({t, "5000", d, c, "Locked"});
  }
  TempFit f = fitTemperature(h, 655);
  TEST_ASSERT_TRUE(f.valid);
  TEST_ASSERT_EQUAL(40, f.points);
  TEST_ASSERT_FLOAT_WITHIN(1.5, 50.0, f.slopeDacPerC);
  TEST_ASSERT_TRUE(f.r2 > 0.95);
  // DAC rising 50 counts/C needs a correction rising with temperature: negative coefficient,
  // which the Nano takes as 10000 + |c|. 50 x 0.1074 x 100 = 537.
  TEST_ASSERT_INT_WITHIN(20, 10537, f.suggestedTempCoeff);
  TEST_ASSERT_INT_WITHIN(5, 400, f.suggestedTempRef);  // about 43 C on an LM35
}

void test_sky_map() {
  SkyMap m;
  TEST_ASSERT_EQUAL_FLOAT(-1.0f, m.mean(4, 9));
  m.add(45, 95, 40);
  m.add(45, 95, 30);
  TEST_ASSERT_FLOAT_WITHIN(0.11f, 35.0f, m.mean(4, 9));
  static uint8_t blob[SkyMap::kBlobSize];
  m.toBlob(blob);
  SkyMap c;
  TEST_ASSERT_TRUE(c.fromBlob(blob, sizeof blob));
  TEST_ASSERT_FLOAT_WITHIN(0.11f, 35.0f, c.mean(4, 9));
}

void test_position_jump_is_spoofing() {
  InterferenceWatch w;
  w.restore(52.0, -1.0, 80.0);
  for (uint32_t t = 0; t < 20; ++t) w.update(t * 1000, 40, true, 52.0, -1.0, 80.0, 0.8f, 38, 6, 10);
  TEST_ASSERT_FALSE(w.spoofing(20000));
  // 2 km north for 10 s.
  for (uint32_t t = 20; t < 31; ++t) w.update(t * 1000, 40, true, 52.018, -1.0, 80.0, 0.8f, 38, 6, 10);
  TEST_ASSERT_TRUE(w.positionJump());
  TEST_ASSERT_TRUE(w.spoofing(31000));
  TEST_ASSERT_TRUE(w.jumpMetres() > 1900);
}

void test_signal_loss_and_uniform() {
  InterferenceWatch w;
  for (uint32_t t = 0; t < 300; ++t) w.update(t * 1000, 42, true, 52, -1, 80, 0.8f, 38, 6, 10);
  TEST_ASSERT_FALSE(w.jamming());
  for (uint32_t t = 300; t < 331; ++t) w.update(t * 1000, 28, true, 52, -1, 80, 0.8f, 26, 4, 8);
  TEST_ASSERT_TRUE(w.jamming());
  for (uint32_t t = 331; t < 400; ++t) w.update(t * 1000, 41, true, 52, -1, 80, 0.8f, 38, 6, 10);
  TEST_ASSERT_FALSE(w.jamming());
  // Many satellites all at 48 +- 1 dB-Hz for 30 s: one transmitter, not the sky.
  for (uint32_t t = 400; t < 431; ++t) w.update(t * 1000, 48, true, 52, -1, 80, 0.8f, 48, 1, 9);
  TEST_ASSERT_TRUE(w.uniformSignals());
}

void test_clock_time_step() {
  UtcClock c;
  const int64_t s = 1000000;
  for (int i = 0; i < 5; ++i) {
    c.onSecondMark(i * s);
    c.onNmeaTime(1794096000 + i, i * s + 150000);
  }
  TEST_ASSERT_EQUAL_UINT32(0, c.steps());
  c.onSecondMark(5 * s);
  c.onNmeaTime(1794096000 + 7, 5 * s + 150000);  // jumps 2 s ahead
  TEST_ASSERT_EQUAL_UINT32(1, c.steps());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_adev_basics);
  RUN_TEST(test_temperature_fit);
  RUN_TEST(test_sky_map);
  RUN_TEST(test_position_jump_is_spoofing);
  RUN_TEST(test_signal_loss_and_uniform);
  RUN_TEST(test_clock_time_step);
  return UNITY_END();
}
