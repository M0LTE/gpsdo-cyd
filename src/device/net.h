#pragma once
#include <cstdint>

#include "monitor.h"
#include "settings.h"
#include "ui.h"

namespace gpsdo {

// Wi-Fi + MQTT publisher, using the settings saved by the setup portal. Off if none are saved.
class Net {
public:
  void begin(const Settings& s);
  // Call every loop. Publishes the state when it changes and every 10 s.
  void loop(const Monitor& m, uint32_t nowMs);
  void fill(UiExtras& x) const;
};

}  // namespace gpsdo
