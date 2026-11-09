#include "interference.h"

#include <cmath>

#include "age.h"

namespace gpsdo {

static double distanceM(double lat1, double lon1, double lat2, double lon2) {
  const double r = 6371000.0, d2r = 3.14159265358979 / 180.0;
  double dlat = (lat2 - lat1) * d2r, dlon = (lon2 - lon1) * d2r * std::cos((lat1 + lat2) / 2 * d2r);
  return r * std::sqrt(dlat * dlat + dlon * dlon);
}

bool InterferenceWatch::timeStepRecent(uint32_t nowMs) const {
  return haveTimeStep_ && ageMs(nowMs, timeStepMs_) < 10UL * 60000;  // flagged for 10 minutes
}

bool InterferenceWatch::learned(double& lat, double& lon, double& alt) const {
  if (!haveRef_) return false;
  lat = refLat_;
  lon = refLon_;
  alt = refAlt_;
  return true;
}

void InterferenceWatch::restore(double lat, double lon, double alt) {
  refLat_ = lat;
  refLon_ = lon;
  refAlt_ = alt;
  haveRef_ = true;
  learnedN_ = kLearnFixes;
}

void InterferenceWatch::update(uint32_t, int best4, bool fix, double lat, double lon, double alt, float hdop,
                               float usedSnrMean, float usedSnrSd, int usedCount) {
  // Signal loss: best-4 average 10 dB below its slow baseline for 30 s; clears after 60 s back
  // within 5 dB. A lost fix with no signal at all counts too (best4 -1 after a good baseline).
  if (best4 >= 0 && !jamming_) baseline_ = baseline_ < 0 ? best4 : baseline_ + (best4 - baseline_) / 600.0f;
  bool low = baseline_ >= 25 && (best4 < 0 || best4 < baseline_ - 10);
  if (low) { ++lowCount_; okCount_ = 0; }
  else { ++okCount_; lowCount_ = 0; }
  if (!jamming_ && lowCount_ >= 30) jamming_ = true;
  if (jamming_ && best4 >= 0 && best4 >= baseline_ - 5 && okCount_ >= 60) jamming_ = false;

  // Uniform strong signals: 6+ satellites, mean 45+ dB-Hz, spread under 2 dB, for 30 s.
  if (usedCount >= 6 && usedSnrMean >= 45 && usedSnrSd < 2.0f) ++uniformCount_;
  else uniformCount_ = 0;
  uniform_ = uniformCount_ >= 30;

  if (!fix) return;
  // Learn the antenna's position from 30 minutes of good fixes, weighted by 1/HDOP^2.
  if (!haveRef_) {
    if (hdop <= 0 || hdop > 3) return;
    double w = 1.0 / (hdop * hdop);
    sumLat_ += lat * w; sumLon_ += lon * w; sumAlt_ += alt * w; wSum_ += w;
    if (++learnedN_ >= kLearnFixes) {
      refLat_ = sumLat_ / wSum_; refLon_ = sumLon_ / wSum_; refAlt_ = sumAlt_ / wSum_;
      haveRef_ = true;
    }
    return;
  }
  // Position jump: more than 100 m (or 200 m in height) away for 10 s; clears after 60 s home.
  jumpM_ = distanceM(refLat_, refLon_, lat, lon);
  bool away = jumpM_ > kJumpM || std::fabs(alt - refAlt_) > 2 * kJumpM;
  if (away) { ++jumpCount_; homeCount_ = 0; }
  else { ++homeCount_; jumpCount_ = 0; }
  if (!posJump_ && jumpCount_ >= 10) posJump_ = true;
  if (posJump_ && homeCount_ >= 60) posJump_ = false;
}

}  // namespace gpsdo
