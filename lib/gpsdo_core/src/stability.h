// Allan deviation of the 10 MHz against GPS, from the GPSDO's once-a-second phase (TIC ns).
// Only a continuous run of locked seconds is used: an unlock, a missed report or no 10 MHz starts
// the run again, because a gap or a lock transient would swamp the statistic.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace gpsdo {

class Stability {
public:
  static constexpr size_t kSeconds = 3600;  // one hour of 1 s phase
  // Feed each report: Nano seconds counter, phase ns, and whether this second is usable.
  void add(uint32_t time, int32_t phaseNs, bool usable);
  size_t samples() const { return count_; }

  // Overlapping Allan deviation at tau seconds, as a dimensionless fraction (1e-11 = 0.01 ppb).
  // False until there are at least 3*tau+1 samples.
  bool adev(uint32_t tau, double& out) const;

  // The standard taus 1, 2, 5, 10 ... 1000 s.
  static constexpr int kTaus = 10;
  static constexpr uint32_t kTau[kTaus] = {1, 2, 5, 10, 20, 50, 100, 200, 500, 1000};

private:
  int32_t at(size_t i) const { return x_[(head_ + kSeconds - count_ + i) % kSeconds]; }
  std::array<int32_t, kSeconds> x_{};
  size_t head_ = 0, count_ = 0;
  uint32_t lastTime_ = 0;
  bool haveLast_ = false;
};

}  // namespace gpsdo
