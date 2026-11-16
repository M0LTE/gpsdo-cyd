#include "ntp_server.h"

#include <Arduino.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <lwip/sockets.h>

#include "ntp_packet.h"

namespace gpsdo {
namespace NtpServer {
namespace {
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
int64_t baseLocalUs = 0, baseUnixUs = 0, refUnixUs = 0;
bool isSynced = false, haveClock = false;
uint32_t dispersionUs = 0;
volatile uint32_t served = 0, servedInterleaved = 0;
Interleave interleave;  // used only by the NTP task

bool unixAt(int64_t localUs, int64_t& unixUs, NtpServerState& st) {
  portENTER_CRITICAL(&mux);
  bool ok = haveClock;
  unixUs = baseUnixUs + (localUs - baseLocalUs);
  st.synced = isSynced;
  st.refUnixUs = refUnixUs;
  st.rootDispersionUs = dispersionUs;
  portEXIT_CRITICAL(&mux);
  return ok;
}

void task(void*) {
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(123);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (sock < 0 || bind(sock, (sockaddr*)&addr, sizeof addr) < 0) {
    Serial.println("# NTP: could not open UDP port 123");
    vTaskDelete(nullptr);
    return;
  }
  Serial.println("# NTP: serving on UDP port 123");
  uint8_t req[68], out[48];
  for (;;) {
    sockaddr_in from = {};
    socklen_t fl = sizeof from;
    int n = recvfrom(sock, req, sizeof req, 0, (sockaddr*)&from, &fl);
    int64_t rxLocal = esp_timer_get_time();
    if (n < 48) continue;
    int64_t rxUnix, txUnix;
    NtpServerState st;
    if (!unixAt(rxLocal, rxUnix, st)) continue;  // no time at all yet: stay silent
    uint32_t ip = from.sin_addr.s_addr;
    // Random bits below the microsecond keep our receive timestamps unique between clients.
    uint64_t rxTs = ntpTimestamp(rxUnix, esp_random());
    while (interleave.holds(rxTs)) ++rxTs;
    uint64_t prevTx = 0;
    bool xleave = interleave.take(ip, req, prevTx);
    unixAt(esp_timer_get_time(), txUnix, st);
    if (!buildNtpReplyTs(req, n, rxTs, xleave ? prevTx : ntpTimestamp(txUnix, esp_random()), xleave, st, out)) continue;
    sendto(sock, out, sizeof out, 0, (sockaddr*)&from, fl);
    // sendto returns once lwIP has handed the frame to the Wi-Fi driver: as close to the real
    // send time as we can see. A client in the interleaved mode gets this with its next reply.
    int64_t sentUnix;
    unixAt(esp_timer_get_time(), sentUnix, st);
    interleave.save(ip, rxTs, ntpTimestamp(sentUnix, esp_random()));
    if (xleave) servedInterleaved = servedInterleaved + 1;
    served = served + 1;
  }
}
}  // namespace

void begin() { xTaskCreatePinnedToCore(task, "ntp", 4096, nullptr, configMAX_PRIORITIES - 2, nullptr, 0); }

void setClock(int64_t localUs, int64_t unixUs, bool synced, uint32_t rootDispersionUs) {
  portENTER_CRITICAL(&mux);
  baseLocalUs = localUs;
  baseUnixUs = unixUs;
  if (synced) refUnixUs = unixUs - unixUs % 1000000;  // set from the GPS at the top of this second
  isSynced = synced;
  dispersionUs = rootDispersionUs;
  haveClock = true;
  portEXIT_CRITICAL(&mux);
}

void setUnsynced() {
  portENTER_CRITICAL(&mux);
  isSynced = false;
  portEXIT_CRITICAL(&mux);
}

uint32_t requests() { return served; }
uint32_t interleavedRequests() { return servedInterleaved; }
bool synced() { return isSynced; }

}  // namespace NtpServer
}  // namespace gpsdo
