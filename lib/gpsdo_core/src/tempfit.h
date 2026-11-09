// How the GPSDO's tuning follows the oven temperature, from its own 18-day history, and the
// tempRef / tempCoeff that would cancel it. A suggestion only: nothing is sent to the Nano.
#pragma once
#include <cstdint>

#include "history.h"

namespace gpsdo {

struct TempFit {
  bool valid = false;
  int points = 0;
  double slopeDacPerC = 0;   // DAC counts per degree C
  double r2 = 0;             // how much of the DAC movement temperature explains (0..1)
  double meanTempC = 0;
  double tempSpanC = 0;
  long suggestedTempRef = 0;   // LM35 raw ADC value at the mean oven temperature
  long suggestedTempCoeff = 0; // in the Nano's units: 0..10000, 10000+ means negative
  double ppbPerC = 0;          // the same slope as a frequency change, from the gain setting
};

// gain: the Nano's gain setting (DAC counts per ppb x 1000 / 65536 ... see .cpp); typically 655.
TempFit fitTemperature(const NanoHistory& h, int gain);

}  // namespace gpsdo
