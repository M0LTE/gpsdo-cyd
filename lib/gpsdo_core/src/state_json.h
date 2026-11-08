// The JSON state published over MQTT (retained) for Home Assistant, pdn-soundmodem and others.
#pragma once
#include <cstdint>
#include <string>

#include "monitor.h"

namespace gpsdo {

// Build the state document. See README for the field list.
std::string stateJson(const Monitor& m, uint32_t nowMs);

// Home Assistant MQTT discovery: the config topic and payload for entity i (0 <= i < count()),
// pointing at <base>/state and <base>/status. Returns false when i is out of range.
struct Discovery {
  static int count();
  static bool entity(int i, const std::string& base, const std::string& deviceId, std::string& topic,
                     std::string& payload);
};

}  // namespace gpsdo
