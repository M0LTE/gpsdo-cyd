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
// noise fills the fraction below one microsecond (it is taken modulo 4294), which keeps the
// timestamps the server hands out unique, as the interleaved mode needs.
uint64_t ntpTimestamp(int64_t unixUs, uint32_t noise = 0);

// The same reply with the receive and transmit timestamps already in NTP form. interleaved: answer
// in the interleaved mode (origin = the request's receive timestamp, transmit = the true send time
// of an earlier reply), see Interleave below.
bool buildNtpReplyTs(const uint8_t* req, int len, uint64_t rxTs, uint64_t txTs, bool interleaved,
                     const NtpServerState& st, uint8_t out[48]);

// Server side of the NTP interleaved client/server mode (draft-ietf-ntp-interleaved-modes, as used
// by chrony's "xleave"). After each reply the server saves its receive timestamp with the time the
// reply really left. A client asking for the interleaved mode puts the receive timestamp of our
// last reply to it in its origin field; we then send the true send time of that reply.
class Interleave {
public:
  static constexpr int kSlots = 64;  // replies to all clients share these, about 1.5 KB
  // Origin, receive and transmit timestamps of a request (bytes 24, 32 and 40, big-endian).
  static uint64_t field(const uint8_t* req, int offset);
  // If req (from ip) asks for the interleaved mode and we still hold the reply it refers to, gives
  // that reply's send time and forgets it (a receive timestamp is used once only).
  bool take(uint32_t ip, const uint8_t* req, uint64_t& prevTxTs);
  // Remember a reply just sent: its receive timestamp and when it actually left.
  void save(uint32_t ip, uint64_t rxTs, uint64_t txTs);
  // True if ts is already held as a receive timestamp (the caller then picks another).
  bool holds(uint64_t rxTs) const;

private:
  struct Slot {
    uint32_t ip = 0;
    uint64_t rx = 0, tx = 0;
  };
  Slot slots_[kSlots];
  int next_ = 0;
};

}  // namespace gpsdo
