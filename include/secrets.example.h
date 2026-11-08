// Copy to include/secrets.h (git-ignored) to turn on Wi-Fi and MQTT. Without it the CYD stays offline.
#pragma once
#define WIFI_SSID "your-ssid"
#define WIFI_PASSWORD "your-password"
#define MQTT_HOST "homeassistant.lan"
#define MQTT_PORT 1883
#define MQTT_USER ""        // empty for anonymous
#define MQTT_PASSWORD ""
#define MQTT_BASE "gpsdo"   // topics: gpsdo/state, gpsdo/status
