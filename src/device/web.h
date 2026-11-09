// Read-only web page and JSON API on port 80 (http://gpsdo.local/), in normal (not setup) mode.
#pragma once
#include "monitor.h"
#include "net.h"

namespace gpsdo {
namespace Web {
void begin(Monitor& m, Net& net);
void loop();
}  // namespace Web
}  // namespace gpsdo
