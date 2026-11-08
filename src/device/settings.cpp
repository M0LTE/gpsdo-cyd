#include "settings.h"

#include <Preferences.h>

namespace gpsdo {

void Settings::load() {
  Preferences p;
  p.begin("gpsdo", true);
  wifiSsid = p.getString("ssid", "");
  wifiPass = p.getString("wpass", "");
  mqttHost = p.getString("mhost", "");
  mqttPort = p.getUShort("mport", 1883);
  mqttUser = p.getString("muser", "");
  mqttPass = p.getString("mpass", "");
  p.end();
}

void Settings::save() const {
  Preferences p;
  p.begin("gpsdo", false);
  p.putString("ssid", wifiSsid);
  p.putString("wpass", wifiPass);
  p.putString("mhost", mqttHost);
  p.putUShort("mport", mqttPort);
  p.putString("muser", mqttUser);
  p.putString("mpass", mqttPass);
  p.end();
}

}  // namespace gpsdo
