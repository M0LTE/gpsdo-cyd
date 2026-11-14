#include <unity.h>

#include <cstring>
#include <vector>

#include "ring_store.h"

using namespace gpsdo;

void setUp() {}
void tearDown() {}

// NOR flash in RAM: erase sets 0xFF, writes can only clear bits. Counts erases per sector.
struct FakeFlash : FlashIo {
  std::vector<uint8_t> mem;
  std::vector<int> eraseCount;
  int writeFailAfter = -1;  // fail the n-th write from now (simulates a restart mid-record)
  explicit FakeFlash(size_t n) : mem(n, 0xFF), eraseCount(n / 4096, 0) {}
  bool read(uint32_t off, void* d, size_t n) override {
    if (off + n > mem.size()) return false;
    std::memcpy(d, &mem[off], n);
    return true;
  }
  bool write(uint32_t off, const void* s, size_t n) override {
    if (off + n > mem.size()) return false;
    if (writeFailAfter == 0) return false;
    if (writeFailAfter > 0) --writeFailAfter;
    const uint8_t* p = static_cast<const uint8_t*>(s);
    for (size_t i = 0; i < n; i++) mem[off + i] &= p[i];
    return true;
  }
  bool erase(uint32_t off, size_t n) override {
    if (off % 4096 || n % 4096 || off + n > mem.size()) return false;
    std::memset(&mem[off], 0xFF, n);
    for (size_t s = off / 4096; s < (off + n) / 4096; s++) eraseCount[s]++;
    return true;
  }
};

struct Rec {
  uint32_t n;
  char pad[60];
};

void test_small_slots_keep_newest_and_wrap() {
  FakeFlash f(4 * 4096);
  RingStore r(f, 0, 4 * 4096, 80);
  r.scan();
  TEST_ASSERT_EQUAL_UINT32(4 * 51, r.slots());
  for (uint32_t i = 1; i <= 1000; i++) {
    Rec x{i, {}};
    TEST_ASSERT_TRUE(r.append(&x, sizeof x));
  }
  Rec y;
  TEST_ASSERT_EQUAL(sizeof y, r.newest(0, &y, sizeof y));
  TEST_ASSERT_EQUAL_UINT32(1000, y.n);
  TEST_ASSERT_EQUAL(sizeof y, r.newest(39, &y, sizeof y));
  TEST_ASSERT_EQUAL_UINT32(961, y.n);
  // At least three sectors' worth survive the erase of the oldest one.
  TEST_ASSERT_EQUAL(sizeof y, r.newest(3 * 51 - 1, &y, sizeof y));
  // Wear is even: every sector erased about the same number of times.
  for (int c : f.eraseCount) TEST_ASSERT_INT_WITHIN(1, f.eraseCount[0], c);
}

void test_scan_resumes_after_restart() {
  FakeFlash f(4 * 4096);
  {
    RingStore r(f, 0, 4 * 4096, 80);
    r.scan();
    for (uint32_t i = 1; i <= 300; i++) {
      Rec x{i, {}};
      r.append(&x, sizeof x);
    }
  }
  RingStore r(f, 0, 4 * 4096, 80);
  r.scan();
  TEST_ASSERT_EQUAL_UINT32(301, r.nextSeq());
  Rec x{301, {}};
  r.append(&x, sizeof x);
  Rec y;
  r.newest(0, &y, sizeof y);
  TEST_ASSERT_EQUAL_UINT32(301, y.n);
  r.newest(1, &y, sizeof y);
  TEST_ASSERT_EQUAL_UINT32(300, y.n);
}

void test_empty_and_short_logs() {
  FakeFlash f(2 * 4096);
  RingStore r(f, 0, 2 * 4096, 80);
  r.scan();
  Rec y;
  TEST_ASSERT_EQUAL(-1, r.newest(0, &y, sizeof y));
  Rec x{7, {}};
  r.append(&x, sizeof x);
  TEST_ASSERT_EQUAL(sizeof y, r.newest(0, &y, sizeof y));
  TEST_ASSERT_EQUAL(-1, r.newest(1, &y, sizeof y));
}

void test_multi_sector_slots() {
  FakeFlash f(30 * 4096);
  RingStore r(f, 0, 30 * 4096, 12288);
  r.scan();
  TEST_ASSERT_EQUAL_UINT32(10, r.slots());
  static uint8_t big[8400];
  for (uint32_t i = 1; i <= 25; i++) {
    std::memset(big, (int)i, sizeof big);
    if (i % 2) r.prepare();  // pre-erasing must not change what is kept
    TEST_ASSERT_TRUE(r.append(big, sizeof big));
  }
  static uint8_t out[8400];
  TEST_ASSERT_EQUAL(sizeof out, r.newest(0, out, sizeof out));
  TEST_ASSERT_EQUAL_UINT8(25, out[8399]);
  TEST_ASSERT_EQUAL(sizeof out, r.newest(9, out, sizeof out));
  TEST_ASSERT_EQUAL_UINT8(16, out[0]);
  TEST_ASSERT_EQUAL(-1, r.newest(10, out, sizeof out));
}

void test_record_cut_short_is_skipped() {
  FakeFlash f(2 * 4096);
  RingStore r(f, 0, 2 * 4096, 80);
  r.scan();
  for (uint32_t i = 1; i <= 5; i++) {
    Rec x{i, {}};
    r.append(&x, sizeof x);
  }
  f.writeFailAfter = 1;  // payload written, header lost
  Rec x{6, {}};
  r.append(&x, sizeof x);
  f.writeFailAfter = -1;
  RingStore r2(f, 0, 2 * 4096, 80);
  r2.scan();
  Rec y;
  r2.newest(0, &y, sizeof y);
  TEST_ASSERT_EQUAL_UINT32(5, y.n);
  // The dirty slot is skipped, not written over.
  Rec z{7, {}};
  TEST_ASSERT_TRUE(r2.append(&z, sizeof z));
  r2.newest(0, &y, sizeof y);
  TEST_ASSERT_EQUAL_UINT32(7, y.n);
  r2.newest(1, &y, sizeof y);
  TEST_ASSERT_EQUAL_UINT32(5, y.n);
}

void test_damaged_record_is_ignored() {
  FakeFlash f(2 * 4096);
  RingStore r(f, 0, 2 * 4096, 80);
  r.scan();
  for (uint32_t i = 1; i <= 3; i++) {
    Rec x{i, {}};
    r.append(&x, sizeof x);
  }
  f.mem[80 + 20] ^= 0x01;  // a bit flip in record 2's payload
  Rec y;
  r.newest(1, &y, sizeof y);
  TEST_ASSERT_EQUAL_UINT32(1, y.n);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_small_slots_keep_newest_and_wrap);
  RUN_TEST(test_scan_resumes_after_restart);
  RUN_TEST(test_empty_and_short_logs);
  RUN_TEST(test_multi_sector_slots);
  RUN_TEST(test_record_cut_short_is_skipped);
  RUN_TEST(test_damaged_record_is_ignored);
  return UNITY_END();
}
