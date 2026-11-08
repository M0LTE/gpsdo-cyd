#include "locator.h"

#include <cmath>

namespace gpsdo {

std::string maidenhead(double lat, double lon, int chars) {
  double x = lon + 180.0, y = lat + 90.0;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x >= 360) x = 359.9999999;
  if (y >= 180) y = 179.9999999;
  std::string s;
  // Field 20 x 10 deg, square 2 x 1 deg, subsquare 5 x 2.5 min, extended 30 x 15 s, then /24 again.
  double w = 20.0, h = 10.0;
  for (int pair = 0; pair < chars / 2; ++pair) {
    int div = pair == 0 ? 18 : (pair % 2 == 1 ? 10 : 24);
    if (pair > 0) { w /= div; h /= div; }
    int ix = (int)std::floor(x / w), iy = (int)std::floor(y / h);
    x -= ix * w;
    y -= iy * h;
    if (pair % 2 == 1) {
      s += (char)('0' + ix);
      s += (char)('0' + iy);
    } else {
      char base = pair == 0 ? 'A' : 'a';
      s += (char)(base + ix);
      s += (char)(base + iy);
    }
  }
  return s;
}

}  // namespace gpsdo
