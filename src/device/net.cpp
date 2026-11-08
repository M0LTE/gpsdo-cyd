#include "net.h"

#if __has_include("secrets.h")
#include "secrets.h"
#define HAVE_NET 1
#else
#define HAVE_NET 0
#endif

#if HAVE_NET
#include <PubSubClient.h>
#include <WiFi.h>

#include "config.h"
#include "state_json.h"
#endif

namespace gpsdo {

#if HAVE_NET
namespace {
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
uint32_t lastAttemptMs = 0, lastPublishMs = 0;
bool txPowerSet = false;
std::string lastKey;
std::string deviceId;

const char* statusTopic() { return MQTT_BASE "/status"; }
const char* stateTopic() { return MQTT_BASE "/state"; }

void announce() {
  mqtt.publish(statusTopic(), "online", true);
  std::string topic, payload;
  for (int i = 0; i < Discovery::count(); ++i) {
    Discovery::entity(i, MQTT_BASE, deviceId, topic, payload);
    mqtt.publish(topic.c_str(), payload.c_str(), true);
  }
}
}  // namespace

void Net::begin() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setTxPower(WIFI_TX_POWER);
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char id[24];
  snprintf(id, sizeof id, "gpsdo_cyd_%02x%02x%02x", mac[3], mac[4], mac[5]);
  deviceId = id;
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(1024);
  mqtt.setKeepAlive(30);
}

void Net::loop(const Monitor& m, uint32_t nowMs) {
  if (WiFi.status() != WL_CONNECTED) { txPowerSet = false; return; }
  if (!txPowerSet) { WiFi.setTxPower(WIFI_TX_POWER); txPowerSet = true; }
  if (!mqtt.connected()) {
    if (nowMs - lastAttemptMs < 5000) return;
    lastAttemptMs = nowMs;
    const char* user = MQTT_USER;
    bool ok = mqtt.connect(deviceId.c_str(), *user ? user : nullptr, *user ? MQTT_PASSWORD : nullptr,
                           statusTopic(), 1, true, "offline");
    if (!ok) { Serial.printf("# MQTT connect failed, state %d\n", mqtt.state()); return; }
    Serial.println("# MQTT connected");
    announce();
    lastKey.clear();
  }
  mqtt.loop();
  // Publish at once when the headline or reference verdict changes, else every 10 s.
  std::string key = std::string(headlineText(m.headline())) + (m.referenceOk() ? "1" : "0");
  if (key != lastKey || nowMs - lastPublishMs >= 10000) {
    std::string j = stateJson(m, nowMs);
    if (mqtt.publish(stateTopic(), j.c_str(), true)) {
      lastKey = key;
      lastPublishMs = nowMs;
    }
  }
}

void Net::fill(UiExtras& x) const {
  x.wifiEnabled = true;
  x.wifiConnected = WiFi.status() == WL_CONNECTED;
  x.mqttConnected = mqtt.connected();
}
#else
void Net::begin() {}
void Net::loop(const Monitor&, uint32_t) {}
void Net::fill(UiExtras& x) const { x.wifiEnabled = false; }
#endif

}  // namespace gpsdo
