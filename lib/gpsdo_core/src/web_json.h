// JSON for the display's web page and API (/api/...). Plain C++, tested on a PC.
#pragma once
#include <cstdint>
#include <string>

#include "monitor.h"

namespace gpsdo {

// Each writes into `out`, replacing its contents but keeping its capacity, so the firmware can reuse
// one buffer reserved at start-up instead of allocating a new one per request.
void eventsJson(const Monitor& m, std::string& out);              // newest first
void trendsJson(const Monitor& m, std::string& out);              // 12 s blocks, oldest first
void historyJson(const Monitor& m, std::string& out);             // the GPSDO's stored averages
void satsJson(const Monitor& m, uint32_t nowMs, std::string& out);
void accuracyJson(const Monitor& m, std::string& out);

// Convenience forms returning a new string (tests, tools).
inline std::string eventsJson(const Monitor& m) { std::string s; eventsJson(m, s); return s; }
inline std::string trendsJson(const Monitor& m) { std::string s; trendsJson(m, s); return s; }
inline std::string historyJson(const Monitor& m) { std::string s; historyJson(m, s); return s; }
inline std::string satsJson(const Monitor& m, uint32_t nowMs) { std::string s; satsJson(m, nowMs, s); return s; }
inline std::string accuracyJson(const Monitor& m) { std::string s; accuracyJson(m, s); return s; }

}  // namespace gpsdo
