// NTPv4 server replies (RFC 5905), built from UTC timestamps in microseconds since 1970.
#pragma once
#include <cstdint>

namespace gpsdo {

struct NtpServerState {
  bool synced = false;          // false: leap indicator 3 (alarm) and stratum 16, so clients ignore us
  int64_t refUnixUs = 0;        // when the clock was last set from the GPS
  uint32_t rootDispersionUs = 0;
  int8_t precisionLog2 = -10;   // about 1 ms
};

// Fills out[48] with the reply to a client request req[len]. Returns false if req is not a valid
// client (mode 3) or symmetric-active (mode 1) request.
bool buildNtpReply(const uint8_t* req, int len, int64_t rxUnixUs, int64_t txUnixUs, const NtpServerState& st,
                   uint8_t out[48]);

// NTP 64-bit timestamp (seconds since 1900 and 2^-32 fractions) from microseconds since 1970.
uint64_t ntpTimestamp(int64_t unixUs);

}  // namespace gpsdo
