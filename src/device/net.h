#pragma once
#include <cstdint>

#include "monitor.h"
#include "ui.h"

namespace gpsdo {

// Wi-Fi + MQTT publisher. Built only when include/secrets.h exists; otherwise a no-op.
class Net {
public:
  void begin();
  // Call every loop. Publishes the state when it changes and every 10 s.
  void loop(const Monitor& m, uint32_t nowMs);
  void fill(UiExtras& x) const;
};

}  // namespace gpsdo
