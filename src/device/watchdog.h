// Restarts the CYD if its main loop stops running for 30 s, and reports why the last restart
// happened (watchdog, crash, brown-out) so it can go in the event log.
#pragma once
#include <cstdint>
#include <string>

namespace gpsdo {
namespace Watchdog {
void begin();
void feed();  // call every loop
// Why the CYD last started, in words, or nullptr for a normal power-on or flash.
const char* lastResetReason();
// The last crash saved in flash as JSON (task, PC, backtrace), or empty if there is none.
const std::string& lastCrash();
}  // namespace Watchdog
}  // namespace gpsdo
