// Automatic backlight: dims the screen in a dark room using the CYD's light sensor (LDR, IO34).
// The sensor reads 0 in bright light and higher in the dark; its range is narrow, so the raw
// reading is logged on the console once a minute to help set the thresholds below.
#pragma once
#include <cstdint>

namespace gpsdo {
namespace Backlight {
void begin();
void loop(uint32_t nowMs);
int level();  // current brightness, 0..255
int lightRaw();  // smoothed sensor reading
}  // namespace Backlight

// Speaker on the CYD's P4 connector (IO26, through the on-board amplifier).
namespace Beeper {
void begin();
void loop(uint32_t nowMs);
void bad();   // three low beeps: the 10 MHz just became unreliable
void good();  // one high beep: it is reliable again
}  // namespace Beeper
}  // namespace gpsdo
