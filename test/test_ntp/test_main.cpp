#include <unity.h>

#include <cstring>

#include "ntp_packet.h"

using namespace gpsdo;

void setUp() {}
void tearDown() {}

static uint64_t get64(const uint8_t* p) {
  uint64_t v = 0;
  for (int i = 0; i < 8; ++i) v = v << 8 | p[i];
  return v;
}

void test_timestamp() {
  // 2026-11-08 00:00:00.5 UTC = Unix 1794096000.5
  uint64_t ts = ntpTimestamp(1794096000LL * 1000000 + 500000);
  TEST_ASSERT_EQUAL_UINT32(1794096000UL + 2208988800UL, (uint32_t)(ts >> 32));
  TEST_ASSERT_EQUAL_UINT32(0x80000000UL, (uint32_t)ts);
}

void test_reply_synced() {
  uint8_t req[48] = {0};
  req[0] = (0 << 6) | (4 << 3) | 3;  // v4 client
  req[2] = 6;
  for (int i = 0; i < 8; ++i) req[40 + i] = (uint8_t)(0xA0 + i);
  NtpServerState st;
  st.synced = true;
  st.refUnixUs = 1794096000LL * 1000000;
  st.rootDispersionUs = 500;
  uint8_t out[48];
  TEST_ASSERT_TRUE(buildNtpReply(req, 48, 1794096001LL * 1000000, 1794096001LL * 1000000 + 120, st, out));
  TEST_ASSERT_EQUAL_HEX8((0 << 6) | (4 << 3) | 4, out[0]);
  TEST_ASSERT_EQUAL_UINT8(1, out[1]);
  TEST_ASSERT_EQUAL_UINT8(6, out[2]);
  TEST_ASSERT_EQUAL_MEMORY("GPS", out + 12, 4);
  TEST_ASSERT_EQUAL_MEMORY(req + 40, out + 24, 8);
  TEST_ASSERT_TRUE(get64(out + 40) > get64(out + 32));
}

void test_reply_unsynced_and_bad_requests() {
  uint8_t req[48] = {0};
  req[0] = (3 << 3) | 3;  // v3 client
  NtpServerState st;
  uint8_t out[48];
  TEST_ASSERT_TRUE(buildNtpReply(req, 48, 0, 0, st, out));
  TEST_ASSERT_EQUAL_HEX8((3 << 6) | (3 << 3) | 4, out[0]);
  TEST_ASSERT_EQUAL_UINT8(16, out[1]);
  TEST_ASSERT_FALSE(buildNtpReply(req, 47, 0, 0, st, out));
  req[0] = (4 << 3) | 4;  // a server reply, not a request
  TEST_ASSERT_FALSE(buildNtpReply(req, 48, 0, 0, st, out));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_timestamp);
  RUN_TEST(test_reply_synced);
  RUN_TEST(test_reply_unsynced_and_bad_requests);
  return UNITY_END();
}
