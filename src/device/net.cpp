#include "net.h"

#include <PubSubClient.h>
#include <WiFi.h>

#include "config.h"
#include "ntp_server.h"
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
std::string lastError, brokerIp;
uint32_t lastErrorMs = 0;
uint32_t publishes = 0, connectedAtMs = 0;
bool everPublished = false, wasConnected = false;

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
  // No modem sleep: sleeping between beacons delays incoming NTP requests by up to ~100 ms but
  // not the replies, and that one-sided delay shows up to clients as a time offset. Staying
  // awake only keeps the receiver on; it adds no transmissions.
  WiFi.setSleep(false);
  // Join the strongest access point for the network, not the first one a quick scan finds
  // (it picked a far-off AP on channel 1 while one sat right next to it).
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
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
  NtpServer::begin();
}

void Net::loop(const Monitor& m, uint32_t nowMs) {
  if (!enabled) return;
  if (WiFi.status() != WL_CONNECTED) { txPowerSet = false; return; }
  if (!txPowerSet) {
    WiFi.setTxPower(WIFI_TX_POWER);
    txPowerSet = true;
    Serial.printf("# Wi-Fi: joined %s as %s, signal %d dBm\n", cfg.wifiSsid.c_str(), WiFi.localIP().toString().c_str(),
                  WiFi.RSSI());
  }
  if (!mqtt.connected()) {
    if (connectedAtMs && wasConnected) {
      wasConnected = false;
      Serial.printf("# MQTT: connection lost (state %d), Wi-Fi signal %d dBm\n", mqtt.state(), WiFi.RSSI());
      char e[48];
      snprintf(e, sizeof e, "MQTT connection lost, state %d", mqtt.state());
      lastError = e;
      lastErrorMs = nowMs;
    }
    if (nowMs - lastAttemptMs < retryMs) return;
    lastAttemptMs = nowMs;
    // Look the broker up again on every attempt, so a changed address is followed.
    IPAddress ip;
    if (!WiFi.hostByName(cfg.mqttHost.c_str(), ip)) {
      Serial.printf("# MQTT: DNS lookup for %s failed, retrying\n", cfg.mqttHost.c_str());
      lastError = "DNS lookup for " + std::string(cfg.mqttHost.c_str()) + " failed";
      lastErrorMs = nowMs;
      retryMs = retryMs < 60000 ? retryMs * 2 : 60000;
      return;
    }
    brokerIp = ip.toString().c_str();
    mqtt.setServer(ip, cfg.mqttPort);
    bool ok = cfg.mqttUser.length()
                  ? mqtt.connect(deviceId.c_str(), cfg.mqttUser.c_str(), cfg.mqttPass.c_str(), statusTopic(), 1, true, "offline")
                  : mqtt.connect(deviceId.c_str(), statusTopic(), 1, true, "offline");
    if (!ok) {
      Serial.printf("# MQTT: connect to %s (%s):%u failed, state %d\n", cfg.mqttHost.c_str(), ip.toString().c_str(),
                    cfg.mqttPort, mqtt.state());
      char e[48];
      snprintf(e, sizeof e, "MQTT connect failed, state %d", mqtt.state());
      lastError = e;
      lastErrorMs = nowMs;
      retryMs = retryMs < 60000 ? retryMs * 2 : 60000;
      return;
    }
    retryMs = 5000;
    connectedAtMs = nowMs;
    wasConnected = true;
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
      ++publishes;
      everPublished = true;
    }
  }
}

void Net::fill(UiExtras& x) const {
  x.wifiEnabled = enabled;
  x.wifiConnected = enabled && WiFi.status() == WL_CONNECTED;
  x.mqttConnected = enabled && mqtt.connected();
  if (!enabled) return;
  uint32_t now = millis();
  x.ssid = cfg.wifiSsid.c_str();
  x.mac = WiFi.macAddress().c_str();
  if (x.wifiConnected) {
    x.ip = WiFi.localIP().toString().c_str();
    x.rssi = WiFi.RSSI();
    x.channel = WiFi.channel();
  }
  x.mqttHost = cfg.mqttHost.c_str();
  x.mqttPort = cfg.mqttPort;
  x.mqttIp = brokerIp;
  x.deviceId = deviceId;
  x.lastError = lastError;
  x.lastErrorAgeS = (now - lastErrorMs) / 1000;
  x.publishes = publishes;
  if (everPublished) x.lastPublishAgeS = (now - lastPublishMs) / 1000;
  if (x.mqttConnected) x.mqttUpS = (now - connectedAtMs) / 1000;
  x.ntpRequests = NtpServer::requests();
  x.ntpSynced = NtpServer::synced();
}

}  // namespace gpsdo
