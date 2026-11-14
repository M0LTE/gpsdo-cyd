// Circular logs in the flash partition the partition table calls "spiffs" (unused otherwise):
// the event log and the GPS dropout recordings. Old entries are overwritten as new ones arrive,
// so the space never fills, and each flash sector is erased only once per lap.
#pragma once
#include "ring_store.h"

namespace FlashLog {
// Both are null if the partition is missing; callers then simply don't persist.
gpsdo::RingStore* events();      // one gpsdo::Event per record, about 5,250 kept (one sector less while the next is pre-erased)
gpsdo::RingStore* recordings();  // one gpsdo::FrRecording per record, 39 or 40 kept
bool begin();
// Erases at most one sector ahead of the writers, so saves themselves only write.
void prepare();
}  // namespace FlashLog
