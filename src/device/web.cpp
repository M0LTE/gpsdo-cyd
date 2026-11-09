#include "web.h"

#include <WebServer.h>
#include <esp_timer.h>

#include <new>
#include <string>

#include "state_json.h"
#include "web_index_gen.h"
#include "web_json.h"

namespace gpsdo {
namespace Web {
namespace {
WebServer server(80);
Monitor* mon = nullptr;
Net* netp = nullptr;
bool started = false;
// The reply is built in one string reused between requests (it keeps its capacity, so after the
// first few requests no new allocation is needed).
std::string reply;

// Sends the body straight from the std::string. (WebServer::send(code, type, String) would copy it
// into a second buffer, and two copies of a 4 KB reply at once ran the heap out on 8 Nov 2026.)
void sendJson(const std::string& body) {
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.setContentLength(body.size());
  server.send(200, "application/json", "");
  server.sendContent(body.data(), body.size());
}

uint32_t lowHeapSeen = UINT32_MAX;
void appendStr(std::string& o, const std::string& v) {
  o += '"';
  for (char c : v) {
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((unsigned char)c < 0x20) o += ' ';
    else o += c;
  }
  o += '"';
}

// What the network screen shows, as JSON. Passwords are never included.
void networkJson(std::string& o) {
  UiExtras x;
  netp->fill(x);
  char b[96];
  o = "{\"hostname\":\"gpsdo\",\"wifi_enabled\":";
  o += x.wifiEnabled ? "true" : "false";
  o += ",\"wifi_connected\":";
  o += x.wifiConnected ? "true" : "false";
  o += ",\"ssid\":";
  appendStr(o, x.ssid);
  o += ",\"ip\":";
  appendStr(o, x.ip);
  o += ",\"mac\":";
  appendStr(o, x.mac);
  std::snprintf(b, sizeof b, ",\"rssi_dbm\":%d,\"channel\":%d,\"ntp_synced\":%s,\"ntp_served\":%u", x.rssi, x.channel,
                x.ntpSynced ? "true" : "false", (unsigned)x.ntpRequests);
  o += b;
  o += ",\"mqtt_host\":";
  appendStr(o, x.mqttHost);
  o += ",\"mqtt_ip\":";
  appendStr(o, x.mqttIp);
  std::snprintf(b, sizeof b, ",\"mqtt_port\":%u,\"mqtt_connected\":%s,\"mqtt_up_s\":%u,\"published\":%u", (unsigned)x.mqttPort,
                x.mqttConnected ? "true" : "false", (unsigned)x.mqttUpS, (unsigned)x.publishes);
  o += b;
  if (x.lastPublishAgeS != UINT32_MAX) {
    std::snprintf(b, sizeof b, ",\"last_publish_age_s\":%u", (unsigned)x.lastPublishAgeS);
    o += b;
  }
  if (!x.lastError.empty()) {
    o += ",\"last_error\":";
    appendStr(o, x.lastError);
    std::snprintf(b, sizeof b, ",\"last_error_age_s\":%u", (unsigned)x.lastErrorAgeS);
    o += b;
  }
  std::snprintf(b, sizeof b, ",\"free_heap\":%u,\"uptime_s\":%u}", (unsigned)ESP.getFreeHeap(), (unsigned)(millis() / 1000));
  o += b;
}

template <class Build>
void serveJson(Build build) {
  uint32_t freeNow = ESP.getFreeHeap();
  if (freeNow < lowHeapSeen) {
    lowHeapSeen = freeNow;
    Serial.printf("# web: new low heap at request %s: free %u, largest %u, loop stack left %u\n", server.uri().c_str(),
                  (unsigned)freeNow, (unsigned)ESP.getMaxAllocHeap(), (unsigned)uxTaskGetStackHighWaterMark(nullptr));
  }
  try {
    build(reply);
  } catch (const std::bad_alloc&) {
    reply.clear();
    server.send(503, "text/plain", "busy, try again");
    return;
  }
  sendJson(reply);
}
}  // namespace

void begin(Monitor& m, Net& net) {
  mon = &m;
  netp = &net;
  server.on("/", HTTP_GET, [] {
    server.sendHeader("Content-Encoding", "gzip");
    server.sendHeader("Cache-Control", "no-cache");
    server.send_P(200, "text/html", (const char*)kWebIndexGz, kWebIndexGzLen);
  });
  server.on("/api/state", HTTP_GET, [] { serveJson([](std::string& o) { o = stateJson(*mon, millis()); }); });
  server.on("/api/events", HTTP_GET, [] { serveJson([](std::string& o) { eventsJson(*mon, o); }); });
  server.on("/api/trends", HTTP_GET, [] { serveJson([](std::string& o) { trendsJson(*mon, o); }); });
  server.on("/api/history", HTTP_GET, [] { serveJson([](std::string& o) { historyJson(*mon, o); }); });
  server.on("/api/sats", HTTP_GET, [] { serveJson([](std::string& o) { satsJson(*mon, millis(), o); }); });
  server.on("/api/accuracy", HTTP_GET, [] { serveJson([](std::string& o) { accuracyJson(*mon, o); }); });
  server.on("/api/time", HTTP_GET, [] { serveJson([](std::string& o) { timeJson(*mon, esp_timer_get_time(), millis(), o); }); });
  server.on("/api/stability", HTTP_GET, [] { serveJson([](std::string& o) { stabilityJson(*mon, 655, o); }); });
  server.on("/api/skymap", HTTP_GET, [] { serveJson([](std::string& o) { skyMapJson(*mon, o); }); });
  server.on("/api/recordings", HTTP_GET, [] { serveJson([](std::string& o) { recordingsJson(*mon, o); }); });
  server.on("/api/recording", HTTP_GET, [] {
    // Streamed with chunked transfer: a recording is about 25 KB of JSON.
    int i = server.hasArg("i") ? server.arg("i").toInt() : 0;
    server.sendHeader("Cache-Control", "no-store");
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "application/json", "");
    recordingJsonStream(*mon, i, [](const std::string& part) { server.sendContent(part.data(), part.size()); });
    server.sendContent("");  // end of the chunked reply
  });
  server.on("/api/network", HTTP_GET, [] { serveJson([](std::string& o) { networkJson(o); }); });
  server.onNotFound([] { server.send(404, "text/plain", "not found"); });
  server.begin();
  started = true;
  Serial.println("# web: serving on port 80");
}

void loop() {
  if (started) server.handleClient();
}

}  // namespace Web
}  // namespace gpsdo
