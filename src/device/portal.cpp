#include "portal.h"

#include <DNSServer.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <WiFi.h>

#include "config.h"

namespace gpsdo {
namespace {

WebServer server(80);
DNSServer dns;
Portal* self = nullptr;
volatile uint8_t lastDisconnectReason = 0;

String esc(const String& s) {
  String o;
  o.reserve(s.length() + 8);
  for (char c : s) {
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

const char* wifiReason(uint8_t r) {
  switch (r) {
    case WIFI_REASON_NO_AP_FOUND: return "network not found (check the name, and that it is 2.4 GHz)";
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT: return "Wi-Fi password rejected";
    case WIFI_REASON_ASSOC_FAIL: return "the access point refused to connect";
    default: return nullptr;
  }
}

const char* mqttReason(int rc) {
  switch (rc) {
    case MQTT_CONNECTION_TIMEOUT: return "the broker did not answer in time";
    case MQTT_CONNECT_FAILED: return "could not connect to that address and port";
    case MQTT_CONNECT_BAD_PROTOCOL: return "the broker does not speak MQTT 3.1.1";
    case MQTT_CONNECT_BAD_CLIENT_ID: return "the broker rejected the client ID";
    case MQTT_CONNECT_UNAVAILABLE: return "the broker is unavailable";
    case MQTT_CONNECT_BAD_CREDENTIALS: return "MQTT username or password rejected";
    case MQTT_CONNECT_UNAUTHORIZED: return "this MQTT user is not allowed to connect";
    default: return "connection failed";
  }
}

}  // namespace

void Portal::begin(const Settings& current) {
  self = this;
  current_ = current;
  trial_ = current;
  active_ = true;
  startMs_ = millis();
  test_ = Test::Idle;
  status_ = "waiting for you to fill in the form";

  uint8_t mac[6];
  WiFi.macAddress(mac);
  char b[32];
  snprintf(b, sizeof b, "GPSDO-CYD-%02X%02X", mac[4], mac[5]);
  apSsid_ = b;
  snprintf(b, sizeof b, "%08lu", (unsigned long)(esp_random() % 100000000UL));
  apPass_ = b;

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) { lastDisconnectReason = info.wifi_sta_disconnected.reason; },
               ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  handleScan();
  WiFi.softAP(apSsid_.c_str(), apPass_.c_str());
  WiFi.setTxPower(WIFI_TX_POWER);
  dns.start(53, "*", WiFi.softAPIP());

  server.on("/", HTTP_GET, [] { self->handleRoot(); });
  server.on("/save", HTTP_POST, [] { self->handleSave(); });
  server.on("/status", HTTP_GET, [] { self->handleStatus(); });
  server.on("/scan", HTTP_GET, [] {
    self->handleScan();
    self->redirectHome();
  });
  server.onNotFound([] { self->redirectHome(); });  // captive: send every other URL to the form
  server.begin();
  Serial.printf("# setup portal: join %s (password %s), open http://%s/\n", apSsid_.c_str(), apPass_.c_str(),
                WiFi.softAPIP().toString().c_str());
}

uint32_t Portal::msLeft(uint32_t nowMs) const {
  uint32_t used = nowMs - startMs_;
  return used >= kTimeoutMs ? 0 : kTimeoutMs - used;
}

void Portal::handleScan() {
  int n = WiFi.scanNetworks();
  scanOptions_ = "";
  for (int i = 0; i < n; ++i) {
    String s = WiFi.SSID(i);
    if (!s.length() || scanOptions_.indexOf("value=\"" + esc(s) + "\"") >= 0) continue;
    scanOptions_ += "<option value=\"" + esc(s) + "\">";
  }
  WiFi.scanDelete();
}

String Portal::page(const String& body, bool refresh) const {
  String h = F("<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>");
  if (refresh) h += F("<meta http-equiv=refresh content=2>");
  h += F("<title>GPSDO display setup</title><style>body{font-family:sans-serif;max-width:28em;margin:1em auto;padding:0 1em;"
         "background:#0b0f14;color:#e6edf3}input,button{width:100%;padding:.6em;margin:.2em 0 .8em;box-sizing:border-box;"
         "font-size:1em}label{color:#8b98a5}button{background:#2da44e;color:#fff;border:0;border-radius:6px}"
         ".bad{color:#e5534b}.ok{color:#2da44e}a{color:#4c9aff}</style></head><body><h2>GPSDO display setup</h2>");
  return h + body + F("</body></html>");
}

void Portal::handleRoot() {
  String b;
  if (test_ == Test::Failed) b += "<p class=bad>Not saved: " + esc(status_) + "</p>";
  b += F("<form method=post action=/save><datalist id=nets>");
  b += scanOptions_;
  b += F("</datalist><label>Wi-Fi network</label><input name=ssid list=nets required value=\"");
  b += esc(trial_.wifiSsid) + F("\"><label>Wi-Fi password</label><input name=wpass type=password placeholder=\"");
  b += trial_.wifiPass.length() ? F("leave blank to keep the saved one") : F("");
  b += F("\"><label>MQTT broker (name or IP address)</label><input name=mhost required value=\"");
  b += esc(trial_.mqttHost) + F("\"><label>MQTT port</label><input name=mport type=number min=1 max=65535 value=\"");
  b += String(trial_.mqttPort) + F("\"><label>MQTT username (blank if none)</label><input name=muser value=\"");
  b += esc(trial_.mqttUser) + F("\"><label>MQTT password</label><input name=mpass type=password placeholder=\"");
  b += trial_.mqttPass.length() ? F("leave blank to keep the saved one") : F("");
  b += F("\"><button>Check and save</button></form><p><a href=/scan>Scan for networks again</a></p>"
         "<p><label>Your phone may drop off this hotspot for a moment while the display joins your Wi-Fi. "
         "Rejoin it if the page stops updating.</label></p>");
  server.send(200, "text/html", page(b, false));
}

void Portal::handleSave() {
  Settings t = trial_;
  t.wifiSsid = server.arg("ssid");
  t.wifiSsid.trim();
  if (server.arg("wpass").length() || t.wifiSsid != current_.wifiSsid) t.wifiPass = server.arg("wpass");
  t.mqttHost = server.arg("mhost");
  t.mqttHost.trim();
  long port = server.arg("mport").toInt();
  t.mqttPort = port > 0 && port < 65536 ? (uint16_t)port : 1883;
  t.mqttUser = server.arg("muser");
  t.mqttUser.trim();
  if (server.arg("mpass").length() || t.mqttUser != current_.mqttUser) t.mqttPass = server.arg("mpass");
  trial_ = t;

  WiFi.disconnect(false, false);
  lastDisconnectReason = 0;
  WiFi.begin(trial_.wifiSsid.c_str(), trial_.wifiPass.c_str());
  test_ = Test::Wifi;
  testStartMs_ = millis();
  status_ = "joining " + trial_.wifiSsid + "...";
  server.sendHeader("Location", "/status");
  server.send(303);
}

void Portal::handleStatus() {
  bool busy = test_ == Test::Wifi || test_ == Test::Mqtt;
  String b;
  if (test_ == Test::Saved) b = "<p class=ok>" + esc(status_) + "</p><p>The display is restarting and will connect by itself. You can leave this hotspot.</p>";
  else if (test_ == Test::Failed) b = "<p class=bad>" + esc(status_) + "</p><p><a href=/>Back to the form</a></p>";
  else if (busy) b = "<p>" + esc(status_) + "</p>";
  else { redirectHome(); return; }
  server.send(200, "text/html", page(b, busy));
}

void Portal::redirectHome() {
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/");
  server.send(302);
}

void Portal::step(uint32_t nowMs) {
  if (test_ == Test::Wifi) {
    if (WiFi.status() == WL_CONNECTED) {
      test_ = Test::Mqtt;
      status_ = "Wi-Fi joined, looking up " + trial_.mqttHost + "...";
      return;
    }
    const char* why = wifiReason(lastDisconnectReason);
    if (why || nowMs - testStartMs_ > 20000) {
      status_ = String("Wi-Fi: ") + (why ? why : "could not join within 20 s");
      test_ = Test::Failed;
      WiFi.disconnect(false, false);
    }
    return;
  }
  if (test_ == Test::Mqtt) {
    IPAddress ip;
    if (!WiFi.hostByName(trial_.mqttHost.c_str(), ip)) {
      status_ = "MQTT: could not find " + trial_.mqttHost + " (DNS lookup failed)";
      test_ = Test::Failed;
      return;
    }
    WiFiClient tcp;
    PubSubClient c(tcp);
    c.setServer(ip, trial_.mqttPort);
    c.setSocketTimeout(5);
    String id = "gpsdo-cyd-setup-" + String((uint32_t)esp_random(), HEX);
    bool ok = trial_.mqttUser.length() ? c.connect(id.c_str(), trial_.mqttUser.c_str(), trial_.mqttPass.c_str())
                                       : c.connect(id.c_str());
    String where = trial_.mqttHost + " (" + ip.toString() + "):" + String(trial_.mqttPort);
    if (!ok) {
      status_ = "MQTT at " + where + ": " + mqttReason(c.state());
      test_ = Test::Failed;
      return;
    }
    c.disconnect();
    trial_.save();
    status_ = "All good: Wi-Fi joined and logged in to MQTT at " + where + ". Saved.";
    test_ = Test::Saved;
    savedMs_ = nowMs;
    Serial.printf("# setup saved: %s\n", status_.c_str());
  }
}

bool Portal::loop(uint32_t) {
  if (!active_) return false;
  dns.processNextRequest();
  server.handleClient();
  // Read the clock after handling requests: a submit sets testStartMs_ from millis(), and an
  // older timestamp would make "now - start" wrap round and fail the test at once.
  uint32_t nowMs = millis();
  step(nowMs);
  if (test_ == Test::Saved && nowMs - savedMs_ > 4000) ESP.restart();
  if (msLeft(nowMs) == 0 && test_ != Test::Saved) {
    Serial.println("# setup portal timed out, Wi-Fi off");
    server.stop();
    dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    active_ = false;
  }
  return active_;
}

}  // namespace gpsdo
