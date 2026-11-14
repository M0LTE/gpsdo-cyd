// A circular log of fixed-size records in a raw flash region. New records go in the next slot;
// when the writer reaches the start of a flash sector it erases that sector, which deletes the
// oldest records. Every sector is erased once per lap of the region, so the wear is spread evenly
// and stays low. Nothing is kept in RAM beyond the write position, so a large region costs no heap.
//
// Each slot holds a 16-byte header (magic, sequence number, length, CRC-32) and the payload. The
// payload is written before the header, so a header that reads back intact means a whole record.
//
// Slots either fit inside a sector (several per sector) or are a whole number of sectors.
#pragma once
#include <cstddef>
#include <cstdint>

namespace gpsdo {

// The flash underneath: erase sets bytes to 0xFF, write can only clear bits.
class FlashIo {
public:
  virtual ~FlashIo() = default;
  virtual bool read(uint32_t off, void* dst, size_t n) = 0;
  virtual bool write(uint32_t off, const void* src, size_t n) = 0;
  virtual bool erase(uint32_t off, size_t n) = 0;  // off and n are whole sectors
};

uint32_t crc32(const void* data, size_t n, uint32_t crc = 0);

class RingStore {
public:
  static constexpr uint32_t kHeader = 16;
  static constexpr uint32_t kMagic = 0x31524447;  // "GDR1"

  // base and size in bytes, both whole sectors. slotSize includes the header.
  RingStore(FlashIo& io, uint32_t base, uint32_t size, uint32_t slotSize, uint32_t sector = 4096);

  // Finds the newest record so appends carry on after it. Call once before anything else.
  void scan();

  // Writes a record (len <= capacity()). Erases the sector(s) it needs first if they are not
  // blank. Returns false on a flash error.
  bool append(const void* data, uint32_t len);

  // Erases at most one sector the next append will need, so that append itself only writes.
  // Cheap when there is nothing to do. Call now and then from a quiet moment.
  void prepare();

  // Reads the i-th newest record (0 = newest) into dst. Returns its length, or -1 when there is
  // no such record (the log is shorter, or the record was damaged). seq gets its sequence number.
  int newest(int i, void* dst, uint32_t cap, uint32_t* seq = nullptr);

  uint32_t capacity() const { return slotSize_ - kHeader; }
  uint32_t slots() const { return slots_; }
  uint32_t nextSeq() const { return nextSeq_; }
  uint32_t erases() const { return erases_; }

private:
  uint32_t slotOffset(uint32_t slot) const;
  bool slotStartsSector(uint32_t slot) const;
  bool isBlank(uint32_t off, uint32_t n);
  int eraseSlotSectors(uint32_t slot, bool oneOnly);  // sectors erased, -1 on error
  bool readValid(uint32_t slot, void* dst, uint32_t cap, uint32_t* seq, uint32_t* len);

  FlashIo& io_;
  uint32_t base_, size_, slotSize_, sector_;
  uint32_t perSector_;      // slots in one sector (0 when a slot spans whole sectors)
  uint32_t sectorsPerSlot_; // sectors per slot (1 when slots share a sector)
  uint32_t slots_;
  uint32_t next_ = 0;       // slot the next append goes into
  uint32_t nextSeq_ = 1;
  uint32_t erases_ = 0;
  bool nextReady_ = false;  // the next slot is known to be blank, so prepare() has nothing to do
};

}  // namespace gpsdo
