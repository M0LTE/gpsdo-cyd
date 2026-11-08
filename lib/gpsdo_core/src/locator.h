// Maidenhead locator from latitude and longitude.
#pragma once
#include <string>

namespace gpsdo {

// chars is 2, 4, 6, 8 or 10. Subsquare letters are lower case, as usually written (IO91wm12).
std::string maidenhead(double latDeg, double lonDeg, int chars = 8);

}  // namespace gpsdo
