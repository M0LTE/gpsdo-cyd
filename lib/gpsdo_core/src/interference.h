// Watches the GPS for signs of jamming or spoofing, using only what it already reports.
//  - Signal loss: the best satellites fall 10 dB or more below their recent level at once (jamming,
//    or the antenna failing).
//  - Position jump: the fix moves more than 100 m from where this fixed antenna normally is.
//  - Time step: the GPS second disagrees with the display's own running clock.
//  - Uniform signals: many satellites all strong and suspiciously equal (a single transmitter).
// Any spoofing sign means the GPS, and so the 10 MHz, cannot be trusted.
#pragma once
#include <cstdint>

namespace gpsdo {

class InterferenceWatch {
public:
  // Once a second with the current GPS picture.
  //  best4: mean SNR of the 4 strongest used satellites, -1 if fewer
  //  fix: a good 3D fix; lat/lon/alt valid only then; hdop for weighting the learned position
  //  usedSnrMean/usedSnrSd/usedCount: SNR statistics over satellites in the fix
  void update(uint32_t nowMs, int best4, bool fix, double lat, double lon, double alt, float hdop, float usedSnrMean,
              float usedSnrSd, int usedCount);
  // From the clock: the GPS time stepped against the display's running seconds.
  void noteTimeStep(uint32_t nowMs) { timeStepMs_ = nowMs; haveTimeStep_ = true; }

  bool jamming() const { return jamming_; }
  bool positionJump() const { return posJump_; }
  bool uniformSignals() const { return uniform_; }
  bool timeStepRecent(uint32_t nowMs) const;
  bool spoofing(uint32_t nowMs) const { return posJump_ || uniform_ || timeStepRecent(nowMs); }
  bool positionLearned() const { return learnedN_ >= kLearnFixes; }
  double jumpMetres() const { return jumpM_; }

  // The learned antenna position, for saving across restarts (kept on the device only).
  bool learned(double& lat, double& lon, double& alt) const;
  void restore(double lat, double lon, double alt);

  static constexpr int kLearnFixes = 1800;  // 30 minutes of good fixes
  static constexpr double kJumpM = 100.0;

private:
  double sumLat_ = 0, sumLon_ = 0, sumAlt_ = 0, wSum_ = 0;
  int learnedN_ = 0;
  double refLat_ = 0, refLon_ = 0, refAlt_ = 0;
  bool haveRef_ = false;
  int jumpCount_ = 0, homeCount_ = 0;
  bool posJump_ = false;
  double jumpM_ = 0;
  float baseline_ = -1;  // slow average of best4
  int lowCount_ = 0, okCount_ = 0;
  bool jamming_ = false;
  int uniformCount_ = 0;
  bool uniform_ = false;
  uint32_t timeStepMs_ = 0;
  bool haveTimeStep_ = false;
};

}  // namespace gpsdo
