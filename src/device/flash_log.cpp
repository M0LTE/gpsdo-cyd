#include "flash_log.h"

#include <Arduino.h>
#include <esp_partition.h>

#include "event_log.h"
#include "flight_recorder.h"

namespace {
class PartitionIo : public gpsdo::FlashIo {
public:
  explicit PartitionIo(const esp_partition_t* p) : p_(p) {}
  bool read(uint32_t off, void* dst, size_t n) override { return esp_partition_read(p_, off, dst, n) == ESP_OK; }
  bool write(uint32_t off, const void* src, size_t n) override { return esp_partition_write(p_, off, src, n) == ESP_OK; }
  bool erase(uint32_t off, size_t n) override { return esp_partition_erase_range(p_, off, n) == ESP_OK; }

private:
  const esp_partition_t* p_;
};

constexpr uint32_t kSector = 4096;
constexpr uint32_t kRecSlot = 3 * kSector;  // an 8.3 KB recording plus header
constexpr uint32_t kRecSlots = 40;
constexpr uint32_t kEventSlot = 80;         // a 64-byte event plus header, 51 to a sector
static_assert(sizeof(gpsdo::FrRecording) + gpsdo::RingStore::kHeader <= kRecSlot, "recording slot too small");
static_assert(sizeof(gpsdo::Event) + gpsdo::RingStore::kHeader <= kEventSlot, "event slot too small");

PartitionIo* io = nullptr;
gpsdo::RingStore* ev = nullptr;
gpsdo::RingStore* rec = nullptr;
bool flip = false;
}  // namespace

namespace FlashLog {
gpsdo::RingStore* events() { return ev; }
gpsdo::RingStore* recordings() { return rec; }

bool begin() {
  const esp_partition_t* p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
  uint32_t recBytes = kRecSlots * kRecSlot;
  if (!p || p->size < recBytes + 8 * kSector) {
    Serial.println("# flash log: no spiffs partition, events and recordings will not survive a restart");
    return false;
  }
  io = new PartitionIo(p);
  rec = new gpsdo::RingStore(*io, 0, recBytes, kRecSlot, kSector);
  uint32_t evBytes = (p->size - recBytes) / kSector * kSector;
  ev = new gpsdo::RingStore(*io, recBytes, evBytes, kEventSlot, kSector);
  uint32_t t0 = millis();
  rec->scan();
  ev->scan();
  Serial.printf("# flash log: %u event slots, %u recording slots, scanned in %u ms\n", (unsigned)ev->slots(),
                (unsigned)rec->slots(), (unsigned)(millis() - t0));
  return true;
}

void prepare() {
  if (!ev) return;
  flip = !flip;
  (flip ? ev : rec)->prepare();
}
}  // namespace FlashLog
