#include "flight_recorder.h"

namespace gpsdo {

void FlightRecorder::add(const FrSample& x) {
  ring_[ringHead_] = x;
  ringHead_ = (ringHead_ + 1) % kBefore;
  if (ringCount_ < kBefore) ++ringCount_;
  if (!active_) return;
  FrRecording& r = recs_[next_];
  if (r.count < (uint16_t)(sizeof r.s / sizeof r.s[0])) r.s[r.count++] = x;
  if (--afterLeft_ <= 0) {
    active_ = false;
    r.valid = true;
    next_ = (next_ + 1) % kKeep;
    if (done_ < kKeep) ++done_;
    ++finishedTotal_;
  }
}

void FlightRecorder::trigger(const char* reason, uint32_t unixSec) {
  if (active_) return;
  FrRecording& r = recs_[next_];
  r = FrRecording();
  std::strncpy(r.reason, reason, sizeof r.reason - 1);
  r.triggerUnixSec = unixSec;
  // Copy the last 60 seconds, oldest first.
  for (int i = 0; i < ringCount_; ++i) r.s[r.count++] = ring_[(ringHead_ + kBefore - ringCount_ + i) % kBefore];
  r.triggerIndex = r.count ? (uint16_t)(r.count - 1) : 0;
  active_ = true;
  afterLeft_ = kAfter;
}

bool FlightRecorder::restore(const uint8_t* in, size_t len) {
  if (len != kBlobSize) return false;
  FrRecording r;
  std::memcpy(&r, in, sizeof r);
  if (!r.valid || r.count > sizeof r.s / sizeof r.s[0]) return false;
  r.reason[sizeof r.reason - 1] = 0;
  recs_[next_] = r;
  next_ = (next_ + 1) % kKeep;
  if (done_ < kKeep) ++done_;
  return true;
}

}  // namespace gpsdo
