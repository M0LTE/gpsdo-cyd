#include "ring_store.h"

#include <cstring>

namespace gpsdo {

uint32_t crc32(const void* data, size_t n, uint32_t crc) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  crc = ~crc;
  while (n--) {
    crc ^= *p++;
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

namespace {
struct Header {
  uint32_t magic, seq, len, crc;
};
static_assert(sizeof(Header) == RingStore::kHeader, "header size");
}  // namespace

RingStore::RingStore(FlashIo& io, uint32_t base, uint32_t size, uint32_t slotSize, uint32_t sector)
    : io_(io), base_(base), size_(size), slotSize_(slotSize), sector_(sector) {
  if (slotSize_ <= sector_) {
    perSector_ = sector_ / slotSize_;
    sectorsPerSlot_ = 1;
    slots_ = (size_ / sector_) * perSector_;
  } else {
    perSector_ = 0;
    sectorsPerSlot_ = (slotSize_ + sector_ - 1) / sector_;
    slots_ = (size_ / sector_) / sectorsPerSlot_;
  }
}

uint32_t RingStore::slotOffset(uint32_t slot) const {
  if (perSector_) return base_ + (slot / perSector_) * sector_ + (slot % perSector_) * slotSize_;
  return base_ + slot * sectorsPerSlot_ * sector_;
}

bool RingStore::slotStartsSector(uint32_t slot) const { return !perSector_ || slot % perSector_ == 0; }

bool RingStore::isBlank(uint32_t off, uint32_t n) {
  uint8_t buf[64];
  while (n) {
    uint32_t k = n < sizeof buf ? n : sizeof buf;
    if (!io_.read(off, buf, k)) return false;
    for (uint32_t i = 0; i < k; i++)
      if (buf[i] != 0xFF) return false;
    off += k;
    n -= k;
  }
  return true;
}

// Erases the sectors under a slot that are not already blank (only the first such one if oneOnly).
int RingStore::eraseSlotSectors(uint32_t slot, bool oneOnly) {
  uint32_t start = perSector_ ? base_ + (slot / perSector_) * sector_ : slotOffset(slot);
  int n = 0;
  for (uint32_t s = 0; s < sectorsPerSlot_; s++) {
    uint32_t off = start + s * sector_;
    if (isBlank(off, sector_)) continue;
    if (!io_.erase(off, sector_)) return -1;
    ++erases_;
    ++n;
    if (oneOnly) return n;
  }
  return n;
}

bool RingStore::readValid(uint32_t slot, void* dst, uint32_t cap, uint32_t* seq, uint32_t* len) {
  Header h;
  uint32_t off = slotOffset(slot);
  if (!io_.read(off, &h, sizeof h)) return false;
  if (h.magic != kMagic || h.len > capacity() || h.seq == 0 || h.seq == 0xFFFFFFFFu) return false;
  // Check the CRC in chunks, copying into dst when asked.
  uint8_t buf[64];
  uint32_t crc = 0, done = 0;
  while (done < h.len) {
    uint32_t k = h.len - done < sizeof buf ? h.len - done : sizeof buf;
    if (!io_.read(off + kHeader + done, buf, k)) return false;
    crc = crc32(buf, k, crc);
    if (dst && done < cap) std::memcpy(static_cast<uint8_t*>(dst) + done, buf, (cap - done) < k ? cap - done : k);
    done += k;
  }
  if (crc != h.crc) return false;
  if (seq) *seq = h.seq;
  if (len) *len = h.len;
  return true;
}

void RingStore::scan() {
  uint32_t best = 0, bestSlot = 0;
  for (uint32_t i = 0; i < slots_; i++) {
    uint32_t seq;
    if (readValid(i, nullptr, 0, &seq, nullptr) && seq > best) {
      best = seq;
      bestSlot = i;
    }
  }
  nextReady_ = false;
  if (best) {
    next_ = (bestSlot + 1) % slots_;
    nextSeq_ = best + 1;
  } else {
    next_ = 0;
    nextSeq_ = 1;
  }
}

void RingStore::prepare() {
  if (!slots_ || nextReady_) return;
  // Mid-sector slots need nothing; otherwise erase one sector per call until none is left.
  if (!slotStartsSector(next_) || eraseSlotSectors(next_, true) == 0) nextReady_ = true;
}

bool RingStore::append(const void* data, uint32_t len) {
  if (!slots_ || len > capacity()) return false;
  // A slot in the middle of a sector should already be blank. If it is not (a write cut short by
  // a restart), skip it rather than erase a sector that holds newer records.
  for (uint32_t tries = 0; tries < slots_; tries++) {
    uint32_t off = slotOffset(next_);
    if (slotStartsSector(next_)) {
      if (eraseSlotSectors(next_, false) < 0) return false;
    } else if (!isBlank(off, slotSize_)) {
      next_ = (next_ + 1) % slots_;
      continue;
    }
    Header h{kMagic, nextSeq_, len, crc32(data, len)};
    bool ok = io_.write(off + kHeader, data, len) && io_.write(off, &h, sizeof h);
    next_ = (next_ + 1) % slots_;
    ++nextSeq_;
    nextReady_ = false;
    return ok;
  }
  return false;
}

int RingStore::newest(int i, void* dst, uint32_t cap, uint32_t* seq) {
  if (!slots_ || i < 0 || (uint32_t)i >= slots_) return -1;
  // Walk back from the newest, skipping damaged slots; sequence numbers must keep falling.
  uint32_t slot = next_, prevSeq = nextSeq_;
  int found = -1;
  for (uint32_t step = 0; step < slots_; step++) {
    slot = (slot + slots_ - 1) % slots_;
    uint32_t s, len;
    if (!readValid(slot, nullptr, 0, &s, &len)) continue;
    if (s >= prevSeq) break;  // wrapped round into newer records: the log has ended
    prevSeq = s;
    if (++found == i) {
      readValid(slot, dst, cap, seq, &len);
      return (int)len;
    }
  }
  return -1;
}

}  // namespace gpsdo
