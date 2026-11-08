#include <unity.h>

#include <string>

#include "nmea.h"

using namespace gpsdo;

void setUp() {}
void tearDown() {}

static const Satellite* find(const NmeaParser& p, Constellation c, uint16_t prn) {
  for (const auto& s : p.satellites())
    if (s.valid && s.sys == c && s.prn == prn) return &s;
  return nullptr;
}

void test_checksum() {
  TEST_ASSERT_TRUE(NmeaParser::checksumOk("$GNZDA,132001.00,08,11,2026,00,00*77"));
  TEST_ASSERT_FALSE(NmeaParser::checksumOk("$GNZDA,132001.00,08,11,2026,00,00*78"));
  TEST_ASSERT_FALSE(NmeaParser::checksumOk("$GNZDA,132001.00,08,11,2026,00,00"));
  TEST_ASSERT_FALSE(NmeaParser::checksumOk("$GNZDA,132001.00,08,11,20"));  // partial line
  TEST_ASSERT_FALSE(NmeaParser::checksumOk("$GNZDA,132001.00,08,11,2026,00,00*7"));
  NmeaParser p;
  TEST_ASSERT_FALSE(p.parse("$GNZDA,132001.00,08,11,2026,00,00*78\r\n", 0));
  TEST_ASSERT_EQUAL_UINT32(1, p.badChecksums());
  TEST_ASSERT_FALSE(p.utc().valid);
}

void test_gga() {
  NmeaParser p;
  TEST_ASSERT_TRUE(p.parse("$GNGGA,132000.00,5126.00000,N,00058.00000,W,1,09,0.95,52.3,M,47.0,M,,*6F\r\n", 5));
  TEST_ASSERT_EQUAL_UINT8(1, p.fix().quality);
  TEST_ASSERT_EQUAL_UINT8(9, p.fix().satsUsed);
  TEST_ASSERT_EQUAL_FLOAT(0.95f, p.fix().hdop);
  TEST_ASSERT_TRUE(p.fix().haveAltitude);
  TEST_ASSERT_EQUAL_FLOAT(52.3f, p.fix().altitudeM);
}

void test_multipart_gsv_with_empty_snr_and_sbas() {
  NmeaParser p;
  TEST_ASSERT_TRUE(p.parse("$GPGSV,2,1,06,05,45,120,40,13,30,250,35,15,60,300,44,18,10,045,,1*6A", 1000));
  TEST_ASSERT_TRUE(p.parse("$GPGSV,2,2,06,20,05,010,,40,25,170,38,1*6A", 1000));
  TEST_ASSERT_TRUE(p.parse("$GLGSV,1,1,02,70,50,090,30,71,20,200,25,1*73", 1000));
  TEST_ASSERT_EQUAL(8, p.satsInView());
  const Satellite* g18 = find(p, Constellation::Gps, 18);
  TEST_ASSERT_NOT_NULL(g18);
  TEST_ASSERT_EQUAL_INT8(10, g18->elevation);
  TEST_ASSERT_EQUAL_INT16(45, g18->azimuth);
  TEST_ASSERT_EQUAL_INT8(-1, g18->snr);
  TEST_ASSERT_NOT_NULL(find(p, Constellation::Sbas, 127));  // PRN 40 -> S127
  const Satellite* r6 = find(p, Constellation::Glonass, 6);  // PRN 70 -> R6
  TEST_ASSERT_NOT_NULL(r6);
  TEST_ASSERT_EQUAL_INT8(30, r6->snr);
}

void test_legacy_gsv_without_signal_id() {
  NmeaParser p;
  TEST_ASSERT_TRUE(p.parse("$GPGSV,1,1,03,05,45,120,40,13,30,250,35,15,60,300,44*48", 0));
  TEST_ASSERT_EQUAL(3, p.satsInView());
  TEST_ASSERT_EQUAL_INT8(44, find(p, Constellation::Gps, 15)->snr);
}

void test_gsa_with_system_id() {
  NmeaParser p;
  p.parse("$GPGSV,2,1,06,05,45,120,40,13,30,250,35,15,60,300,44,18,10,045,,1*6A", 1000);
  p.parse("$GLGSV,1,1,02,70,50,090,30,71,20,200,25,1*73", 1000);
  TEST_ASSERT_TRUE(p.parse("$GNGSA,A,3,05,13,15,18,,,,,,,,,1.60,0.95,1.29,1*0A", 1000));
  TEST_ASSERT_TRUE(p.parse("$GNGSA,A,3,70,71,,,,,,,,,,,1.60,0.95,1.29,2*02", 1000));
  TEST_ASSERT_EQUAL_UINT8(3, p.fix().mode);
  TEST_ASSERT_EQUAL_FLOAT(1.60f, p.fix().pdop);
  TEST_ASSERT_EQUAL_FLOAT(1.29f, p.fix().vdop);
  TEST_ASSERT_EQUAL(6, p.satsUsed(1500));
  TEST_ASSERT_TRUE(p.isUsed(*find(p, Constellation::Glonass, 7), 1500));
  TEST_ASSERT_FALSE(p.isUsed(*find(p, Constellation::Gps, 13), 1000 + NmeaParser::kUsedWindowMs + 1));
}

void test_gn_gsa_without_system_id_uses_ublox_numbering() {
  NmeaParser p;
  TEST_ASSERT_TRUE(p.parse("$GNGSA,A,3,05,13,15,18,70,71,,,,,,,1.60,0.95,1.29*16", 0));
  TEST_ASSERT_TRUE(p.isUsed(*find(p, Constellation::Glonass, 6), 0));
  TEST_ASSERT_TRUE(p.isUsed(*find(p, Constellation::Gps, 5), 0));
}

void test_expiry() {
  NmeaParser p;
  p.parse("$GPGSV,1,1,03,05,45,120,40,13,30,250,35,15,60,300,44*48", 0);
  p.expire(NmeaParser::kExpireMs);
  TEST_ASSERT_EQUAL(3, p.satsInView());
  p.expire(NmeaParser::kExpireMs + 1);
  TEST_ASSERT_EQUAL(0, p.satsInView());
}

void test_rmc_zda_txt() {
  NmeaParser p;
  TEST_ASSERT_TRUE(p.parse("$GNRMC,132000.00,A,5126.00000,N,00058.00000,W,0.01,,081126,,,A,V*36", 0));
  TEST_ASSERT_TRUE(p.utc().valid);
  TEST_ASSERT_EQUAL_UINT16(2026, p.utc().year);
  TEST_ASSERT_EQUAL_UINT8(11, p.utc().month);
  TEST_ASSERT_EQUAL_UINT8(8, p.utc().day);
  TEST_ASSERT_EQUAL_UINT8(13, p.utc().hour);
  TEST_ASSERT_TRUE(p.parse("$GNZDA,132001.00,08,11,2026,00,00*77", 1000));
  TEST_ASSERT_EQUAL_UINT8(1, p.utc().second);
  TEST_ASSERT_TRUE(p.parse("$GNTXT,01,01,02,u-blox AG - www.u-blox.com*4E", 0));
  TEST_ASSERT_EQUAL_STRING("u-blox AG - www.u-blox.com", p.lastTxt().c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_checksum);
  RUN_TEST(test_gga);
  RUN_TEST(test_multipart_gsv_with_empty_snr_and_sbas);
  RUN_TEST(test_legacy_gsv_without_signal_id);
  RUN_TEST(test_gsa_with_system_id);
  RUN_TEST(test_gn_gsa_without_system_id_uses_ublox_numbering);
  RUN_TEST(test_expiry);
  RUN_TEST(test_rmc_zda_txt);
  return UNITY_END();
}
