#include "ui.h"

#include "age.h"
#include "locator.h"
#include "tempfit.h"

#include <lvgl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace gpsdo {
namespace {

// Palette: dark background, colour only where it carries meaning.
constexpr uint32_t kBg = 0x0B0F14;
constexpr uint32_t kPanel = 0x161C24;
constexpr uint32_t kLine = 0x2A3340;
constexpr uint32_t kText = 0xE6EDF3;
constexpr uint32_t kDim = 0x8B98A5;
constexpr uint32_t kFaint = 0x4A5563;
constexpr uint32_t kGreen = 0x2DA44E;
constexpr uint32_t kAmber = 0xD4A017;
constexpr uint32_t kRed = 0xE5534B;
constexpr uint32_t kBlue = 0x4C9AFF;
constexpr uint32_t kGrey = 0x3D4652;
constexpr uint32_t kLime = 0x8CC63F;

constexpr int kW = 320;
constexpr int kH = 240;
constexpr int kFooterH = 22;

lv_obj_t* O(void* p) { return static_cast<lv_obj_t*>(p); }
lv_chart_series_t* S(void* p) { return static_cast<lv_chart_series_t*>(p); }


lv_obj_t* box(lv_obj_t* parent, int x, int y, int w, int h, uint32_t bg, int radius = 6) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(o, radius, 0);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  return o;
}

lv_obj_t* text(lv_obj_t* parent, const lv_font_t* font, uint32_t color, int x, int y, const char* s = "") {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_obj_set_pos(l, x, y);
  lv_label_set_text(l, s);
  return l;
}

lv_obj_t* ring(lv_obj_t* parent, int cx, int cy, int r, uint32_t color, int width) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, 2 * r, 2 * r);
  lv_obj_set_pos(o, cx - r, cy - r);
  lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_color(o, lv_color_hex(color), 0);
  lv_obj_set_style_border_width(o, width, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  return o;
}

void setText(void* label, const char* s) {
  if (std::strcmp(lv_label_get_text(O(label)), s) != 0) lv_label_set_text(O(label), s);
}

void setColor(void* label, uint32_t c) { lv_obj_set_style_text_color(O(label), lv_color_hex(c), 0); }

uint32_t snrColor(int snr) {
  if (snr < 0) return kGrey;
  if (snr < 20) return kRed;
  if (snr < 30) return kAmber;
  if (snr < 40) return kLime;
  return kGreen;
}

uint32_t headlineColor(Headline h) {
  switch (h) {
    case Headline::Locked: return kGreen;
    case Headline::Unlocked:
    case Headline::Holdover:
    case Headline::WarmUp: return kAmber;
    case Headline::NoData: return kGrey;
    default: return kRed;
  }
}

void fmtDuration(char* out, size_t n, uint32_t s) {
  uint32_t d = s / 86400, h = (s / 3600) % 24, m = (s / 60) % 60;
  if (d) std::snprintf(out, n, "%ud %uh", (unsigned)d, (unsigned)h);
  else if (h) std::snprintf(out, n, "%uh %02um", (unsigned)h, (unsigned)m);
  else std::snprintf(out, n, "%um %02us", (unsigned)m, (unsigned)(s % 60));
}

const char* sysName(Constellation c) {
  switch (c) {
    case Constellation::Gps: return "GPS";
    case Constellation::Sbas: return "SBAS";
    case Constellation::Glonass: return "GLONASS";
    case Constellation::Galileo: return "Galileo";
    case Constellation::BeiDou: return "BeiDou";
    case Constellation::Qzss: return "QZSS";
    default: return "Other";
  }
}

lv_obj_t* newScreen() {
  lv_obj_t* s = lv_obj_create(nullptr);
  lv_obj_remove_style_all(s);
  lv_obj_set_style_bg_color(s, lv_color_hex(kBg), 0);
  lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
  return s;
}

lv_obj_t* tile(lv_obj_t* parent, int x, int y, int w, int h, const char* title) {
  lv_obj_t* t = box(parent, x, y, w, h, kPanel, 8);
  text(t, &lv_font_montserrat_10, kDim, 8, 5, title);
  return t;
}

}  // namespace

void Ui::begin() { show(kStatus); }

// Only the visible screen exists: building all nine up front took 115 KB of the ESP32's heap,
// which left too little for Wi-Fi traffic. A screen is built when shown and deleted when left.
void Ui::build(int i) {
  switch (i) {
    case kStatus: buildStatus(); break;
    case kTime: buildTime(); break;
    case kAccuracy: buildAccuracy(); break;
    case kSky: buildSky(); break;
    case kBars: buildBars(); break;
    case kTrends: buildTrends(); break;
    case kHistory: buildHistory(); break;
    case kEvents: buildEvents(); break;
    case kNetwork: buildNetwork(); break;
    case kStability: buildStability(); break;
    case kSkyMap: buildSkyMap(); break;
  }
  skyMapSig_ = UINT32_MAX;
  // Fresh screens must redraw everything on their first update.
  histSig_ = 0;
  lastClockDs_ = -1;
  for (bool& h : haveRange_) h = false;
  // The footer is the "can I trust the 10 MHz right now" strip, the same on every screen.
  lv_obj_t* f = box(O(screens_[i]), 0, kH - kFooterH, kW, kFooterH, kGrey, 0);
  footer_[i] = f;
  footerRef_[i] = text(f, &lv_font_montserrat_14, 0xFFFFFF, 8, 3, "");
  // Source ages and network state, right-aligned before the page number.
  footerNano_[i] = text(f, &lv_font_montserrat_10, 0xFFFFFF, 0, 6);
  lv_obj_set_width(O(footerNano_[i]), 150);
  lv_obj_set_x(O(footerNano_[i]), 138);
  lv_obj_set_style_text_align(O(footerNano_[i]), LV_TEXT_ALIGN_RIGHT, 0);
  footerPage_[i] = text(f, &lv_font_montserrat_10, 0xFFFFFF, 297, 6);
  for (void* l : {footerNano_[i], footerPage_[i]}) lv_obj_set_style_text_opa(O(l), LV_OPA_80, 0);
}

void Ui::switchTo(int screen, const Monitor& m, const UiExtras& x, uint32_t nowMs, int64_t nowUs) {
  show(screen);
  update(m, x, nowMs);
  fastUpdate(m, nowUs);
  lv_refr_now(nullptr);
}

void Ui::show(int screen) {
  int old = current_;
  if (!screens_[screen]) build(screen);
  current_ = screen;
  lv_screen_load(O(screens_[screen]));
  if (old != screen && screens_[old]) {
    lv_obj_delete(O(screens_[old]));  // its child pointers are never used again until rebuilt
    screens_[old] = nullptr;
  }
}

// ---------------------------------------------------------------- Status

void Ui::buildStatus() {
  lv_obj_t* s = newScreen();
  screens_[kStatus] = s;
  banner_ = box(s, 6, 6, kW - 12, 78, kGrey, 10);
  headline_ = text(O(banner_), &lv_font_montserrat_40, 0xFFFFFF, 12, 4, "NO DATA");
  subline_ = text(O(banner_), &lv_font_montserrat_14, 0xFFFFFF, 14, 50, "waiting for the GPSDO");
  lv_obj_set_style_text_opa(O(subline_), LV_OPA_80, 0);
  lv_obj_set_size(O(subline_), kW - 12 - 28, 18);  // one line, "..." if too long
  lv_label_set_long_mode(O(subline_), LV_LABEL_LONG_DOT);
  // Progress bar along the bottom of the banner: warm-up timer, or good seconds towards lock.
  progressTrack_ = box(O(banner_), 14, 69, kW - 12 - 28, 4, 0xFFFFFF, 2);
  lv_obj_set_style_bg_opa(O(progressTrack_), LV_OPA_30, 0);
  progressFill_ = box(O(progressTrack_), 0, 0, 0, 4, 0xFFFFFF, 2);
  lv_obj_add_flag(O(progressTrack_), LV_OBJ_FLAG_HIDDEN);

  const int top = 90, w = 151, h = 60, gap = 5;
  lv_obj_t* g = tile(s, 6, top, w, h, "GPS");
  gpsMain_ = text(g, &lv_font_montserrat_20, kText, 8, 18, "--");
  gpsSub_ = text(g, &lv_font_montserrat_12, kDim, 8, 44, "");

  lv_obj_t* t = tile(s, 6 + w + gap, top, w, h, "PHASE VS GPS PULSE");
  ticMain_ = text(t, &lv_font_montserrat_20, kText, 8, 18, "--");
  ticSub_ = text(t, &lv_font_montserrat_12, kDim, 8, 44, "");

  lv_obj_t* d = tile(s, 6, top + h + gap, w, h, "OSCILLATOR TUNING");
  dacMain_ = text(d, &lv_font_montserrat_20, kText, 8, 18, "--");
  // Range bar 0..65535 with the firmware's warning zones (below 3000, above 62535) in red.
  const int bx = 8, by = 48, bw = w - 16, bh = 6;
  box(d, bx, by, bw, bh, kGrey, 3);
  int warnW = std::max(2, bw * 3000 / 65535);
  box(d, bx, by, warnW, bh, kRed, 3);
  box(d, bx + bw - warnW, by, warnW, bh, kRed, 3);
  dacMarker_ = box(d, bx, by - 3, 3, bh + 6, kText, 1);
  dacSub_ = text(d, &lv_font_montserrat_10, kDim, 0, 20, "");
  lv_obj_align(O(dacSub_), LV_ALIGN_TOP_RIGHT, -8, 24);

  lv_obj_t* tt = tile(s, 6 + w + gap, top + h + gap, w, h, "OSCILLATOR TEMP");
  tempMain_ = text(tt, &lv_font_montserrat_20, kText, 8, 18, "--");
  tempSub_ = text(tt, &lv_font_montserrat_12, kDim, 8, 44, "");
  for (void* l : {gpsSub_, ticSub_, tempSub_, gpsMain_, ticMain_, tempMain_}) {
    lv_obj_set_width(O(l), w - 12);
    lv_label_set_long_mode(O(l), LV_LABEL_LONG_DOT);
  }
}

