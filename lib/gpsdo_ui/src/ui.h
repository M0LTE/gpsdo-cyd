// LVGL screens for the 320x240 GPSDO status display.
// Builds on any LVGL 9 display; the firmware and the PC simulator both use it.
#pragma once
#include <cstdint>

#include "monitor.h"

namespace gpsdo {

// Things the UI shows that the monitor does not know about.
struct UiExtras {
  bool wifiEnabled = false;
  bool wifiConnected = false;
  bool mqttConnected = false;
};

class Ui {
public:
  static constexpr int kScreens = 4;
  enum Screen { kStatus = 0, kSky = 1, kBars = 2, kTrends = 3 };

  void begin();
  // Refresh the visible screen from the monitor. Call about once a second.
  void update(const Monitor& m, const UiExtras& x, uint32_t nowMs);
  void show(int screen);
  void next() { show((current_ + 1) % kScreens); }
  int current() const { return current_; }

private:
  void buildStatus();
  void buildSky();
  void buildBars();
  void buildTrends();
  void updateStatus(const Monitor& m, uint32_t nowMs);
  void updateSky(const Monitor& m, uint32_t nowMs);
  void updateBars(const Monitor& m, uint32_t nowMs);
  void updateTrends(const Monitor& m);
  void updateFooter(int screen, const Monitor& m, const UiExtras& x, uint32_t nowMs);

  int current_ = 0;
  void* screens_[kScreens] = {};
  void* footer_[kScreens] = {};
  void* footerNano_[kScreens] = {};
  void* footerGps_[kScreens] = {};
  void* footerNet_[kScreens] = {};
  void* footerPage_[kScreens] = {};
  void* footerTime_[kScreens] = {};

  // Status
  void *banner_ = nullptr, *headline_ = nullptr, *subline_ = nullptr;
  void *gpsMain_ = nullptr, *gpsSub_ = nullptr;
  void *ticMain_ = nullptr, *ticSub_ = nullptr;
  void *dacMain_ = nullptr, *dacSub_ = nullptr, *dacMarker_ = nullptr;
  void *tempMain_ = nullptr, *tempSub_ = nullptr;

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
};

}  // namespace gpsdo
