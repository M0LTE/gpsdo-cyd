// JSON for the display's web page and API (/api/...). Plain C++, tested on a PC.
#pragma once
#include <cstdint>
#include <functional>
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
// Allan deviation at the standard taus, and the temperature fit with its suggested settings.
void stabilityJson(const Monitor& m, int gain, std::string& out);
// The long-term sky map: [el index, az index, mean SNR, weight] for each cell seen.
void skyMapJson(const Monitor& m, std::string& out);
// Flight recorder: a list of the recordings, and one recording in full.
void recordingsJson(const Monitor& m, std::string& out);
// One recording, written piece by piece through `emit` (it is ~25 KB, too big to build in one
// string on the ESP32).
void recordingJsonStream(const Monitor& m, int index, const std::function<void(const std::string&)>& emit);
// UTC (from the PPS-aligned clock) and the GPS position. For the local web page only: the position
// is never published over MQTT.
void timeJson(const Monitor& m, int64_t nowUs, uint32_t nowMs, std::string& out);

// Convenience forms returning a new string (tests, tools).
inline std::string eventsJson(const Monitor& m) { std::string s; eventsJson(m, s); return s; }
inline std::string trendsJson(const Monitor& m) { std::string s; trendsJson(m, s); return s; }
inline std::string historyJson(const Monitor& m) { std::string s; historyJson(m, s); return s; }
inline std::string satsJson(const Monitor& m, uint32_t nowMs) { std::string s; satsJson(m, nowMs, s); return s; }
inline std::string accuracyJson(const Monitor& m) { std::string s; accuracyJson(m, s); return s; }
inline std::string stabilityJson(const Monitor& m, int gain) { std::string s; stabilityJson(m, gain, s); return s; }
inline std::string skyMapJson(const Monitor& m) { std::string s; skyMapJson(m, s); return s; }
inline std::string timeJson(const Monitor& m, int64_t nowUs, uint32_t nowMs) { std::string s; timeJson(m, nowUs, nowMs, s); return s; }

}  // namespace gpsdo
