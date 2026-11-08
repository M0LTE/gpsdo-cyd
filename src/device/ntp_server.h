// Stratum 1 NTP server on UDP port 123, in its own task so a request is timestamped the moment it
// arrives rather than whenever the display loop gets round to it.
#pragma once
#include <cstdint>

namespace gpsdo {

namespace NtpServer {
void begin();
// Called by the main loop with a fresh reading of the clock: at local time localUs it is
// unixUs. synced says whether that reading may be served as stratum 1.
void setClock(int64_t localUs, int64_t unixUs, bool synced, uint32_t rootDispersionUs);
uint32_t requests();
bool synced();
}  // namespace NtpServer

}  // namespace gpsdo
