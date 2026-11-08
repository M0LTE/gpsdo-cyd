// JSON for the display's web page and API (/api/...). Plain C++, tested on a PC.
#pragma once
#include <cstdint>
#include <string>

#include "monitor.h"

namespace gpsdo {

std::string eventsJson(const Monitor& m);              // newest first
std::string trendsJson(const Monitor& m);              // 12 s blocks, oldest first
std::string historyJson(const Monitor& m);             // the GPSDO's stored 5-minute and 3-hour averages
std::string satsJson(const Monitor& m, uint32_t nowMs);
std::string accuracyJson(const Monitor& m);

}  // namespace gpsdo
