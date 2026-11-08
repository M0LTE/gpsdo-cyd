// LVGL screens for the 320x240 GPSDO status display.
// Builds on any LVGL 9 display; the firmware and the PC simulator both use it.
#pragma once
#include <cstdint>
#include <string>

#include "monitor.h"

namespace gpsdo {

// Things the UI shows that the monitor does not know about.
struct UiExtras {
  bool wifiEnabled = false;
  bool wifiConnected = false;
  bool mqttConnected = false;
  // Network screen details (empty when unknown).
  std::string ssid, ip, mac, mqttHost, mqttIp, lastError, deviceId;
  int rssi = 0, channel = 0;
  uint32_t lastErrorAgeS = 0;
  uint16_t mqttPort = 0;
  uint32_t publishes = 0;
  uint32_t lastPublishAgeS = UINT32_MAX;
  uint32_t mqttUpS = 0;  // seconds since the current MQTT session started
  uint32_t ntpRequests = 0;
  bool ntpSynced = false;
};

class Ui {
public:
  static constexpr int kScreens = 9;
  enum Screen { kStatus = 0, kTime = 1, kAccuracy = 2, kSky = 3, kBars = 4, kTrends = 5, kHistory = 6, kEvents = 7,
                kNetwork = 8 };

  void begin();
  // Refresh the visible screen from the monitor. Call about once a second.
  void update(const Monitor& m, const UiExtras& x, uint32_t nowMs);
  void show(int screen);
  void next() { show((current_ + 1) % kScreens); }
  // Switch screens and draw the new one with fresh data straight away, so it never shows
  // whatever it held when last visible.
  void switchTo(int screen, const Monitor& m, const UiExtras& x, uint32_t nowMs, int64_t nowUs);
  // Call often (every ~50 ms) to keep the tenths moving on the time screen; cheap otherwise.
  // nowUs is on the same local microsecond clock as the timestamps given to the monitor.
  void fastUpdate(const Monitor& m, int64_t nowUs);
  int current() const { return current_; }
  // Full-screen Wi-Fi setup instructions, shown while the setup portal runs.
  void showSetup(const char* apSsid, const char* apPass, const char* url, const char* status, uint32_t secondsLeft);

private:
  void buildStatus();
  void buildSky();
  void buildBars();
  void buildTrends();
  void buildTime();
  void updateTime(const Monitor& m, uint32_t nowMs);
  void buildNetwork();
  void buildAccuracy();
  void updateAccuracy(const Monitor& m);
  void buildHistory();
  void updateHistory(const Monitor& m);
  void buildEvents();
  void updateEvents(const Monitor& m);
  void updateNetwork(const UiExtras& x);
  void updateStatus(const Monitor& m, uint32_t nowMs);
  void updateSky(const Monitor& m, uint32_t nowMs);
  void updateBars(const Monitor& m, uint32_t nowMs);
  void updateTrends(const Monitor& m);
  void updateFooter(int screen, const Monitor& m, const UiExtras& x, uint32_t nowMs);

  int current_ = 0;
  void* screens_[kScreens] = {};
  void* footer_[kScreens] = {};
  void* footerNano_[kScreens] = {};
  void* footerPage_[kScreens] = {};
  void* footerRef_[kScreens] = {};
  static constexpr int kNetRows = 9;
  void* netKey_[kNetRows] = {};
  void* netVal_[kNetRows] = {};
  void* netTitle_ = nullptr;
  // Accuracy
  void *accBig_ = nullptr, *accBigNote_ = nullptr;
  void* accRow_[3] = {};
  void* accBand_[6] = {};
  void* accNote_ = nullptr;
  // History
  void* histChart_[3] = {};
  void* histSeries_[3] = {};
  void* histRange_[3] = {};
  void* histStrip_ = nullptr;
  void* histNote_ = nullptr;
  uint32_t histSig_ = 0;
  // Events
  static constexpr int kEventRows = 9;
  void* evWhen_[kEventRows] = {};
  void* evText_[kEventRows] = {};
  void* evDot_[kEventRows] = {};
  void* evEmpty_ = nullptr;
  // Signal screen extra line
  void* barsAntenna_ = nullptr;
  void *setup_ = nullptr, *setupNet_ = nullptr, *setupPass_ = nullptr, *setupUrl_ = nullptr, *setupStatus_ = nullptr,
       *setupLeft_ = nullptr;

  // Time and position
  static constexpr int kClockChars = 10;  // HH:MM:SS.s
  void* clockChar_[kClockChars] = {};
  void *dateLabel_ = nullptr, *clockSource_ = nullptr;
  void *locLabel_ = nullptr, *latLabel_ = nullptr, *lonLabel_ = nullptr, *altLabel_ = nullptr;
  int64_t lastClockDs_ = -1;

  // Status
  void *banner_ = nullptr, *headline_ = nullptr, *subline_ = nullptr;
  void *gpsMain_ = nullptr, *gpsSub_ = nullptr;
  void *ticMain_ = nullptr, *ticSub_ = nullptr;
  void *dacMain_ = nullptr, *dacSub_ = nullptr, *dacMarker_ = nullptr;
  void *tempMain_ = nullptr, *tempSub_ = nullptr;
  void *progressTrack_ = nullptr, *progressFill_ = nullptr;

  // Sky
  static constexpr int kSkyDots = 48;
  void* skyDot_[kSkyDots] = {};
  void* skyLabel_[kSkyDots] = {};
  void* skyLegend_ = nullptr;
  void* skyEmpty_ = nullptr;

  // Bars
  static constexpr int kBarCount = 12;
  void* bar_[kBarCount] = {};
  void* barValue_[kBarCount] = {};
  void* barPrn_[kBarCount] = {};
  void* barsTitle_ = nullptr;
  void* barsEmpty_ = nullptr;

  // Trends
  void* chart_[3] = {};
  void* series_[3] = {};
  void* chartRange_[3] = {};
  void* lockStrip_ = nullptr;
  int32_t rangeLo_[3] = {}, rangeHi_[3] = {};
  bool haveRange_[3] = {};
};

}  // namespace gpsdo
