#include "tempfit.h"

#include <cmath>

namespace gpsdo {

TempFit fitTemperature(const NanoHistory& h, int gain) {
  TempFit f;
  double n = 0, sx = 0, sy = 0, sxx = 0, sxy = 0, syy = 0, tmin = 1e9, tmax = -1e9;
  // Only 3-hour blocks that were locked throughout: unlocked or restart blocks have the loop
  // still pulling, which is not the temperature effect.
  h.forEach(false, [&](const HistoryEntry& e) {
    if (e.lock != 'L') return;
    double x = e.tempC, y = e.dac;
    n += 1; sx += x; sy += y; sxx += x * x; sxy += x * y; syy += y * y;
    tmin = std::fmin(tmin, x);
    tmax = std::fmax(tmax, x);
  });
  f.points = (int)n;
  if (n < 8) return f;
  double vx = sxx - sx * sx / n, vy = syy - sy * sy / n, cxy = sxy - sx * sy / n;
  if (vx <= 1e-9) return f;
  f.slopeDacPerC = cxy / vx;
  f.r2 = vy > 0 ? (cxy * cxy) / (vx * vy) : 0;
  f.meanTempC = sx / n;
  f.tempSpanC = tmax - tmin;
  // LM35 with the 1.1 V reference: one ADC count is 1100/1024 mV = 0.1074 C.
  const double cPerCount = 1100.0 / 1024.0 * 0.1;
  f.suggestedTempRef = std::lround(f.meanTempC / cPerCount);
  // The firmware adds (tempRef*100 - filteredADC*100) * tempCoeff / 10000 to the DAC (per count,
  // tempCoeff/100 DAC counts per ADC count, falling as temperature rises). To cancel a DAC that
  // rises slope counts per C, the correction must also rise: tempCoeff = -slope * cPerCount * 100.
  double coeff = -f.slopeDacPerC * cPerCount * 100.0;
  long c = std::lround(coeff);
  f.suggestedTempCoeff = c >= 0 ? c : 10000 - c;  // the Nano's "add 10000 for negative"
  // gain = 65536 / (VCO range in ppb), so one DAC count is 1/gain... ppb per count = 65536/gain/65536.
  if (gain > 0) f.ppbPerC = f.slopeDacPerC / gain;
  f.valid = std::fabs(c) <= 10000;
  return f;
}

}  // namespace gpsdo
