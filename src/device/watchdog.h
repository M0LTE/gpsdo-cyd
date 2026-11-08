// Restarts the CYD if its main loop stops running for 30 s, and reports why the last restart
// happened (watchdog, crash, brown-out) so it can go in the event log.
#pragma once
#include <cstdint>

namespace gpsdo {
namespace Watchdog {
void begin();
void feed();  // call every loop
// Why the CYD last started, in words, or nullptr for a normal power-on or flash.
const char* lastResetReason();
}  // namespace Watchdog
}  // namespace gpsdo
