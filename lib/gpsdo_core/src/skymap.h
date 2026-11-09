// Long-term map of signal strength by direction: 10 x 10 degree cells of azimuth and elevation,
// each a running average of the satellites seen there. Blocked parts of the sky show up as weak
// or empty cells. Plain data, so the firmware can save it across restarts.
#pragma once
#include <array>
#include <cstdint>
#include <cstring>

namespace gpsdo {

class SkyMap {
public:
  static constexpr int kAz = 36, kEl = 9;  // 10-degree cells; elevation 0-89
  static constexpr uint16_t kMaxWeight = 500;  // older samples fade once a cell has this many

  void add(int elevation, int azimuth, int snr) {
    if (elevation < 0 || elevation > 90 || azimuth < 0 || azimuth >= 360 || snr < 0) return;
    Cell& c = cells_[cell(elevation, azimuth)];
    if (c.n < kMaxWeight) ++c.n;
    c.meanX10 += (int)((snr * 10 - (int)c.meanX10) / (int)c.n);
    ++samples_;
  }
  // Mean SNR (dB-Hz) in the cell, or -1 if nothing has been seen there.
  float mean(int elIndex, int azIndex) const {
    const Cell& c = cells_[elIndex * kAz + azIndex];
    return c.n ? c.meanX10 / 10.0f : -1.0f;
  }
  uint16_t weight(int elIndex, int azIndex) const { return cells_[elIndex * kAz + azIndex].n; }
  uint32_t samples() const { return samples_; }

  static constexpr size_t kBlobSize = sizeof(uint16_t) * 2 * kAz * kEl + 4;
  void toBlob(uint8_t* out) const {
    std::memcpy(out, cells_.data(), sizeof(Cell) * kAz * kEl);
    std::memcpy(out + sizeof(Cell) * kAz * kEl, &samples_, 4);
  }
  bool fromBlob(const uint8_t* in, size_t len) {
    if (len != kBlobSize) return false;
    std::memcpy(cells_.data(), in, sizeof(Cell) * kAz * kEl);
    std::memcpy(&samples_, in + sizeof(Cell) * kAz * kEl, 4);
    return true;
  }

private:
  struct Cell {
    uint16_t meanX10;
    uint16_t n;
  };
  static_assert(sizeof(Cell) == 4, "packed cell");
  static int cell(int el, int az) { return (el >= 90 ? kEl - 1 : el / 10) * kAz + az / 10; }
  std::array<Cell, kAz * kEl> cells_{};
  uint32_t samples_ = 0;
};

}  // namespace gpsdo
