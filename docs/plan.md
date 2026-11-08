# GPSDO status display: build plan for Claude Code

## Goal

Build a passive status display for my Lars/PaulV GPSDO using an **ESP32-2432S028R "Cheap Yellow Display" (CYD)**, 2.8" 320×240 resistive touch.

The CYD **listens** to two serial lines inside the GPSDO:

1. the Arduino Nano's per-second report (its TX, D1);
2. the u-blox GPS module's NMEA output (its TX).

It shows lock state, Arduino loop data, satellite numbers and a sky plot.

It must **never transmit** to either device and must not affect GPSDO timing.

## Access you have

You have USB serial access to:
- **The GPSDO Arduino.** It's on its front mini-USB at 9600 baud. Opening the port resets the Nano (DTR), so it will have to re-lock afterwards. Keep sessions few and short.
- **The u-blox module.** It's on a temporary USB-serial adapter clipped to its TX (and possibly RX).
- **The CYD,** for flashing and its console.

Before any serial session that could change device state, tell me what you'll send. Never send configuration to the u-blox that saves to its flash/BBR (no CFG-CFG save) without my explicit OK.

## Background (verified from source)

The firmware is PaulV's fork of Lars Walenius' code, `gpsdo_V3_71.ino` in https://github.com/paulvee/Lars-GPSDO-V1. My unit's exact build is unknown, so check the startup banner (e.g. "Rev. 3.7.1 paulv mods").

**Serial port**
- 9600 baud (line 1290).
- Prints a header row, then one tab-separated line per PPS.
- Header labels include: `time`, `ns`, `dac`, `OCXO`, `status`, `diff_ns filtX10`, `tc`, `filt`, `timer1`, `ambient`.
- The `status` field prints `Locked` or `****` (unlocked) (lines 1052-1053, 1157-1163).

**Other strings to handle**
- `" No PPS"` (about once a second while PPS is missing)
- `"Missing 10MHz?"`
- `"Restarted+hold"`
- The help/info text from `f1`/`f2`/`f4`

**Lock logic** (lines 266-295)
- `lockPPScounter` increments each PPS.
- It resets if the filtered TIC differs from `TIC_Offset` by more than `lockPPSlimit` (default 100 ns), or if the filtered frequency error is more than 20 ppb.
- The unit counts as locked when `lockPPScounter > timeConst * lockPPSfactor`. The defaults are tc = 32 and factor 5, so it needs more than 160 s of good seconds.
- After about 200 s without PPS the counter resets (line 1508).

**DAC near limits**
- In run mode the firmware flashes the lock LED off when `dacValueOut < 3000 || > 62535` (lines 1481-1485).
- Treat that as a warning condition: the EFC is near the end of its range.

**GPS input**
- The Arduino does **not** parse NMEA; it only uses the PPS. All satellite data must come from the u-blox UART.

**Rule:** don't assume column order from this summary. Derive it from the real header row you capture, and parse by header name where possible.

## Phase 1: discovery (do this first, report back before building)

**1. Capture the Arduino output**
- Capture 10+ minutes of the Arduino report from USB. Include the startup banner and header.
- If I agree, also capture the `f1`, `f2` and `f4` output once. Sending those means writing to the Arduino, so ask first.
- Save the captures to `captures/`.

**2. Capture the u-blox output**
- Auto-detect the baud rate: try 9600 first, then 38400 and 115200.
- Capture 10+ minutes and save to `captures/`.
- Report:
  - which sentences arrive (GGA/GSA/GSV/RMC/ZDA/TXT, etc.) and from which constellations (GP/GL/GA/GB/GN talkers);
  - whether any UBX binary messages arrive;
  - the module model and firmware, if `$GxTXT` shows them.
- If GSV (with elevation/azimuth) or GSA is missing, stop and propose the minimum change. Prefer enabling messages in RAM only, and ask me before doing it.

**3. Identify the CYD**
- Work out the display controller variant. The 2432S028R comes with ILI9341 or ST7789, and with one or two USB ports.
- Use https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display (`PINS.md`, `DisplayConfig/`) to confirm the TFT_eSPI or LVGL config.

**4. Report back to me**
- The confirmed formats.
- Anything that contradicts this plan.

## Phase 2: wiring instructions (deliver as `WIRING.md`, before I solder anything)

Write clear, step-by-step wiring instructions I can follow with the case open. Include:

**Signals to tap**
- **Arduino Nano D1 (TX).**
  - Show where D1 is on a Nano.
  - Explain how to confirm it with a scope or meter: idle high at 5 V, with activity once a second.
- **u-blox TX.**
  - Explain how to identify it from the module markings and datasheet pinout.
  - Explain how to confirm it at 3.3 V idle with bursts once a second.
  - I'll tell you the module model if I can see it. Ask me.
- **GND.** Use a ground point close to the taps.

**Connections to the CYD**

| Source | Via | CYD pin | Connector |
|---|---|---|---|
| Nano D1 (5 V TTL) | Divider: 10 kΩ series, 20 kΩ to GND (≈3.3 V) | IO35 (input-only, no pull-up) | P3 |
| u-blox TX (3.3 V) | 1 kΩ series | IO22 | P3 or CN1 |
| GPSDO GND | n/a | GND | P3/CN1 |

- **Do not use IO21.** It's the TFT backlight, even though it's on P3.
- **Do not use P1.** It's UART0, shared with the USB bridge.
- **Leave both CYD TX directions unconnected.**

