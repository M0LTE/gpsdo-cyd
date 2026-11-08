#include "ntp_server.h"

#include <Arduino.h>
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
volatile uint32_t served = 0;

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
    unixAt(esp_timer_get_time(), txUnix, st);
    if (!buildNtpReply(req, n, rxUnix, txUnix, st, out)) continue;
    sendto(sock, out, sizeof out, 0, (sockaddr*)&from, fl);
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

uint32_t requests() { return served; }
bool synced() { return isSynced; }

}  // namespace NtpServer
}  // namespace gpsdo
