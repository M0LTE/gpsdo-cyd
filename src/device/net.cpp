#include "net.h"

#include <PubSubClient.h>
#include <WiFi.h>

#include "config.h"
#include "state_json.h"

#define MQTT_BASE "gpsdo"  // topics: gpsdo/state, gpsdo/status

namespace gpsdo {
namespace {
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
Settings cfg;
bool enabled = false;
uint32_t lastAttemptMs = 0, lastPublishMs = 0, retryMs = 5000;
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

void Net::begin(const Settings& s) {
  cfg = s;
  enabled = s.configured();
  if (!enabled) {
    WiFi.mode(WIFI_OFF);
    return;
  }
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());
  WiFi.setTxPower(WIFI_TX_POWER);
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char id[24];
  snprintf(id, sizeof id, "gpsdo_cyd_%02x%02x%02x", mac[3], mac[4], mac[5]);
  deviceId = id;
  mqtt.setBufferSize(1024);
  mqtt.setKeepAlive(30);
  mqtt.setSocketTimeout(5);
}

void Net::loop(const Monitor& m, uint32_t nowMs) {
  if (!enabled) return;
  if (WiFi.status() != WL_CONNECTED) { txPowerSet = false; return; }
  if (!txPowerSet) { WiFi.setTxPower(WIFI_TX_POWER); txPowerSet = true; }
  if (!mqtt.connected()) {
    if (nowMs - lastAttemptMs < retryMs) return;
    lastAttemptMs = nowMs;
    // Look the broker up again on every attempt, so a changed address is followed.
    IPAddress ip;
    if (!WiFi.hostByName(cfg.mqttHost.c_str(), ip)) {
      Serial.printf("# MQTT: DNS lookup for %s failed, retrying\n", cfg.mqttHost.c_str());
      retryMs = retryMs < 60000 ? retryMs * 2 : 60000;
      return;
    }
    mqtt.setServer(ip, cfg.mqttPort);
    bool ok = cfg.mqttUser.length()
                  ? mqtt.connect(deviceId.c_str(), cfg.mqttUser.c_str(), cfg.mqttPass.c_str(), statusTopic(), 1, true, "offline")
                  : mqtt.connect(deviceId.c_str(), statusTopic(), 1, true, "offline");
    if (!ok) {
      Serial.printf("# MQTT: connect to %s (%s):%u failed, state %d\n", cfg.mqttHost.c_str(), ip.toString().c_str(),
                    cfg.mqttPort, mqtt.state());
      retryMs = retryMs < 60000 ? retryMs * 2 : 60000;
      return;
    }
    retryMs = 5000;
    Serial.printf("# MQTT: connected to %s (%s):%u\n", cfg.mqttHost.c_str(), ip.toString().c_str(), cfg.mqttPort);
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
  x.wifiEnabled = enabled;
  x.wifiConnected = enabled && WiFi.status() == WL_CONNECTED;
  x.mqttConnected = enabled && mqtt.connected();
}

}  // namespace gpsdo