**Physical notes**
- P3 and CN1 are 1.25 mm 4-pin Molex PicoBlade. Give the pin order with the board orientation.
- Run the taps as a twisted pair or thin screened cable, kept away from the TIC area (C1, HC4046, A0).
- Bring the cable out through a grommet if the CYD is mounted outside.
- Power the CYD from its own USB supply, **not** the GPSDO 5 V.

**Diagrams**
- Include an ASCII or SVG wiring diagram, plus a divider schematic.

**Verification steps after wiring**
1. The CYD console shows raw lines from both UARTs.
2. Compare the GPSDO's own USB report before and after: TIC `ns` and `diff_ns` should be unaffected.

**Unconfirmed pin positions**
If you can't confirm a pin position from documentation, say so and tell me how to check it. Don't guess.

## Phase 3: firmware

**Stack**
- PlatformIO, Arduino framework for ESP32.
- TFT_eSPI or LVGL. Pick one and justify it briefly; LVGL is preferred if it fits comfortably.
- Touch via XPT2046.
- Wi-Fi **off** by default. Its transmit bursts could add noise near the GPSDO.

**Serial**
- UART1 RX = IO35 (Arduino, 9600).
- UART2 RX = IO22 (u-blox, baud from Phase 1).
- TX pins unassigned or set to an unused pin.
- Use non-blocking line assemblers with ring buffers, and never block the UI.

**Parsers** (pure C++ with no Arduino dependencies, so they're unit-testable)
- **Arduino report parser:**
  - map the header row → column indices;
  - parse each per-second line into a struct;
  - flag `No PPS`, `Missing 10MHz?` and `Restarted+hold`;
  - track the time since the last valid line.
- **NMEA parser:**
  - verify checksums;
  - GGA: fix quality, sats used, HDOP, altitude;
  - GSA: fix mode 2D/3D, PRNs used, PDOP/HDOP/VDOP;
  - GSV: per satellite PRN, constellation, elevation, azimuth, SNR;
  - RMC/ZDA: UTC date and time.
  - Handle multi-talker output (GP/GL/GA/GB/GN) and multi-part GSV correctly.
  - Expire satellites not seen for about 10 s.

**Derived lock information**
- Locked/unlocked from `status`.
- Time locked: seconds since the last unlock→lock transition.
- Count of lock losses since boot, and when the last one happened.
- "Approaching lock" estimate: consecutive seconds within limits, if derivable. Otherwise show TIC deviation from the offset.
- DAC warning: below 3000 or above 62535.
- PPS-missing and Arduino-silent alarms (no line for more than 3 s).
- GPS fix lost, or sats used < 4.

**Screens** (tap to cycle; landscape 320×240)

1. **Status (default)**
   - A large LOCKED / UNLOCKED / NO PPS / NO DATA banner, colour coded.
   - Time locked.
   - Lock-loss count.
   - UTC time.
   - Fix type and sats used / in view.
   - HDOP.
   - DAC value, with a small bar showing position in its 0-65535 range and the warning zones marked.
   - TIC ns, `diff_ns`, tc.
   - OCXO and ambient temperatures.
2. **Sky plot**
   - Polar plot with the horizon as the outer ring, rings at 30° and 60°, and N/E/S/W marked.
   - Each satellite as a dot at azimuth/elevation, coloured by SNR and labelled with its PRN.
   - Filled dot = used in fix, hollow = in view only.
   - Constellation shown by marker shape or a prefix.
3. **Signal bars**
   - Per-satellite SNR bars sorted by SNR.
   - Used-in-fix highlighted.
   - PRN labels.
4. **Trends**
   - Last ~30 min of TIC ns, `diff_ns` and DAC, as sparklines or a small chart.
   - Lock state shown as a background band.

**Other behaviour**
- On-screen indicators show how old each data source's most recent line is.
- Optional, behind a compile-time flag and **off by default**:
  - Wi-Fi + MQTT publishing lock state, sats used, DAC, TIC and alarms for Home Assistant (I run HA);
  - a GPIO alarm output.
  - Design for this, but don't enable it.

## Testing

- **Native unit tests** for both parsers (PlatformIO `native` env), using the real captures from Phase 1 as fixtures. Include edge cases:
  - partial lines and checksum failures;
  - multi-part GSV and empty SNR fields;
  - header re-print after an Arduino reset;
  - `No PPS` interleaving.
- **A replay mode** that feeds the captures into the UI. This can be on-device over the console, or a desktop simulator if cheap. It lets screens be checked without live hardware.
- **On-device checks** after I wire it:
  1. both sources show as live;
  2. values match the GPSDO USB report;
  3. the GPSDO's TIC behaviour is unchanged with the CYD running.

## Deliverables

- `WIRING.md` (Phase 2), delivered before firmware work proceeds.
- The PlatformIO project with a README: build, flash, which screen is which, and what each alarm means.
- `captures/` with the sample data used for tests.
- A short summary of anything in my unit that differed from this plan.

## Constraints recap

- **Listen-only:** never transmit to the Arduino or u-blox from the CYD.
- **Ask before:**
  - sending commands to the Arduino;
  - changing the u-blox configuration;
  - anything that would interrupt the GPSDO for long.
- **Don't guess pinouts or formats.** Confirm them from captures or documentation, or ask me.
