#include "backlight.h"

#include <Arduino.h>

namespace gpsdo {
namespace Backlight {
namespace {
constexpr int kPin = 21, kLdrPin = 34, kChannel = 7;
constexpr int kBright = 255, kDim = 28;
// Hysteresis on the smoothed reading: darker than kDarkAbove dims, brighter than kLightBelow restores.
constexpr int kDarkAbove = 450, kLightBelow = 250;
float ema = 0;
bool haveEma = false, dark = false;
int current = kBright, target = kBright;
uint32_t lastReadMs = 0, lastStepMs = 0, lastLogMs = 0;
}  // namespace

void begin() {
  pinMode(kLdrPin, INPUT);
  ledcSetup(kChannel, 5000, 8);
  ledcAttachPin(kPin, kChannel);
  ledcWrite(kChannel, current);
}

void loop(uint32_t nowMs) {
  if (nowMs - lastReadMs >= 250) {
    lastReadMs = nowMs;
    int raw = analogRead(kLdrPin);
    ema = haveEma ? ema + (raw - ema) * 0.05f : raw;
    haveEma = true;
    if (!dark && ema > kDarkAbove) dark = true;
    else if (dark && ema < kLightBelow) dark = false;
    target = dark ? kDim : kBright;
  }
  if (current != target && nowMs - lastStepMs >= 15) {  // fade over about 3 s
    lastStepMs = nowMs;
    current += current < target ? 1 : -1;
    ledcWrite(kChannel, current);
  }
  if (nowMs - lastLogMs >= 60000) {
    lastLogMs = nowMs;
    Serial.printf("# light sensor %d (dims above %d, brightens below %d), backlight %d/255\n", (int)ema, kDarkAbove,
                  kLightBelow, current);
  }
}

int level() { return current; }
int lightRaw() { return (int)ema; }
}  // namespace Backlight

namespace Beeper {
namespace {
constexpr int kPin = 26, kChannel = 6;
struct Note { uint16_t hz, ms; };
const Note kBad[] = {{880, 150}, {0, 120}, {880, 150}, {0, 120}, {660, 300}};
const Note kGood[] = {{1760, 120}};
const Note* seq = nullptr;
int seqLen = 0, pos = 0;
uint32_t noteStartMs = 0;

void play(const Note* s, int n) {
  seq = s;
  seqLen = n;
  pos = -1;
  noteStartMs = 0;
}
}  // namespace

void begin() {
  ledcSetup(kChannel, 1000, 8);
  ledcAttachPin(kPin, kChannel);
  ledcWriteTone(kChannel, 0);
}

void loop(uint32_t nowMs) {
  if (!seq) return;
  if (pos >= 0 && nowMs - noteStartMs < seq[pos].ms) return;
  ++pos;
  if (pos >= seqLen) {
    ledcWriteTone(kChannel, 0);
    seq = nullptr;
    return;
  }
  noteStartMs = nowMs;
  ledcWriteTone(kChannel, seq[pos].hz);
}

void bad() { play(kBad, sizeof kBad / sizeof kBad[0]); }
void good() { play(kGood, 1); }
}  // namespace Beeper
}  // namespace gpsdo
