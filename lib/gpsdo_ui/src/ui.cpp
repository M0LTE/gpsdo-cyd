#include "ui.h"

#include "locator.h"

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
constexpr int kLockCells = 60;

lv_obj_t* O(void* p) { return static_cast<lv_obj_t*>(p); }
lv_chart_series_t* S(void* p) { return static_cast<lv_chart_series_t*>(p); }

lv_obj_t* lockCells[kLockCells];

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

void Ui::begin() {
  buildStatus();
  buildSky();
  buildBars();
  buildTrends();
  buildTime();
  buildNetwork();
  for (int i = 0; i < kScreens; ++i) {
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
  show(kStatus);
}

void Ui::show(int screen) {
  current_ = screen;
  lv_screen_load(O(screens_[screen]));
}

// ---------------------------------------------------------------- Status

void Ui::buildStatus() {
  lv_obj_t* s = newScreen();
  screens_[kStatus] = s;
  banner_ = box(s, 6, 6, kW - 12, 78, kGrey, 10);
  headline_ = text(O(banner_), &lv_font_montserrat_40, 0xFFFFFF, 12, 4, "NO DATA");
  subline_ = text(O(banner_), &lv_font_montserrat_14, 0xFFFFFF, 14, 50, "waiting for the GPSDO");
  lv_obj_set_style_text_opa(O(subline_), LV_OPA_80, 0);
  lv_obj_set_width(O(subline_), kW - 12 - 28);
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

  lv_obj_t* t = tile(s, 6 + w + gap, top, w, h, "TIC");
  ticMain_ = text(t, &lv_font_montserrat_20, kText, 8, 18, "--");
  ticSub_ = text(t, &lv_font_montserrat_12, kDim, 8, 44, "");

  lv_obj_t* d = tile(s, 6, top + h + gap, w, h, "DAC");
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

  lv_obj_t* tt = tile(s, 6 + w + gap, top + h + gap, w, h, "TEMPERATURE");
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
      fmtDuration(dur, sizeof dur, n.secondsLocked());
      std::snprintf(b, sizeof b, "for %s%s, %u lock loss%s", dur, n.lockedSinceKnown() ? "" : "+",
                    (unsigned)n.lockLosses(), n.lockLosses() == 1 ? "" : "es");
      if (m.gpsdoRestarts() || m.gpsRestarts()) {
        char ago[24];
        fmtDuration(ago, sizeof ago, (nowMs - m.lastRestartMs()) / 1000);
        std::snprintf(b, sizeof b, "for %s, restarted %s ago", dur, ago);
      }
      break;
    case Headline::RefSuspect:
      std::snprintf(b, sizeof b, "but the GPS has no good fix");
      break;
    case Headline::Unlocked: {
      uint32_t e = m.earliestLockS();
      if (e) {
        fmtDuration(dur, sizeof dur, e);
        std::snprintf(b, sizeof b, "TIC %+.0f ns, lock in %s at best", m.ticErrorNs(), dur);
      } else {
        std::snprintf(b, sizeof b, "TIC %+.0f ns off target (limit 100)", m.ticErrorNs());
      }
      break;
    }
    case Headline::NoPps:
      if ((m.gpsdoRestarts() || m.gpsRestarts()) && nowMs - m.lastRestartMs() < 3600000UL) {
        fmtDuration(dur, sizeof dur, (nowMs - m.lastRestartMs()) / 1000);
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
    const char* mode = (m.alarms() & kAlarmGpsNoFix) ? "NO FIX" : (f.mode == 3 ? "3D" : f.mode == 2 ? "2D" : "FIX");
    unsigned used = f.haveGga ? f.satsUsed : (unsigned)g.satsUsed(nowMs);
    std::snprintf(b, sizeof b, "%s  %u/%u", mode, used, (unsigned)g.satsInView());
    setText(gpsMain_, b);
    setColor(gpsMain_, (m.alarms() & (kAlarmGpsNoFix | kAlarmGpsFewSats | kAlarmGpsSilent)) ? kRed : kText);
    std::snprintf(b, sizeof b, "used/seen   HDOP %.2f", f.hdop);
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
  if (d.hasDetail) std::snprintf(b, sizeof b, "diff %ld  err %+.1f  tc %u", (long)d.diffNs, m.ticErrorNs(), (unsigned)d.tc);
  else b[0] = 0;
  setText(ticSub_, b);

  // DAC tile.
  std::snprintf(b, sizeof b, "%u", (unsigned)d.dac);
  setText(dacMain_, b);
  bool warn = ReportTracker::dacWarning(d.dac);
  setColor(dacMain_, warn ? kRed : kText);
  setText(dacSub_, warn ? "NEAR LIMIT" : "");
  setColor(dacSub_, kRed);
  const int bw = 151 - 16;
  lv_obj_set_x(O(dacMarker_), 8 + (int)((long)d.dac * (bw - 3) / 65535));

  // Temperature tile.
  std::snprintf(b, sizeof b, "OCXO %.1f C", d.ocxoC);
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
  barsTitle_ = text(s, &lv_font_montserrat_10, kDim, 70, 10, "");
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
  std::snprintf(b, sizeof b, "%u in view, %u in fix, strongest first", (unsigned)v.size(), (unsigned)g.satsUsed(nowMs));
  setText(barsTitle_, v.empty() ? "" : b);
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

static constexpr int kChartPoints = 150;

void Ui::buildTrends() {
  lv_obj_t* s = newScreen();
  screens_[kTrends] = s;
  const char* titles[3] = {"TIC ns", "diff ns", "DAC"};
  const uint32_t colors[3] = {kBlue, kAmber, kGreen};
  for (int i = 0; i < 3; ++i) {
    int y = 4 + i * 64;
    lv_obj_t* c = lv_chart_create(s);
    lv_obj_set_pos(c, 6, y);
    lv_obj_set_size(c, kW - 12, 58);
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
    lv_chart_set_update_mode(c, LV_CHART_UPDATE_MODE_SHIFT);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    series_[i] = lv_chart_add_series(c, lv_color_hex(colors[i]), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(c, S(series_[i]), LV_CHART_POINT_NONE);
    chart_[i] = c;
    text(c, &lv_font_montserrat_10, kDim, 0, -14, titles[i]);
    chartRange_[i] = text(c, &lv_font_montserrat_10, kFaint, 60, -14, "");
  }
  // Lock state strip under the charts: one cell per 30 s, green when locked.
  int sy = 4 + 3 * 64;
  for (int i = 0; i < kLockCells; ++i) {
    lockCells[i] = box(s, 6 + i * (kW - 12) / kLockCells, sy, (kW - 12) / kLockCells + 1, 6, kGrey, 0);
  }
  text(s, &lv_font_montserrat_10, kFaint, 6, sy + 8, "-30 min");
  text(s, &lv_font_montserrat_10, kFaint, 136, sy + 8, "locked");
  text(s, &lv_font_montserrat_10, kFaint, kW - 30, sy + 8, "now");
}

void Ui::updateTrends(const Monitor& m) {
  size_t n = m.trendCount();
  const size_t total = Monitor::kTrendPoints;
  const size_t per = total / kChartPoints;  // trend points per chart point
  for (int k = 0; k < 3; ++k) {
    lv_obj_t* c = O(chart_[k]);
    lv_chart_series_t* s = S(series_[k]);
    int32_t lo = INT32_MAX, hi = INT32_MIN;
    for (int p = 0; p < kChartPoints; ++p) {
      // Chart point p covers trend slots [p*per, (p+1)*per) of a full 30-minute window ending now.
      long start = (long)p * per - (long)(total - n);
      long sum = 0;
      int cnt = 0;
      for (size_t j = 0; j < per; ++j) {
        long idx = start + (long)j;
        if (idx < 0 || idx >= (long)n) continue;
        const TrendPoint& t = m.trend((size_t)idx);
        if (!t.valid) continue;
        sum += k == 0 ? t.ns : k == 1 ? t.diffNs : t.dac;
        ++cnt;
      }
      int32_t v = cnt ? (int32_t)(sum / cnt) : LV_CHART_POINT_NONE;
      lv_chart_set_value_by_id(c, s, p, v);
      if (cnt) { lo = std::min(lo, v); hi = std::max(hi, v); }
    }
    char b[40];
    if (lo == INT32_MAX) {
      lv_chart_set_range(c, LV_CHART_AXIS_PRIMARY_Y, 0, 1);
      b[0] = 0;
    } else {
      int32_t pad = std::max<int32_t>(2, (hi - lo) / 8);
      lv_chart_set_range(c, LV_CHART_AXIS_PRIMARY_Y, lo - pad, hi + pad);
      std::snprintf(b, sizeof b, "%ld to %ld", (long)lo, (long)hi);
    }
    setText(chartRange_[k], b);
    lv_chart_refresh(c);
  }
  // Lock strip.
  const size_t perCell = total / kLockCells;
  for (int i = 0; i < kLockCells; ++i) {
    long start = (long)i * perCell - (long)(total - n);
    int locked = 0, other = 0;
    for (size_t j = 0; j < perCell; ++j) {
      long idx = start + (long)j;
      if (idx < 0 || idx >= (long)n) continue;
      if (m.trend((size_t)idx).status == (uint8_t)LockStatus::Locked) ++locked;
      else ++other;
    }
    uint32_t c = !(locked + other) ? kBg : other ? (locked ? kAmber : kRed) : kGreen;
    lv_obj_set_style_bg_color(lockCells[i], lv_color_hex(c), 0);
  }
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
  bool fresh = m.gpsEverSeen() && nowMs - g.lastGoodMs() <= Monitor::kSilentMs;
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
    std::snprintf(b, sizeof b, "%d dBm (%s)", x.rssi, q);
    set(1, b, x.rssi >= -72 ? kText : kAmber);
  } else {
    set(1, "");
  }
  set(2, x.wifiConnected ? x.ip : "");
  std::snprintf(b, sizeof b, "%s, %u requests answered", x.ntpSynced ? "stratum 1 (GPS)" : "not synced, clients told to ignore",
                (unsigned)x.ntpRequests);
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
  set(8, x.lastError, kAmber);
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
  age(ga, sizeof ga, m.gpsEverSeen() ? nowMs - m.gps().lastGoodMs() : UINT32_MAX);
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
  }
  updateFooter(current_, m, x, nowMs);
}

}  // namespace gpsdo
