// A short log of notable events (lock lost, GPS fix lost, restarts...), newest last.
// Plain data so the firmware can save it in flash and reload it after a restart.
#pragma once
#include <cstdint>
#include <cstring>

namespace gpsdo {

struct Event {
  int64_t unixSec;  // 0 if UTC was unknown at the time
  uint32_t upS;     // display uptime when it happened (used if unixSec is 0)
  uint8_t level;    // 0 good news, 1 notable, 2 bad
  char text[47];
};

class EventLog {
public:
  static constexpr int kSize = 40;
  void add(int64_t unixSec, uint32_t upS, uint8_t level, const char* text) {
    Event& e = ev_[head_];
    e.unixSec = unixSec;
    e.upS = upS;
    e.level = level;
    std::strncpy(e.text, text, sizeof e.text - 1);
    e.text[sizeof e.text - 1] = 0;
    head_ = (head_ + 1) % kSize;
    if (count_ < kSize) ++count_;
    ++changes_;
  }
  int count() const { return count_; }
  // i = 0 is the newest.
  const Event& newest(int i) const { return ev_[(head_ + kSize - 1 - i) % kSize]; }
  uint32_t changes() const { return changes_; }

  // Raw storage for saving to flash.
  static constexpr size_t kBlobSize = sizeof(Event) * kSize + 2;
  void toBlob(uint8_t* out) const {
    std::memcpy(out, ev_, sizeof ev_);
    out[sizeof ev_] = (uint8_t)head_;
    out[sizeof ev_ + 1] = (uint8_t)count_;
  }
  bool fromBlob(const uint8_t* in, size_t len) {
    if (len != kBlobSize || in[sizeof ev_] >= kSize || in[sizeof ev_ + 1] > kSize) return false;
    std::memcpy(ev_, in, sizeof ev_);
    head_ = in[sizeof ev_];
    count_ = in[sizeof ev_ + 1];
    for (auto& e : ev_) e.text[sizeof e.text - 1] = 0;
    return true;
  }

private:
  Event ev_[kSize] = {};
  int head_ = 0, count_ = 0;
  uint32_t changes_ = 0;
};

}  // namespace gpsdo
