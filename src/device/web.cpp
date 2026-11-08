#include "web.h"

#include <WebServer.h>

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

void begin(Monitor& m) {
  mon = &m;
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
