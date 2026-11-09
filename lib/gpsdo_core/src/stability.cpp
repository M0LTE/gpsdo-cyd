#include "stability.h"

#include <cmath>

namespace gpsdo {

constexpr uint32_t Stability::kTau[];

void Stability::add(uint32_t time, int32_t phaseNs, bool usable) {
  if (!usable || (haveLast_ && time != lastTime_ + 1)) {
    count_ = 0;
    head_ = 0;
  }
  haveLast_ = true;
  lastTime_ = time;
  if (!usable) return;
  x_[head_] = (int16_t)(phaseNs > 32767 ? 32767 : phaseNs < -32768 ? -32768 : phaseNs);
  head_ = (head_ + 1) % kSeconds;
  if (count_ < kSeconds) ++count_;
}

bool Stability::adev(uint32_t m, double& out) const {
  if (m == 0 || count_ < 3 * (size_t)m + 1) return false;
  // sigma^2(tau) = sum (x[i+2m] - 2 x[i+m] + x[i])^2 / (2 tau^2 (N - 2m)), x in seconds
  double sum = 0;
  size_t n = count_ - 2 * m;
  for (size_t i = 0; i < n; ++i) {
    double d = (double)at(i + 2 * m) - 2.0 * at(i + m) + at(i);
    sum += d * d;
  }
  double tau = m;
  out = std::sqrt(sum / (2.0 * tau * tau * n)) * 1e-9;
  return true;
}

}  // namespace gpsdo