void Ui::updateStatus(const Monitor& m, uint32_t nowMs) {
  char b[96];
  Headline h = m.headline();
  lv_obj_set_style_bg_color(O(banner_), lv_color_hex(headlineColor(h)), 0);
  setText(headline_, headlineText(h));
  const ReportTracker& n = m.nano();
  const ReportLine& d = n.last();

  // Sub line: the one thing worth knowing about the current state.
  char dur[24];
  switch (h) {
    case Headline::Locked:
      bool known;
      fmtDuration(dur, sizeof dur, m.secondsLocked(nowMs, known));
      if (m.missedPpsSinceStart())
        std::snprintf(b, sizeof b, "for %s%s, %u lock loss%s, %u missed pulse%s", dur, known ? "" : "+",
                      (unsigned)n.lockLosses(), n.lockLosses() == 1 ? "" : "es", (unsigned)m.missedPpsSinceStart(),
                      m.missedPpsSinceStart() == 1 ? "" : "s");
      else
        std::snprintf(b, sizeof b, "for %s%s, %u lock loss%s", dur, known ? "" : "+",
                      (unsigned)n.lockLosses(), n.lockLosses() == 1 ? "" : "es");
      if (m.gpsdoRestarts() || m.gpsRestarts()) {
        char ago[24];
        fmtDuration(ago, sizeof ago, ageMs(nowMs, m.lastRestartMs()) / 1000);
        std::snprintf(b, sizeof b, "for %s, restarted %s ago", dur, ago);
      }
      break;
    case Headline::RefSuspect:
      std::snprintf(b, sizeof b, "but the GPS has no good fix");
      break;
    case Headline::Unlocked: {
      uint32_t e = m.earliestLockS();
      if (e && m.haveStatus(nowMs) && m.status().mode == 'R') {
        fmtDuration(dur, sizeof dur, e);
        std::snprintf(b, sizeof b, "%+.0f ns off; locks in %s if it stays in", m.ticErrorNs(), dur);
      } else if (e) {
        fmtDuration(dur, sizeof dur, e);
        std::snprintf(b, sizeof b, "%+.0f ns off; earliest lock in %s", m.ticErrorNs(), dur);
      } else {
        std::snprintf(b, sizeof b, "phase %+.0f ns off target (limit 100)", m.ticErrorNs());
      }
      break;
    }
    case Headline::NoPps:
      if ((m.gpsdoRestarts() || m.gpsRestarts()) && ageMs(nowMs, m.lastRestartMs()) < 3600000UL) {
        fmtDuration(dur, sizeof dur, ageMs(nowMs, m.lastRestartMs()) / 1000);
        std::snprintf(b, sizeof b, "restarted %s ago, waiting for GPS", dur);
      } else {
        std::snprintf(b, sizeof b, "no GPS pulse, holding last tune");
      }
      break;
    case Headline::Missing10MHz: std::snprintf(b, sizeof b, "no 10 MHz from the oscillator"); break;
    case Headline::Holdover: std::snprintf(b, sizeof b, "hold mode set by command"); break;
    case Headline::WarmUp:
      fmtDuration(dur, sizeof dur, m.warmUpRemainingS());
      std::snprintf(b, sizeof b, "start-up timer, %s to go", dur);
      break;
    default:
      std::snprintf(b, sizeof b, n.haveData() ? "no report from the GPSDO" : "waiting for the GPSDO");
  }
  setText(subline_, b);
  int pm = m.progressPermille();
  if (pm < 0 || (h != Headline::WarmUp && h != Headline::Unlocked)) {
    lv_obj_add_flag(O(progressTrack_), LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_remove_flag(O(progressTrack_), LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_width(O(progressFill_), std::max(4, (kW - 12 - 28) * pm / 1000));
  }

  // GPS tile.
  const NmeaParser& g = m.gps();
  const Fix& f = g.fix();
  if (!m.gpsEverSeen()) {
    setText(gpsMain_, "no GPS data");
    setColor(gpsMain_, kFaint);
    setText(gpsSub_, "u-blox not connected");
  } else {
    bool noFix = m.alarms() & kAlarmGpsNoFix;
    unsigned used = f.haveGga ? f.satsUsed : (unsigned)g.satsUsed(nowMs);
    if (noFix) std::snprintf(b, sizeof b, "no fix");
    else std::snprintf(b, sizeof b, "%u satellites", used);
    setText(gpsMain_, b);
    setColor(gpsMain_, (m.alarms() & (kAlarmGpsNoFix | kAlarmGpsFewSats | kAlarmGpsSilent)) ? kRed : kText);
    std::snprintf(b, sizeof b, "%s, %u in view", noFix ? "searching" : f.mode == 3 ? "3D fix" : "2D fix",
                  (unsigned)g.satsInView());
    setText(gpsSub_, b);
  }

  if (!n.haveData()) return;
  // Grey the GPSDO figures when they are not fresh, so old numbers never pass for current ones.
  bool fresh = n.msSinceData(nowMs) <= Monitor::kSilentMs;
  for (void* l : {ticMain_, ticSub_, dacMain_, tempMain_, tempSub_})
    lv_obj_set_style_text_opa(O(l), fresh ? LV_OPA_COVER : LV_OPA_40, 0);
  // TIC tile.
  if (d.missing10MHz) std::snprintf(b, sizeof b, "no 10 MHz");
  else std::snprintf(b, sizeof b, "%ld ns", (long)d.ns);
  setText(ticMain_, b);
  // The filtered phase is what the firmware locks on; 500 ns is its target.
  if (m.haveStatus(nowMs)) std::snprintf(b, sizeof b, "lock test %+ld of +-100 ns", (long)m.status().ticErrNs);
  else if (d.hasDetail) std::snprintf(b, sizeof b, "average %+.0f ns off target", m.ticErrorNs());
  else b[0] = 0;
  setText(ticSub_, b);

  // DAC tile.
  std::snprintf(b, sizeof b, "%u", (unsigned)d.dac);
  setText(dacMain_, b);
  bool warn = ReportTracker::dacWarning(d.dac);
  setColor(dacMain_, warn ? kRed : kText);
  if (warn) std::snprintf(b, sizeof b, "NEAR LIMIT");
  else std::snprintf(b, sizeof b, "%u%% of range", (unsigned)((uint32_t)d.dac * 100 / 65535));
  setText(dacSub_, b);
  setColor(dacSub_, warn ? kRed : kDim);
  const int bw = 151 - 16;
  lv_obj_set_x(O(dacMarker_), 8 + (int)((long)d.dac * (bw - 3) / 65535));

  // Temperature tile.
  std::snprintf(b, sizeof b, "%.1f C", d.ocxoC);
  setText(tempMain_, b);
  if (d.hasDetail) std::snprintf(b, sizeof b, "ambient %.1f C", d.ambientC);
  else b[0] = 0;
  setText(tempSub_, b);
}

// ---------------------------------------------------------------- Sky plot

static constexpr int kSkyCx = 116, kSkyCy = 110, kSkyR = 100;

void Ui::buildSky() {
  lv_obj_t* s = newScreen();
  screens_[kSky] = s;
  ring(s, kSkyCx, kSkyCy, kSkyR, kLine, 2);
  ring(s, kSkyCx, kSkyCy, kSkyR * 2 / 3, kLine, 1);
  ring(s, kSkyCx, kSkyCy, kSkyR / 3, kLine, 1);
  box(s, kSkyCx, kSkyCy - kSkyR, 1, 2 * kSkyR, kLine, 0);
  box(s, kSkyCx - kSkyR, kSkyCy, 2 * kSkyR, 1, kLine, 0);
  text(s, &lv_font_montserrat_12, kDim, kSkyCx - 4, kSkyCy - kSkyR - 1, "N");
  text(s, &lv_font_montserrat_12, kDim, kSkyCx + kSkyR + 4, kSkyCy - 7, "E");
  text(s, &lv_font_montserrat_12, kDim, kSkyCx - 4, kSkyCy + kSkyR - 13, "S");
  text(s, &lv_font_montserrat_12, kDim, kSkyCx - kSkyR - 13, kSkyCy - 7, "W");
  text(s, &lv_font_montserrat_10, kFaint, kSkyCx + 3, kSkyCy - kSkyR * 2 / 3 + 1, "30");
  text(s, &lv_font_montserrat_10, kFaint, kSkyCx + 3, kSkyCy - kSkyR / 3 + 1, "60");
  for (int i = 0; i < kSkyDots; ++i) {
    lv_obj_t* dot = ring(s, 0, 0, 5, kGreen, 2);
    lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
    skyDot_[i] = dot;
    lv_obj_t* l = text(s, &lv_font_montserrat_10, kDim, 0, 0, "");
    lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
    skyLabel_[i] = l;
  }
  skyEmpty_ = text(s, &lv_font_montserrat_12, kFaint, kSkyCx - 36, kSkyCy + 20, "no satellites");
  text(s, &lv_font_montserrat_12, kText, 236, 8, "SKY");
  skyLegend_ = text(s, &lv_font_montserrat_10, kDim, 236, 28, "");
  lv_obj_set_style_text_line_space(O(skyLegend_), 3, 0);
  // SNR key.
  const uint32_t keyC[4] = {kGreen, kLime, kAmber, kRed};
  const char* keyT[4] = {"40+ dB-Hz", "30-39", "20-29", "under 20"};
  for (int i = 0; i < 4; ++i) {
    box(s, 236, 142 + i * 14, 8, 8, keyC[i], 4);
    text(s, &lv_font_montserrat_10, kDim, 250, 140 + i * 14, keyT[i]);
  }
  text(s, &lv_font_montserrat_10, kFaint, 236, 200, "filled = in fix");
}

void Ui::updateSky(const Monitor& m, uint32_t nowMs) {
  const NmeaParser& g = m.gps();
  int k = 0;
  unsigned seen[7] = {}, used[7] = {};
  for (const auto& s : g.satellites()) {
    if (!s.valid) continue;
    bool u = g.isUsed(s, nowMs);
    if (s.elevation >= 0) {
      ++seen[(int)s.sys];
      if (u) ++used[(int)s.sys];
    }
    if (s.elevation < 0 || s.azimuth < 0 || k >= kSkyDots) continue;
    float r = kSkyR * (90.0f - s.elevation) / 90.0f;
    float a = s.azimuth * 3.14159265f / 180.0f;
    int x = kSkyCx + (int)std::lround(r * std::sin(a));
    int y = kSkyCy - (int)std::lround(r * std::cos(a));
    lv_obj_t* dot = O(skyDot_[k]);
    uint32_t c = snrColor(s.snr);
    lv_obj_set_pos(dot, x - 5, y - 5);
    lv_obj_set_style_border_color(dot, lv_color_hex(c), 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(c), 0);
    lv_obj_set_style_bg_opa(dot, u ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_HIDDEN);
    char b[8];
    std::snprintf(b, sizeof b, "%s%02u", constellationPrefix(s.sys), (unsigned)s.prn);
    lv_obj_t* l = O(skyLabel_[k]);
    setText(l, b);
    setColor(l, u ? kText : kDim);
    lv_obj_set_pos(l, x + 6, y - 6);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_HIDDEN);
    ++k;
  }
  for (int i = k; i < kSkyDots; ++i) {
    lv_obj_add_flag(O(skyDot_[i]), LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(O(skyLabel_[i]), LV_OBJ_FLAG_HIDDEN);
  }
  if (k) lv_obj_add_flag(O(skyEmpty_), LV_OBJ_FLAG_HIDDEN);
  else lv_obj_remove_flag(O(skyEmpty_), LV_OBJ_FLAG_HIDDEN);

  char b[200];
  size_t n = 0;
  b[0] = 0;
  const Constellation order[] = {Constellation::Gps, Constellation::Galileo, Constellation::Glonass,
                                 Constellation::BeiDou, Constellation::Qzss, Constellation::Sbas};
  for (Constellation c : order) {
    int i = (int)c;
    if (!seen[i]) continue;
    n += std::snprintf(b + n, sizeof b - n, "%s%s %u/%u", n ? "\n" : "", sysName(c), used[i], seen[i]);
  }
  if (!n) std::snprintf(b, sizeof b, m.gpsEverSeen() ? "none in view" : "no GPS data");
  setText(skyLegend_, b);
}

// ---------------------------------------------------------------- Signal bars

static constexpr int kBarTop = 34, kBarBottom = 186, kBarMaxSnr = 55;

void Ui::buildBars() {
  lv_obj_t* s = newScreen();
  screens_[kBars] = s;
  text(s, &lv_font_montserrat_12, kText, 8, 8, "SIGNAL");
  barsTitle_ = text(s, &lv_font_montserrat_10, kDim, 70, 4, "");
  barsAntenna_ = text(s, &lv_font_montserrat_10, kDim, 70, 17, "");
  for (int db : {20, 30, 40, 50}) {
    int y = kBarBottom - (kBarBottom - kBarTop) * db / kBarMaxSnr;
    box(s, 26, y, kW - 34, 1, kLine, 0);
    char b[4];
    std::snprintf(b, sizeof b, "%d", db);
    text(s, &lv_font_montserrat_10, kFaint, 8, y - 6, b);
  }
  box(s, 26, kBarBottom, kW - 34, 1, kFaint, 0);
  for (int i = 0; i < kBarCount; ++i) {
    int x = 34 + i * 24;
    bar_[i] = box(s, x, kBarBottom - 1, 16, 1, kGreen, 2);
    barValue_[i] = text(s, &lv_font_montserrat_10, kDim, x + 1, 0, "");
    barPrn_[i] = text(s, &lv_font_montserrat_10, kDim, x - 2, kBarBottom + 4, "");
    lv_obj_add_flag(O(bar_[i]), LV_OBJ_FLAG_HIDDEN);
  }
  barsEmpty_ = text(s, &lv_font_montserrat_12, kFaint, 110, 100, "no satellites");
}

void Ui::updateBars(const Monitor& m, uint32_t nowMs) {
  const NmeaParser& g = m.gps();
  std::vector<const Satellite*> v;
  for (const auto& s : g.satellites())
    if (s.valid && s.elevation >= 0) v.push_back(&s);
  std::sort(v.begin(), v.end(), [&](const Satellite* a, const Satellite* b) {
    bool ua = g.isUsed(*a, nowMs), ub = g.isUsed(*b, nowMs);
    if (a->snr != b->snr) return a->snr > b->snr;
    return ua > ub;
  });
  char b[48];
  std::snprintf(b, sizeof b, "%u in view, %u in the fix, strongest first", (unsigned)v.size(), (unsigned)g.satsUsed(nowMs));
  setText(barsTitle_, v.empty() ? "" : b);
  {
    char a[96];
    const std::string& ant = m.gps().antennaStatus();
    int lo, hi;
    size_t n = 0;
    a[0] = 0;
    if (!ant.empty()) n += std::snprintf(a + n, sizeof a - n, "antenna %s", ant.c_str());
    if (m.top4Snr() >= 0) n += std::snprintf(a + n, sizeof a - n, "%sbest 4 average %d dB-Hz", n ? ", " : "", m.top4Snr());
    if (m.top4Range(lo, hi)) std::snprintf(a + n, sizeof a - n, " (24 h: %d to %d)", lo, hi);
    setText(barsAntenna_, a);
    setColor(barsAntenna_, (!ant.empty() && ant != "OK") ? kRed : kDim);
  }
  for (int i = 0; i < kBarCount; ++i) {
    if (i >= (int)v.size()) {
      lv_obj_add_flag(O(bar_[i]), LV_OBJ_FLAG_HIDDEN);
      setText(barValue_[i], "");
      setText(barPrn_[i], "");
      continue;
    }
    const Satellite& s = *v[i];
    bool u = g.isUsed(s, nowMs);
    int snr = s.snr < 0 ? 0 : std::min<int>(s.snr, kBarMaxSnr);
    int h = std::max(2, (kBarBottom - kBarTop) * snr / kBarMaxSnr);
    lv_obj_t* bar = O(bar_[i]);
    uint32_t c = snrColor(s.snr);
    lv_obj_set_y(bar, kBarBottom - h);
    lv_obj_set_height(bar, h);
    lv_obj_set_style_bg_color(bar, lv_color_hex(c), 0);
    lv_obj_set_style_bg_opa(bar, u ? LV_OPA_COVER : LV_OPA_30, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(c), 0);
    lv_obj_set_style_border_width(bar, u ? 0 : 1, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_HIDDEN);
    if (s.snr >= 0) std::snprintf(b, sizeof b, "%d", s.snr);
    else std::snprintf(b, sizeof b, "-");
    setText(barValue_[i], b);
    lv_obj_set_y(O(barValue_[i]), kBarBottom - h - 13);
    setColor(barValue_[i], u ? kText : kDim);
    std::snprintf(b, sizeof b, "%s%02u", constellationPrefix(s.sys), (unsigned)s.prn);
    setText(barPrn_[i], b);
    setColor(barPrn_[i], u ? kText : kDim);
  }
  if (v.empty()) {
    lv_obj_remove_flag(O(barsEmpty_), LV_OBJ_FLAG_HIDDEN);
    setText(barsEmpty_, m.gpsEverSeen() ? "no satellites" : "no GPS data");
  } else {
    lv_obj_add_flag(O(barsEmpty_), LV_OBJ_FLAG_HIDDEN);
  }
}

// ---------------------------------------------------------------- Trends

static constexpr int kChartPoints = (int)Monitor::kTrendPoints;  // one per 12 s block, 30 minutes
static constexpr int kStripW = kChartPoints * 2;                  // 2 px per block
static uint8_t stripBuf[LV_CANVAS_BUF_SIZE(kStripW, 8, 16, LV_DRAW_BUF_STRIDE_ALIGN)];

// A "nice" axis range around [lo, hi] (steps of 1, 2 or 5 x 10^n), kept steady: the old range is
// reused while the data still fits and fills at least a third of it, so the scale rarely jumps.
static void niceRange(int32_t lo, int32_t hi, int32_t& rlo, int32_t& rhi, bool haveOld) {
  if (haveOld && lo >= rlo && hi <= rhi && (hi - lo) * 3 >= (rhi - rlo)) return;
  int32_t span = std::max<int32_t>(hi - lo, 4);
  int32_t step = 1;
  while (step * 4 < span) {
    if (step * 2 * 4 >= span) { step *= 2; break; }
    if (step * 5 * 4 >= span) { step *= 5; break; }
    step *= 10;
  }
  rlo = (int32_t)std::floor((double)lo / step) * step - step;
  rhi = (int32_t)std::ceil((double)hi / step) * step + step;
}

void Ui::buildTrends() {
  lv_obj_t* s = newScreen();
  screens_[kTrends] = s;
  const char* titles[3] = {"Phase vs GPS pulse, ns", "Phase change per second, ns", "Oscillator tuning (DAC)"};
  const uint32_t colors[3] = {kBlue, kAmber, kGreen};
  for (int i = 0; i < 3; ++i) {
    int y = 4 + i * 60;
    lv_obj_t* c = lv_chart_create(s);
    lv_obj_set_pos(c, 6, y);
    lv_obj_set_size(c, kW - 12, 56);
    lv_obj_set_style_bg_color(c, lv_color_hex(kPanel), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_radius(c, 8, 0);
    lv_obj_set_style_pad_all(c, 4, 0);
    lv_obj_set_style_pad_top(c, 16, 0);
    lv_obj_set_style_line_color(c, lv_color_hex(kLine), LV_PART_MAIN);
    lv_obj_set_style_line_width(c, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(c, 0, 0, LV_PART_INDICATOR);
    lv_chart_set_div_line_count(c, 3, 0);
    lv_chart_set_type(c, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(c, kChartPoints);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    series_[i] = lv_chart_add_series(c, lv_color_hex(colors[i]), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(c, S(series_[i]), LV_CHART_POINT_NONE);
    chart_[i] = c;
    text(c, &lv_font_montserrat_10, kDim, 0, -14, titles[i]);
    chartRange_[i] = text(c, &lv_font_montserrat_10, kFaint, 0, -14, "");
    lv_obj_set_width(O(chartRange_[i]), kW - 20);
    lv_obj_set_style_text_align(O(chartRange_[i]), LV_TEXT_ALIGN_RIGHT, 0);
  }
  // Lock history strip: one 2 px column per 12 s block.
  int sy = 4 + 3 * 60;
  lv_obj_t* cv = lv_canvas_create(s);
  lv_canvas_set_buffer(cv, stripBuf, kStripW, 8, LV_COLOR_FORMAT_RGB565);
  lv_canvas_fill_bg(cv, lv_color_hex(kBg), LV_OPA_COVER);
  lv_obj_set_pos(cv, kW - 6 - kStripW, sy + 2);
  lockStrip_ = cv;
  text(s, &lv_font_montserrat_10, kDim, 8, sy + 12, "Locked: green yes, amber partly, red no");
  lv_obj_t* r = text(s, &lv_font_montserrat_10, kFaint, 0, sy + 12, "30 min ago ... now");
  lv_obj_set_width(r, kW - 12);
  lv_obj_set_style_text_align(r, LV_TEXT_ALIGN_RIGHT, 0);
}

void Ui::updateTrends(const Monitor& m) {
  size_t n = m.trendCount();
  // Right-aligned: the newest block is the last chart point, older ones to its left.
  for (int k = 0; k < 3; ++k) {
    lv_obj_t* c = O(chart_[k]);
    lv_chart_series_t* s = S(series_[k]);
    int32_t lo = INT32_MAX, hi = INT32_MIN, last = 0;
    for (int p = 0; p < kChartPoints; ++p) {
      long idx = (long)p - (kChartPoints - (long)n);
      int32_t v = LV_CHART_POINT_NONE;
      if (idx >= 0) {
        const TrendPoint& t = m.trend((size_t)idx);
        if (t.valid) {
          v = k == 0 ? t.ns : k == 1 ? t.diffNs : t.dac;
          lo = std::min(lo, v);
          hi = std::max(hi, v);
          last = v;
        }
      }
      lv_chart_set_value_by_id(c, s, p, v);
    }
    char b[48];
    if (lo == INT32_MAX) {
      lv_chart_set_range(c, LV_CHART_AXIS_PRIMARY_Y, 0, 1);
      std::snprintf(b, sizeof b, "collecting...");
      haveRange_[k] = false;
    } else {
      niceRange(lo, hi, rangeLo_[k], rangeHi_[k], haveRange_[k]);
      haveRange_[k] = true;
      lv_chart_set_range(c, LV_CHART_AXIS_PRIMARY_Y, rangeLo_[k], rangeHi_[k]);
      std::snprintf(b, sizeof b, "now %ld, 30 min %ld to %ld", (long)last, (long)lo, (long)hi);
    }
    setText(chartRange_[k], b);
    lv_chart_refresh(c);
  }
  lv_obj_t* cv = O(lockStrip_);
  for (int p = 0; p < kChartPoints; ++p) {
    long idx = (long)p - (kChartPoints - (long)n);
    uint32_t col = kBg;
    if (idx >= 0) {
      const TrendPoint& t = m.trend((size_t)idx);
      col = t.lockedAll ? kGreen : t.lockedAny ? kAmber : kRed;
    }
    for (int x = 0; x < 2; ++x)
      for (int y = 0; y < 8; ++y) lv_canvas_set_px(cv, p * 2 + x, y, lv_color_hex(col), LV_OPA_COVER);
  }
  lv_obj_invalidate(cv);
}

// ---------------------------------------------------------------- Time and position

// The clock is drawn one character per fixed cell so the digits don't shuffle as they change.
static constexpr int kClockY = 34;

void Ui::buildTime() {
  lv_obj_t* s = newScreen();
  screens_[kTime] = s;
  text(s, &lv_font_montserrat_12, kText, 8, 8, "UTC");
  dateLabel_ = text(s, &lv_font_montserrat_12, kDim, 0, 8, "");
  lv_obj_align(O(dateLabel_), LV_ALIGN_TOP_RIGHT, -8, 8);
  // HH:MM:SS at 40 px, .s at 28 px.
  const int digitW = 27, colonW = 12, smallW = 19, dotW = 9;
  int widths[kClockChars] = {digitW, digitW, colonW, digitW, digitW, colonW, digitW, digitW, dotW, smallW};
  int total = 0;
  for (int w : widths) total += w;
  int x = (kW - total) / 2;
  for (int i = 0; i < kClockChars; ++i) {
    bool small = i >= 8;
    lv_obj_t* l = text(s, small ? &lv_font_montserrat_28 : &lv_font_montserrat_40, kText, 0,
                       small ? kClockY + 11 : kClockY, "-");
    lv_obj_set_width(l, widths[i]);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_x(l, x);
    x += widths[i];
    clockChar_[i] = l;
  }
  clockSource_ = text(s, &lv_font_montserrat_10, kDim, 0, kClockY + 50, "");
  lv_obj_set_width(O(clockSource_), kW);
  lv_obj_set_style_text_align(O(clockSource_), LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t* t = tile(s, 6, 104, kW - 12, kH - kFooterH - 110, "POSITION");
  locLabel_ = text(t, &lv_font_montserrat_28, kText, 8, 18, "--------");
  latLabel_ = text(t, &lv_font_montserrat_16, kText, 170, 20, "");
  lonLabel_ = text(t, &lv_font_montserrat_16, kText, 170, 42, "");
  altLabel_ = text(t, &lv_font_montserrat_12, kDim, 8, 60, "");
}

void Ui::updateTime(const Monitor& m, uint32_t nowMs) {
  const NmeaParser& g = m.gps();
  const Fix& f = g.fix();
  const UtcTime& u = g.utc();
  char b[64];
  if (u.dateValid) std::snprintf(b, sizeof b, "%04u-%02u-%02u", u.year, u.month, u.day);
  else b[0] = 0;
  setText(dateLabel_, b);
  bool fresh = m.gpsEverSeen() && ageMs(nowMs, g.lastGoodMs()) <= Monitor::kSilentMs;
  if (fresh && f.haveLatLon) {
    setText(locLabel_, maidenhead(f.latDeg, f.lonDeg, 8).c_str());
    std::snprintf(b, sizeof b, "%.5f %c", std::fabs(f.latDeg), f.latDeg < 0 ? 'S' : 'N');
    setText(latLabel_, b);
    std::snprintf(b, sizeof b, "%.5f %c", std::fabs(f.lonDeg), f.lonDeg < 0 ? 'W' : 'E');
    setText(lonLabel_, b);
    if (f.haveAltitude) std::snprintf(b, sizeof b, "%.1f m above sea level, %s fix, %u satellites", f.altitudeM,
                                      f.mode == 3 ? "3D" : "2D", (unsigned)f.satsUsed);
    else b[0] = 0;
    setText(altLabel_, b);
  } else {
    setText(locLabel_, "--------");
    setText(latLabel_, "no fix");
    setText(lonLabel_, "");
    setText(altLabel_, "");
  }
  lastClockDs_ = -1;  // force the clock to redraw at the next fastUpdate
}

void Ui::fastUpdate(const Monitor& m, int64_t nowUs) {
  if (current_ != kTime) return;
  int64_t u;
  bool pps;
  char b[16];
  if (!m.clock().now(nowUs, u, pps)) {
    if (lastClockDs_ == -2) return;
    lastClockDs_ = -2;
    std::snprintf(b, sizeof b, "--:--:--.-");
    setText(clockSource_, m.gpsEverSeen() ? "waiting for GPS time" : "no GPS data");
  } else {
    int64_t t = u % 86400000000LL;  // microseconds of the UTC day
    if (t / 100000 == lastClockDs_) return;
    lastClockDs_ = t / 100000;
    unsigned ds = (unsigned)((t / 100000) % 10), sec = (unsigned)((t / 1000000) % 60),
             min = (unsigned)((t / 60000000) % 60), hr = (unsigned)(t / 3600000000LL);
    std::snprintf(b, sizeof b, "%02u:%02u:%02u.%u", hr, min, sec, ds);
    setText(clockSource_, pps ? "seconds aligned to the GPS pulse" : "from GPS messages only, may lag up to 0.1 s");
  }
  for (int i = 0; i < kClockChars; ++i) {
    char c[2] = {b[i], 0};
    setText(clockChar_[i], c);
  }
}

// ---------------------------------------------------------------- Network

void Ui::buildNetwork() {
  lv_obj_t* s = newScreen();
  screens_[kNetwork] = s;
  netTitle_ = text(s, &lv_font_montserrat_12, kText, 8, 8, "NETWORK");
  const char* keys[kNetRows] = {"Wi-Fi", "Signal", "IP address", "NTP", "MQTT broker", "Broker IP", "MQTT", "Published",
                                "Last error"};
  text(s, &lv_font_montserrat_10, kFaint, 96, 10, "topics gpsdo/state, gpsdo/status");
  for (int i = 0; i < kNetRows; ++i) {
    int y = 28 + i * 19;
    netKey_[i] = text(s, &lv_font_montserrat_12, kDim, 8, y, keys[i]);
    netVal_[i] = text(s, &lv_font_montserrat_12, kText, 96, y, "");
    lv_obj_set_size(O(netVal_[i]), kW - 104, 16);  // one line, "..." if too long
    lv_label_set_long_mode(O(netVal_[i]), LV_LABEL_LONG_DOT);
  }
  lv_obj_t* hint = text(s, &lv_font_montserrat_10, kFaint, 8, kH - kFooterH - 14,
                        "To change these, hold the screen while the display starts up.");
  (void)hint;
}

void Ui::updateNetwork(const UiExtras& x) {
  char b[96];
  auto set = [&](int row, const std::string& v, uint32_t color = kText) {
    setText(netVal_[row], v.empty() ? "-" : v.c_str());
    setColor(netVal_[row], v.empty() ? kFaint : color);
  };
  if (!x.wifiEnabled) {
    set(0, "off (not set up)", kFaint);
    for (int i = 1; i < kNetRows; ++i) set(i, "");
    return;
  }
  set(0, x.ssid + (x.wifiConnected ? "" : "  (not connected)"), x.wifiConnected ? kText : kAmber);
  if (x.wifiConnected) {
    const char* q = x.rssi >= -60 ? "good" : x.rssi >= -72 ? "fair" : "weak";
    std::snprintf(b, sizeof b, "%d dBm (%s), channel %d", x.rssi, q, x.channel);
    set(1, b, x.rssi >= -72 ? kText : kAmber);
  } else {
    set(1, "");
  }
  set(2, x.wifiConnected ? x.ip : "");
  std::snprintf(b, sizeof b, "%s, %u served", x.ntpSynced ? "stratum 1 (GPS)" : "not synced", (unsigned)x.ntpRequests);
  set(3, x.wifiConnected ? b : "", x.ntpSynced ? kGreen : kAmber);
  std::snprintf(b, sizeof b, "%s:%u", x.mqttHost.c_str(), (unsigned)x.mqttPort);
  set(4, b);
  set(5, x.mqttIp);
  if (x.mqttConnected) {
    char d[24];
    fmtDuration(d, sizeof d, x.mqttUpS);
    std::snprintf(b, sizeof b, "connected for %s", d);
    set(6, b, kGreen);
  } else {
    set(6, "not connected", kAmber);
  }
  if (x.lastPublishAgeS != UINT32_MAX) {
    std::snprintf(b, sizeof b, "%u, last %us ago", (unsigned)x.publishes, (unsigned)x.lastPublishAgeS);
    set(7, b);
  } else {
    set(7, "");
  }
  if (x.lastError.empty()) {
    set(8, "none");
    setColor(netVal_[8], kFaint);
  } else {
    char ago[24];
    fmtDuration(ago, sizeof ago, x.lastErrorAgeS);
    std::snprintf(b, sizeof b, "%s ago: %s", ago, x.lastError.c_str());
    set(8, b, x.mqttConnected ? kDim : kAmber);  // dim once it has recovered
  }
}


// ---------------------------------------------------------------- Accuracy

static const struct { const char* name; float mhz; } kBands[6] = {
    {"3.5 MHz", 3.5f}, {"7 MHz", 7.0f}, {"14 MHz", 14.0f}, {"28 MHz", 28.0f}, {"50 MHz", 50.0f}, {"144 MHz", 144.0f}};

void Ui::buildAccuracy() {
  lv_obj_t* s = newScreen();
  screens_[kAccuracy] = s;
  text(s, &lv_font_montserrat_12, kText, 8, 8, "10 MHz ACCURACY");
  text(s, &lv_font_montserrat_10, kFaint, 140, 10, "measured against the GPS pulse");
  accBig_ = text(s, &lv_font_montserrat_28, kText, 8, 28, "--");
  accBigNote_ = text(s, &lv_font_montserrat_10, kDim, 8, 62, "");
  for (int i = 0; i < 3; ++i) accRow_[i] = text(s, &lv_font_montserrat_12, kText, 8, 82 + i * 17, "");
  lv_obj_t* t = tile(s, 6, 136, kW - 12, kH - kFooterH - 140, "WHAT THAT MEANS ON AIR (30-MINUTE FIGURE)");
  for (int i = 0; i < 6; ++i) {
    accBand_[i] = text(t, &lv_font_montserrat_12, kText, 8 + (i % 3) * 100, 20 + (i / 3) * 18, "");
  }
  accNote_ = text(s, &lv_font_montserrat_10, kFaint, 8, 0, "");
  lv_obj_add_flag(O(accNote_), LV_OBJ_FLAG_HIDDEN);
}

static void fmtHz(char* out, size_t n, double hz) {
  double a = std::fabs(hz);
  if (a < 1e-3) std::snprintf(out, n, "%.0f uHz", a * 1e6);
  else if (a < 1.0) std::snprintf(out, n, "%.1f mHz", a * 1e3);
  else std::snprintf(out, n, "%.2f Hz", a);
}

void Ui::updateAccuracy(const Monitor& m) {
  char b[64];
  float ppb[3];
  bool have[3];
  const uint32_t win[3] = {60, 300, 1800};
  const char* names[3] = {"1 minute", "5 minutes", "30 minutes"};
  for (int i = 0; i < 3; ++i) have[i] = m.frequencyPpb(win[i], ppb[i]);
  int best = have[2] ? 2 : have[1] ? 1 : have[0] ? 0 : -1;
  if (best < 0) {
    setText(accBig_, "--");
    setText(accBigNote_, m.nano().locked() ? "needs a minute of locked data" : "only measured while locked");
  } else {
    char hz[24];
    fmtHz(hz, sizeof hz, ppb[best] * 1e-9 * 10e6);
    std::snprintf(b, sizeof b, "%+.3f ppb", ppb[best]);
    setText(accBig_, b);
    std::snprintf(b, sizeof b, "average over %s, so 10 MHz is off by %s", names[best], hz);
    setText(accBigNote_, b);
  }
  for (int i = 0; i < 3; ++i) {
    if (have[i]) std::snprintf(b, sizeof b, "over %s: %+.3f ppb", names[i], ppb[i]);
    else std::snprintf(b, sizeof b, "over %s: not enough locked data yet", names[i]);
    setText(accRow_[i], b);
    setColor(accRow_[i], have[i] ? kText : kFaint);
  }
  for (int i = 0; i < 6; ++i) {
    if (have[2]) {
      char hz[24];
      fmtHz(hz, sizeof hz, ppb[2] * 1e-9 * kBands[i].mhz * 1e6);
      std::snprintf(b, sizeof b, "%s %s", kBands[i].name, hz);
    } else {
      std::snprintf(b, sizeof b, "%s --", kBands[i].name);
    }
    setText(accBand_[i], b);
  }
}

// ---------------------------------------------------------------- History (the GPSDO's own memory)

static constexpr int kHistPoints = NanoHistory::kSize;
static uint8_t histStripBuf[LV_CANVAS_BUF_SIZE(kHistPoints * 2, 8, 16, LV_DRAW_BUF_STRIDE_ALIGN)];

void Ui::buildHistory() {
  lv_obj_t* s = newScreen();
  screens_[kHistory] = s;
  const char* titles[3] = {"Oscillator tuning (DAC), 18 days", "Oscillator temperature C, 18 days",
                           "Phase vs target ns, 12 hours"};
  const uint32_t colors[3] = {kGreen, kAmber, kBlue};
  for (int i = 0; i < 3; ++i) {
    int y = 4 + i * 60;
    lv_obj_t* c = lv_chart_create(s);
    lv_obj_set_pos(c, 6, y);
    lv_obj_set_size(c, kW - 12, 56);
    lv_obj_set_style_bg_color(c, lv_color_hex(kPanel), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_radius(c, 8, 0);
    lv_obj_set_style_pad_all(c, 4, 0);
    lv_obj_set_style_pad_top(c, 16, 0);
    lv_obj_set_style_line_color(c, lv_color_hex(kLine), LV_PART_MAIN);
    lv_obj_set_style_line_width(c, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(c, 0, 0, LV_PART_INDICATOR);
    lv_chart_set_div_line_count(c, 3, 0);
    lv_chart_set_type(c, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(c, kHistPoints);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    histSeries_[i] = lv_chart_add_series(c, lv_color_hex(colors[i]), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(c, S(histSeries_[i]), LV_CHART_POINT_NONE);
    histChart_[i] = c;
    text(c, &lv_font_montserrat_10, kDim, 0, -14, titles[i]);
    histRange_[i] = text(c, &lv_font_montserrat_10, kFaint, 0, -14, "");
    lv_obj_set_width(O(histRange_[i]), kW - 20);
    lv_obj_set_style_text_align(O(histRange_[i]), LV_TEXT_ALIGN_RIGHT, 0);
  }
  int sy = 4 + 3 * 60;
  lv_obj_t* cv = lv_canvas_create(s);
  lv_canvas_set_buffer(cv, histStripBuf, kHistPoints * 2, 8, LV_COLOR_FORMAT_RGB565);
  lv_canvas_fill_bg(cv, lv_color_hex(kBg), LV_OPA_COVER);
  lv_obj_set_pos(cv, kW - 6 - kHistPoints * 2, sy + 2);
  histStrip_ = cv;
  histNote_ = text(s, &lv_font_montserrat_10, kDim, 8, sy + 12, "");
}

void Ui::updateHistory(const Monitor& m) {
  const NanoHistory& h = m.history();
  // Redraw only when something new has arrived (the whole set refreshes every 5 minutes).
  uint32_t sig = (uint32_t)h.seen(true) * 1000 + (uint32_t)h.seen(false) + (uint32_t)m.nano().last().time / 300 * 7;
  if (sig == histSig_) return;
  histSig_ = sig;
  auto three = h.ordered(false);
  auto five = h.ordered(true);
  for (int k = 0; k < 3; ++k) {
    const auto& src = k < 2 ? three : five;
    lv_obj_t* c = O(histChart_[k]);
    lv_chart_series_t* ser = S(histSeries_[k]);
    int32_t lo = INT32_MAX, hi = INT32_MIN;
    for (int p = 0; p < kHistPoints; ++p) {
      long idx = (long)p - (kHistPoints - (long)src.size());
      int32_t v = LV_CHART_POINT_NONE;
      if (idx >= 0) {
        const HistoryEntry& e = src[(size_t)idx];
        v = k == 0 ? e.dac : k == 1 ? (int32_t)std::lround(e.tempC * 10) : (e.ticX10 - GpsdoConstants::kTicOffsetNs * 10);
        lo = std::min(lo, v);
        hi = std::max(hi, v);
      }
      lv_chart_set_value_by_id(c, ser, p, v);
    }
    char b[48];
    if (lo == INT32_MAX) {
      lv_chart_set_range(c, LV_CHART_AXIS_PRIMARY_Y, 0, 1);
      std::snprintf(b, sizeof b, "collecting...");
    } else {
      int32_t rlo = 0, rhi = 0;
      niceRange(lo, hi, rlo, rhi, false);
      lv_chart_set_range(c, LV_CHART_AXIS_PRIMARY_Y, rlo, rhi);
      if (k == 0) std::snprintf(b, sizeof b, "%ld to %ld", (long)lo, (long)hi);
      else if (k == 1) std::snprintf(b, sizeof b, "%.1f to %.1f", lo / 10.0, hi / 10.0);
      else std::snprintf(b, sizeof b, "%.0f to %.0f", lo / 10.0, hi / 10.0);
    }
    setText(histRange_[k], b);
    lv_chart_refresh(c);
  }
  lv_obj_t* cv = O(histStrip_);
  for (int p = 0; p < kHistPoints; ++p) {
    long idx = (long)p - (kHistPoints - (long)three.size());
    uint32_t col = kBg;
    if (idx >= 0) {
      char l = three[(size_t)idx].lock;
      col = l == 'L' ? kGreen : l == 'U' ? kRed : l == ' ' ? kGrey : kAmber;
    }
    for (int x = 0; x < 2; ++x)
      for (int y = 0; y < 8; ++y) lv_canvas_set_px(cv, p * 2 + x, y, lv_color_hex(col), LV_OPA_COVER);
  }
  lv_obj_invalidate(cv);
  char b[96];
  std::snprintf(b, sizeof b, "Locked all 3 h: green yes, red no. From the GPSDO (%d/144)", h.seen(false));
  setText(histNote_, b);
}

// ---------------------------------------------------------------- Events

void Ui::buildEvents() {
  lv_obj_t* s = newScreen();
  screens_[kEvents] = s;
  text(s, &lv_font_montserrat_12, kText, 8, 8, "EVENTS");
  text(s, &lv_font_montserrat_10, kFaint, 70, 10, "newest first, UTC, kept across restarts");
  for (int i = 0; i < kEventRows; ++i) {
    int y = 28 + i * 20;
    evDot_[i] = box(s, 8, y + 4, 8, 8, kGrey, 4);
    evWhen_[i] = text(s, &lv_font_montserrat_10, kDim, 22, y + 1, "");
    evText_[i] = text(s, &lv_font_montserrat_12, kText, 100, y, "");
    lv_obj_set_size(O(evText_[i]), kW - 106, 16);
    lv_label_set_long_mode(O(evText_[i]), LV_LABEL_LONG_DOT);
  }
  evEmpty_ = text(s, &lv_font_montserrat_12, kFaint, 8, 40, "nothing yet");
}

void Ui::updateEvents(const Monitor& m) {
  const EventLog& log = m.events();
  static const char* kMon[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  for (int i = 0; i < kEventRows; ++i) {
    bool have = i < log.count();
    if (!have) {
      lv_obj_add_flag(O(evDot_[i]), LV_OBJ_FLAG_HIDDEN);
      setText(evWhen_[i], "");
      setText(evText_[i], "");
      continue;
    }
    const Event& e = log.newest(i);
    char w[24];
    if (e.unixSec > 0) {
      int64_t days = e.unixSec / 86400, sod = e.unixSec % 86400;
      // civil from days (Howard Hinnant)
      int64_t z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
      unsigned doe = (unsigned)(z - era * 146097);
      unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
      unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
      unsigned mp = (5 * doy + 2) / 153, d = doy - (153 * mp + 2) / 5 + 1, mo = mp < 10 ? mp + 3 : mp - 9;
      std::snprintf(w, sizeof w, "%u %s %02u:%02u", d, kMon[mo - 1], (unsigned)(sod / 3600), (unsigned)(sod / 60 % 60));
    } else {
      std::snprintf(w, sizeof w, "time unknown");
    }
    setText(evWhen_[i], w);
    setText(evText_[i], e.text);
    lv_obj_remove_flag(O(evDot_[i]), LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(O(evDot_[i]), lv_color_hex(e.level == 0 ? kGreen : e.level == 1 ? kAmber : kRed), 0);
  }
  if (log.count()) lv_obj_add_flag(O(evEmpty_), LV_OBJ_FLAG_HIDDEN);
  else lv_obj_remove_flag(O(evEmpty_), LV_OBJ_FLAG_HIDDEN);
}


// ---------------------------------------------------------------- Stability (Allan deviation)

static constexpr int kPlotX = 46, kPlotY = 26, kPlotW = 264, kPlotH = 112;
static constexpr double kAdevTop = -7.0, kAdevDecades = 5.0;  // 1e-7 down to 1e-12
static lv_point_precise_t stabPts[Stability::kTaus];

static int plotX(double tau) { return kPlotX + (int)std::lround(kPlotW * std::log10(tau) / 3.0); }
static int plotY(double adev) {
  double v = std::log10(adev);
  if (v > kAdevTop) v = kAdevTop;
  if (v < kAdevTop - kAdevDecades) v = kAdevTop - kAdevDecades;
  return kPlotY + (int)std::lround(kPlotH * (kAdevTop - v) / kAdevDecades);
}

void Ui::buildStability() {
  lv_obj_t* s = newScreen();
  screens_[kStability] = s;
  text(s, &lv_font_montserrat_12, kText, 8, 6, "STABILITY");
  text(s, &lv_font_montserrat_10, kFaint, 90, 8, "Allan deviation against GPS");
  box(s, kPlotX, kPlotY, kPlotW, kPlotH, kPanel, 0);
  for (int d = 0; d <= 5; ++d) {
    int y = kPlotY + kPlotH * d / 5;
    box(s, kPlotX, y, kPlotW, 1, kLine, 0);
    char b[8];
    std::snprintf(b, sizeof b, "1e%d", (int)kAdevTop - d);
    text(s, &lv_font_montserrat_10, kFaint, 10, y - 6, b);
  }
  const char* tl[4] = {"1 s", "10 s", "100 s", "1000 s"};
  for (int d = 0; d <= 3; ++d) {
    int x = kPlotX + kPlotW * d / 3;
    box(s, x, kPlotY, 1, kPlotH, kLine, 0);
    text(s, &lv_font_montserrat_10, kFaint, x - (d == 3 ? 30 : 6), kPlotY + kPlotH + 2, tl[d]);
  }
  lv_obj_t* line = lv_line_create(s);
  lv_obj_set_style_line_color(line, lv_color_hex(kBlue), 0);
  lv_obj_set_style_line_width(line, 2, 0);
  lv_obj_set_pos(line, 0, 0);
  stabLine_ = line;
  for (int i = 0; i < Stability::kTaus; ++i) {
    stabDot_[i] = box(s, 0, 0, 6, 6, kBlue, 3);
    lv_obj_add_flag(O(stabDot_[i]), LV_OBJ_FLAG_HIDDEN);
  }
  stabEmpty_ = text(s, &lv_font_montserrat_12, kFaint, kPlotX + 30, kPlotY + 46, "needs a few minutes of locked data");
  stabNote_ = text(s, &lv_font_montserrat_10, kDim, 8, kPlotY + kPlotH + 16, "");
  tempNote_ = text(s, &lv_font_montserrat_10, kDim, 8, kPlotY + kPlotH + 30, "");
  lv_obj_set_width(O(tempNote_), kW - 16);
  lv_label_set_long_mode(O(tempNote_), LV_LABEL_LONG_WRAP);
}

void Ui::updateStability(const Monitor& m) {
  int n = 0;
  for (int i = 0; i < Stability::kTaus; ++i) {
    double a;
    if (!m.stability().adev(Stability::kTau[i], a) || a <= 0) {
      lv_obj_add_flag(O(stabDot_[i]), LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    int x = plotX(Stability::kTau[i]), y = plotY(a);
    stabPts[n].x = x;
    stabPts[n].y = y;
    ++n;
    lv_obj_set_pos(O(stabDot_[i]), x - 3, y - 3);
    lv_obj_remove_flag(O(stabDot_[i]), LV_OBJ_FLAG_HIDDEN);
  }
  lv_line_set_points(O(stabLine_), stabPts, (uint32_t)n);
  if (n) lv_obj_add_flag(O(stabEmpty_), LV_OBJ_FLAG_HIDDEN);
  else lv_obj_remove_flag(O(stabEmpty_), LV_OBJ_FLAG_HIDDEN);
  char b[140];
  std::snprintf(b, sizeof b, "%u min locked. Left: GPS pulse jitter. Right: oscillator + loop.",
                (unsigned)(m.stability().samples() / 60));
  setText(stabNote_, b);
  TempFit f = fitTemperature(m.history(), 655);
  if (f.points >= 8)
    std::snprintf(b, sizeof b,
                  "Temperature: tuning moves %+.1f per C (explains %.0f%% of it, %d blocks). Would need tempRef %ld, "
                  "tempCoeff %ld. Not applied.",
                  f.slopeDacPerC, f.r2 * 100, f.points, f.suggestedTempRef, f.suggestedTempCoeff);
  else
    std::snprintf(b, sizeof b, "Temperature fit: waiting for the GPSDO's 3-hour history.");
  setText(tempNote_, b);
}

// ---------------------------------------------------------------- Sky map (long-term signal by direction)

static constexpr int kMapSize = 150, kMapR = 72;

static void freeCanvasBuf(lv_event_t* e) {
  lv_draw_buf_t* buf = static_cast<lv_draw_buf_t*>(lv_event_get_user_data(e));
  if (buf) lv_draw_buf_destroy(buf);
}

void Ui::buildSkyMap() {
  lv_obj_t* s = newScreen();
  screens_[kSkyMap] = s;
  text(s, &lv_font_montserrat_12, kText, 8, 6, "SKY MAP");
  text(s, &lv_font_montserrat_10, kFaint, 82, 8, "average signal by direction");
  // The canvas buffer (45 KB) exists only while this screen is shown.
  lv_draw_buf_t* buf = lv_draw_buf_create(kMapSize, kMapSize, LV_COLOR_FORMAT_RGB565, 0);
  lv_obj_t* cv = lv_canvas_create(s);
  if (buf) {
    lv_canvas_set_draw_buf(cv, buf);
    lv_obj_add_event_cb(cv, freeCanvasBuf, LV_EVENT_DELETE, buf);
  }
  lv_obj_set_pos(cv, 30, 34);
  skyMapCanvas_ = buf ? cv : nullptr;
  int cx = 30 + kMapSize / 2, cy = 34 + kMapSize / 2;
  ring(s, cx, cy, kMapR, kLine, 1);
  ring(s, cx, cy, kMapR * 2 / 3, kLine, 1);
  ring(s, cx, cy, kMapR / 3, kLine, 1);
  text(s, &lv_font_montserrat_10, kDim, cx - 3, cy - kMapR - 14, "N");
  text(s, &lv_font_montserrat_10, kDim, cx + kMapR + 4, cy - 6, "E");
  text(s, &lv_font_montserrat_10, kDim, cx - 3, cy + kMapR + 2, "S");
  text(s, &lv_font_montserrat_10, kDim, cx - kMapR - 12, cy - 6, "W");
  const uint32_t keyC[5] = {kGreen, kLime, kAmber, kRed, kPanel};
  const char* keyT[5] = {"40+ dB-Hz", "30-39", "20-29", "under 20", "not seen yet"};
  for (int i = 0; i < 5; ++i) {
    box(s, 210, 50 + i * 16, 9, 9, keyC[i], 2);
    text(s, &lv_font_montserrat_10, kDim, 224, 48 + i * 16, keyT[i]);
  }
  skyMapNote_ = text(s, &lv_font_montserrat_10, kDim, 210, 140, "");
  lv_obj_set_width(O(skyMapNote_), 104);
  lv_label_set_long_mode(O(skyMapNote_), LV_LABEL_LONG_WRAP);
}

void Ui::updateSkyMap(const Monitor& m) {
  const SkyMap& k = m.skyMap();
  char b[96];
  std::snprintf(b, sizeof b, "%u readings. Weak or empty areas: blocked sky.", (unsigned)k.samples());
  setText(skyMapNote_, b);
  if (!skyMapCanvas_) return;
  uint32_t sig = k.samples() / 20;  // redraw when a few new readings have come in
  if (sig == skyMapSig_) return;
  skyMapSig_ = sig;
  lv_obj_t* cv = O(skyMapCanvas_);
  const float r0 = kMapSize / 2.0f;
  for (int y = 0; y < kMapSize; ++y)
    for (int x = 0; x < kMapSize; ++x) {
      float dx = x - r0 + 0.5f, dy = y - r0 + 0.5f, r = std::sqrt(dx * dx + dy * dy);
      uint32_t c = kBg;
      if (r <= kMapR) {
        int el = (int)(90.0f * (1.0f - r / kMapR));
        int az = (int)(std::atan2(dx, -dy) * 180.0f / 3.14159265f);
        if (az < 0) az += 360;
        float v = k.mean(el >= 90 ? SkyMap::kEl - 1 : el / 10, (az % 360) / 10);
        c = v < 0 ? kPanel : snrColor((int)v);
      }
      lv_canvas_set_px(cv, x, y, lv_color_hex(c), LV_OPA_COVER);
    }
  lv_obj_invalidate(cv);
}

// ---------------------------------------------------------------- Wi-Fi setup

void Ui::showSetup(const char* apSsid, const char* apPass, const char* url, const char* status, uint32_t secondsLeft) {
  if (!setup_) {
    lv_obj_t* s = newScreen();
    setup_ = s;
    text(s, &lv_font_montserrat_20, kText, 12, 10, "Wi-Fi setup");
    text(s, &lv_font_montserrat_12, kDim, 12, 44, "1. On your phone, join the Wi-Fi network");
    setupNet_ = text(s, &lv_font_montserrat_20, kBlue, 24, 62, "");
    text(s, &lv_font_montserrat_12, kDim, 12, 92, "   with the password");
    setupPass_ = text(s, &lv_font_montserrat_20, kBlue, 24, 110, "");
    text(s, &lv_font_montserrat_12, kDim, 12, 140, "2. The setup page should open. If not, browse to");
    setupUrl_ = text(s, &lv_font_montserrat_16, kBlue, 24, 158, "");
    setupStatus_ = text(s, &lv_font_montserrat_12, kAmber, 12, 188, "");
    lv_obj_set_width(O(setupStatus_), kW - 24);
    lv_label_set_long_mode(O(setupStatus_), LV_LABEL_LONG_WRAP);
    setupLeft_ = text(s, &lv_font_montserrat_10, kFaint, 12, 224, "");
  }
  setText(setupNet_, apSsid);
  setText(setupPass_, apPass);
  setText(setupUrl_, url);
  setText(setupStatus_, status);
  char b[48];
  std::snprintf(b, sizeof b, "closes in %u:%02u, then Wi-Fi stays off", (unsigned)(secondsLeft / 60),
                (unsigned)(secondsLeft % 60));
  setText(setupLeft_, b);
  if (lv_screen_active() != O(setup_)) lv_screen_load(O(setup_));
}

// ---------------------------------------------------------------- Footer and dispatch

void Ui::updateFooter(int i, const Monitor& m, const UiExtras& x, uint32_t nowMs) {
  char b[40];
  uint32_t bg;
  const char* verdict;
  if (m.referenceOk()) { bg = kGreen; verdict = "10 MHz OK"; }
  else if (m.nano().locked() && !m.gpsEverSeen() && !(m.alarms() & (kAlarmNanoSilent | kAlarmNoPps))) {
    bg = kAmber; verdict = "10 MHz UNCHECKED";  // locked, but no GPS data to confirm it
  } else { bg = kRed; verdict = "10 MHz UNRELIABLE"; }
  lv_obj_set_style_bg_color(O(footer_[i]), lv_color_hex(bg), 0);
  setText(footerRef_[i], verdict);
  auto age = [](char* out, size_t n, uint32_t ms) {
    if (ms == UINT32_MAX) std::snprintf(out, n, "--");
    else if (ms < 100000) std::snprintf(out, n, "%us", (unsigned)(ms / 1000));
    else std::snprintf(out, n, ">99s");
  };
  char na[8], ga[8];
  age(na, sizeof na, m.msSinceNanoLine(nowMs));
  age(ga, sizeof ga, m.gpsEverSeen() ? ageMs(nowMs, m.gps().lastGoodMs()) : UINT32_MAX);
  const char* net = !x.wifiEnabled ? "offline" : !x.wifiConnected ? "no Wi-Fi" : !x.mqttConnected ? "no MQTT" : "MQTT";
  std::snprintf(b, sizeof b, "GPSDO %s  GPS %s  %s", na, ga, net);
  setText(footerNano_[i], b);
  std::snprintf(b, sizeof b, "%d/%d", i + 1, kScreens);
  setText(footerPage_[i], b);
}

void Ui::update(const Monitor& m, const UiExtras& x, uint32_t nowMs) {
  switch (current_) {
    case kStatus: updateStatus(m, nowMs); break;
    case kSky: updateSky(m, nowMs); break;
    case kBars: updateBars(m, nowMs); break;
    case kTrends: updateTrends(m); break;
    case kTime: updateTime(m, nowMs); break;
    case kNetwork: updateNetwork(x); break;
    case kAccuracy: updateAccuracy(m); break;
    case kHistory: updateHistory(m); break;
    case kEvents: updateEvents(m); break;
    case kStability: updateStability(m); break;
    case kSkyMap: updateSkyMap(m); break;
  }
  updateFooter(current_, m, x, nowMs);
}

}  // namespace gpsdo
