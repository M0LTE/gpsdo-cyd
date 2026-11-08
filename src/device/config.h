// Fixed hardware facts for the CYD build. Wi-Fi and MQTT are set through the setup portal.
#pragma once

// Taps from the GPSDO, both on connector P3 (see WIRING.md). Receive only: no TX pin is routed.
#define NANO_RX_PIN 35   // input-only pin, cannot drive the Nano even by mistake
#define NANO_BAUD 9600
#define GPS_RX_PIN 22
#ifndef GPS_BAUD
#define GPS_BAUD 9600    // confirm from the u-blox capture
#endif

// Optional: the GPSDO's buffered 1PPS output, through a 10k/20k divider, on CN1. Either polarity
// works: the firmware picks the edge that comes just before the Nano's report.
#define PPS_PIN 27

// Touch controller (XPT2046) on its own SPI bus.
#define TOUCH_CS 33
#define TOUCH_IRQ 36
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_CLK 25

// Wi-Fi transmit power cap; the CYD sits near the GPSDO. Raise only if the link is unreliable.
#define WIFI_TX_POWER WIFI_POWER_2dBm

#define CONSOLE_BAUD 115200
