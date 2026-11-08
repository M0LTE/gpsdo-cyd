// Milliseconds from `then` to `now` on a wrapping 32-bit millisecond clock. A `then` up to a minute
// after `now` (an event timestamped later in the same loop pass than the loop's own reading of the
// clock) counts as age 0. Anything else that looks negative is a real age of 24.8 days or more and
// counts as as old as possible, never as fresh. (Ages that wrap all the way round, 49.7 days, are
// caught by the monitor latching a source as stale until new data arrives.)
#pragma once
#include <cstdint>

namespace gpsdo {
inline uint32_t ageMs(uint32_t now, uint32_t then) {
  int32_t d = (int32_t)(now - then);
  if (d >= 0) return (uint32_t)d;
  return d > -60000 ? 0u : 0xFFFFFFFFu;
}
}  // namespace gpsdo
