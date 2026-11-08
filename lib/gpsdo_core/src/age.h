// Milliseconds from `then` to `now` on a wrapping 32-bit millisecond clock. A `then` slightly after
// `now` (an event timestamped later in the same loop pass than the loop's own reading of the clock)
// counts as age 0 instead of wrapping round to about 49 days.
#pragma once
#include <cstdint>

namespace gpsdo {
inline uint32_t ageMs(uint32_t now, uint32_t then) {
  int32_t d = (int32_t)(now - then);
  return d < 0 ? 0u : (uint32_t)d;
}
}  // namespace gpsdo
