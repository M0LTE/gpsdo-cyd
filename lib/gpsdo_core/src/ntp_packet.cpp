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

uint64_t ntpTimestamp(int64_t unixUs, uint32_t noise) {
  uint64_t sec = (uint64_t)(unixUs / 1000000) + kNtpUnixOffset;
  uint64_t us = (uint64_t)(unixUs % 1000000);
  uint64_t frac = (us << 32) / 1000000 + noise % 4294;  // 2^32 / 10^6 = 4294.97 steps per microsecond
  return (sec << 32) | frac;
}

bool buildNtpReply(const uint8_t* req, int len, int64_t rxUnixUs, int64_t txUnixUs, const NtpServerState& st,
                   uint8_t out[48]) {
  return buildNtpReplyTs(req, len, ntpTimestamp(rxUnixUs), ntpTimestamp(txUnixUs), false, st, out);
}

uint64_t Interleave::field(const uint8_t* req, int offset) {
  uint64_t v = 0;
  for (int i = 0; i < 8; ++i) v = v << 8 | req[offset + i];
  return v;
}

bool Interleave::take(uint32_t ip, const uint8_t* req, uint64_t& prevTxTs) {
  uint64_t origin = field(req, 24), rx = field(req, 32), tx = field(req, 40);
  // A basic request has a zero origin, or (from some clients) receive equal to transmit.
  if (origin == 0 || rx == tx) return false;
  for (Slot& s : slots_) {
    if (s.rx == origin && s.ip == ip) {
      prevTxTs = s.tx;
      s = Slot();
      return true;
    }
  }
  return false;
}

void Interleave::save(uint32_t ip, uint64_t rxTs, uint64_t txTs) {
  slots_[next_].ip = ip;
  slots_[next_].rx = rxTs;
  slots_[next_].tx = txTs;
  next_ = (next_ + 1) % kSlots;
}

bool Interleave::holds(uint64_t rxTs) const {
  for (const Slot& s : slots_)
    if (s.rx == rxTs) return true;
  return false;
}

bool buildNtpReplyTs(const uint8_t* req, int len, uint64_t rxTs, uint64_t txTs, bool interleaved,
                     const NtpServerState& st, uint8_t out[48]) {
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
  // Origin: the client's transmit timestamp, or in the interleaved mode its receive timestamp,
  // which is how the client tells the two apart.
  std::memcpy(out + 24, req + (interleaved ? 32 : 40), 8);
  put64(out + 32, rxTs);
  put64(out + 40, txTs);
  return true;
}

}  // namespace gpsdo
