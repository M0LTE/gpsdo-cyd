#include "web.h"

#include <WebServer.h>

#include "state_json.h"
#include "web_index_gen.h"
#include "web_json.h"

namespace gpsdo {
namespace Web {
namespace {
WebServer server(80);
Monitor* mon = nullptr;
bool started = false;

void sendJson(const std::string& body) {
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", body.c_str());
}
}  // namespace

void begin(Monitor& m) {
  mon = &m;
  server.on("/", HTTP_GET, [] {
    server.sendHeader("Content-Encoding", "gzip");
    server.sendHeader("Cache-Control", "no-cache");
    server.send_P(200, "text/html", (const char*)kWebIndexGz, kWebIndexGzLen);
  });
  server.on("/api/state", HTTP_GET, [] { sendJson(stateJson(*mon, millis())); });
  server.on("/api/events", HTTP_GET, [] { sendJson(eventsJson(*mon)); });
  server.on("/api/trends", HTTP_GET, [] { sendJson(trendsJson(*mon)); });
  server.on("/api/history", HTTP_GET, [] { sendJson(historyJson(*mon)); });
  server.on("/api/sats", HTTP_GET, [] { sendJson(satsJson(*mon, millis())); });
  server.on("/api/accuracy", HTTP_GET, [] { sendJson(accuracyJson(*mon)); });
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
