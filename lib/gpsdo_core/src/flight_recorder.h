// Keeps the last 60 seconds of the GPS picture (every satellite's signal level, satellites used,
// fix, PPS present) and freezes it, plus the 30 seconds after, whenever the GPS drops out. Lets a
// dropout be diagnosed afterwards: everything falling at once points at the antenna or receiver,
// a gradual sag across all satellites at interference.
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>

namespace gpsdo {

struct FrSat {
  uint16_t id;  // constellation letter in the high byte, satellite number in the low byte
  int8_t snr;   // -1 if tracked without a level
  uint8_t used;
};

struct FrSample {
  uint32_t unixSec;  // 0 if UTC unknown
  uint8_t used;     // satellites in the fix (GGA)
  uint8_t quality;  // GGA fix quality
  int8_t best4;     // mean of the best four, -1 if fewer
  uint8_t pps;      // 1 if the GPSDO reported a PPS this second
  uint8_t n;        // satellites in sats[]
  FrSat sats[20];
};

struct FrRecording {
  bool valid = false;
  char reason[24] = {};
  uint32_t triggerUnixSec = 0;
  uint16_t triggerIndex = 0;  // sample index of the trigger second
  uint16_t count = 0;
  FrSample s[90];
};

class FlightRecorder {
public:
  static constexpr int kBefore = 60, kAfter = 30, kKeep = 3;

  // Once a second.
  void add(const FrSample& x);
  // A dropout started. Ignored while a recording is still filling.
  void trigger(const char* reason, uint32_t unixSec);

  // Finished recordings, newest first (i < count()).
  int count() const { return done_; }
  const FrRecording& recording(int i) const { return recs_[(next_ + kKeep - 1 - i) % kKeep]; }
  FlightRecorder() : recs_(new FrRecording[kKeep]) {}  // on the heap: too big for the ESP32's static RAM
  bool recording() const { return active_; }
  uint32_t finished() const { return finishedTotal_; }  // increments when a recording completes

  // The newest recording as raw bytes, for saving across restarts.
  static constexpr size_t kBlobSize = sizeof(FrRecording);
  bool restore(const uint8_t* in, size_t len);

private:
  std::array<FrSample, kBefore> ring_{};
  int ringHead_ = 0, ringCount_ = 0;
  std::unique_ptr<FrRecording[]> recs_;
  int next_ = 0, done_ = 0;
  bool active_ = false;
  int afterLeft_ = 0;
  uint32_t finishedTotal_ = 0;
};

}  // namespace gpsdo
