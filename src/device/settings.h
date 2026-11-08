// Wi-Fi and MQTT settings, kept in the ESP32's flash (NVS) and set through the setup portal.
#pragma once
#include <Arduino.h>

namespace gpsdo {

struct Settings {
  String wifiSsid, wifiPass;
  String mqttHost;  // name or IP address; looked up again on every reconnect
  uint16_t mqttPort = 1883;
  String mqttUser, mqttPass;

  bool configured() const { return wifiSsid.length() > 0 && mqttHost.length() > 0; }
  void load();
  void save() const;
};

}  // namespace gpsdo
