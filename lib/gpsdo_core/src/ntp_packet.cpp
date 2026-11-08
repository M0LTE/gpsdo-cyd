#include "ntp_packet.h"

#include <cstring>

namespace gpsdo {
namespace {
constexpr uint64_t kNtpUnixOffset = 2208988800ULL;  // seconds from 1900 to 1970

void put32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v >> 24);
  p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);
  p[3] = (uint8_t)v;
}
void put64(uint8_t* p, uint64_t v) {
  put32(p, (uint32_t)(v >> 32));
  put32(p + 4, (uint32_t)v);
}
}  // namespace

uint64_t ntpTimestamp(int64_t unixUs) {
  uint64_t sec = (uint64_t)(unixUs / 1000000) + kNtpUnixOffset;
  uint64_t us = (uint64_t)(unixUs % 1000000);
  uint64_t frac = (us << 32) / 1000000;
  return (sec << 32) | frac;
}

bool buildNtpReply(const uint8_t* req, int len, int64_t rxUnixUs, int64_t txUnixUs, const NtpServerState& st,
                   uint8_t out[48]) {
  if (len < 48) return false;
  uint8_t mode = req[0] & 7, version = (req[0] >> 3) & 7;
  if ((mode != 3 && mode != 1) || version < 1 || version > 4) return false;
  std::memset(out, 0, 48);
  uint8_t li = st.synced ? 0 : 3;
  out[0] = (uint8_t)(li << 6 | version << 3 | 4);  // mode 4: server
  out[1] = st.synced ? 1 : 16;                     // stratum
  out[2] = req[2];                                 // poll: echo the client's
  out[3] = (uint8_t)st.precisionLog2;
  // Root delay 0; root dispersion in 16.16 seconds.
  put32(out + 8, (uint32_t)(((uint64_t)st.rootDispersionUs << 16) / 1000000));
  std::memcpy(out + 12, "GPS", 4);                 // reference ID for a stratum 1 GPS source
  put64(out + 16, st.refUnixUs > 0 ? ntpTimestamp(st.refUnixUs) : 0);
  std::memcpy(out + 24, req + 40, 8);              // origin = client's transmit timestamp
  put64(out + 32, ntpTimestamp(rxUnixUs));
  put64(out + 40, ntpTimestamp(txUnixUs));
  return true;
}

}  // namespace gpsdo
