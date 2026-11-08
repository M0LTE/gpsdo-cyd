// Setup portal: a temporary Wi-Fi hotspot with a web form for the Wi-Fi and MQTT settings.
// On submit it checks the settings for real (joins the Wi-Fi, looks up the broker, connects and
// logs in) and only saves them once all of that works. It stops after 10 minutes.
#pragma once
#include <Arduino.h>

#include "settings.h"

namespace gpsdo {

class Portal {
public:
  static constexpr uint32_t kTimeoutMs = 10UL * 60UL * 1000UL;

  void begin(const Settings& current);
  // Call every loop while active(). Returns false once the portal has closed (timed out).
  bool loop(uint32_t nowMs);
  bool active() const { return active_; }

  // For the screen.
  const String& apSsid() const { return apSsid_; }
  const String& apPass() const { return apPass_; }
  const String& status() const { return status_; }
  uint32_t msLeft(uint32_t nowMs) const;

private:
  enum class Test : uint8_t { Idle, Wifi, Mqtt, Saved, Failed };
  void handleRoot();
  void handleSave();
  void handleStatus();
  void handleScan();
  void redirectHome();
  void step(uint32_t nowMs);
  String page(const String& body, bool refresh) const;

  bool active_ = false;
  uint32_t startMs_ = 0, testStartMs_ = 0, savedMs_ = 0;
  Test test_ = Test::Idle;
  Settings current_, trial_;
  String apSsid_, apPass_, status_, scanOptions_;
};

}  // namespace gpsdo
