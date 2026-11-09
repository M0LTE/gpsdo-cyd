# Our unit: what we found inside

Notes on the particular GPSDO this display was built for, collected while building it on 8 November 2026. It is a Lars Walenius design with PaulV's modifications, but the board does not match PaulV's published schematics in several places, so measure rather than trust the drawings.

## Summary

| Part | What it is |
|---|---|
| Controller | Arduino Nano (CH340 USB chip, old bootloader at 57600 baud), front mini-USB |
| Firmware | PaulV's "Rev. 3.4 paulv mods", unmodified until 8 Nov 2026; now 3.4 plus our status line (see `nano/`) |
| GPS | u-blox NEO-M8N-0-10, firmware SPG 3.01, protocol 18.00, GPS + GLONASS in use |
| Power | 12 V DC input |
| Outputs | 10 MHz (used as the reference for GB7RDG's Flex); 1PPS on a front-panel BNC |
| Front connector "GPS / TRG" | GPS serial, not a trigger (see below) |
| Fan | none |

## Headers and connectors (measured)

PaulV's V2/V2.1 schematic shows J1 as the GPS serial header ("Ser2USB") and J4 as the Nano's serial ("Raspberry Pi Serial Monitor"), plus a J6 jumper that disables the Nano's serial reset. On our board:

- **J1** (inside): GPS serial. Top pin: GPS TXD through 100 ohm. Middle: GPS RXD through 100 ohm. Bottom: GND.
- **J4**, brought out to the front panel as **"GPS / TRG"**: also GPS serial. Left: GPS TXD through 100 ohm. Middle: GPS RXD through 100 ohm. Right: GND. Nothing on it is a trigger.
- **The Nano's TX is not brought out anywhere.** We tap it at the Nano's TX1 pin.
- **There is no J6**, yet opening the Nano's USB port does not reset it. Programming therefore means pressing the Nano's reset button while `tools/nano_boot.py` waits for the bootloader.
- All grounds (Nano, GPS, headers) are common.

## How the display is connected

- Nano TX1 through a 10k/20k divider to the CYD's IO35 (5 V logic to 3.3 V).
- GPS TXD from J1's top pin through 1k to the CYD's IO22.
- Ground from the same header. All three on the CYD's P3 connector; IO21 on P3 (the backlight) is left unconnected.
- The CYD has its own USB power. The GPSDO has no 3.3 V rail big enough to run it, and its 5 V rails feed the TIC and the oscillator.
- The front "GPS / TRG" connector stays free for a USB-serial adapter to the GPS (3.3 V, adapter RX to the left pin).
- Not yet done: the front 1PPS BNC (5 V logic) to the CYD's IO27 on CN1 through a 10k/20k divider, for a directly timed second edge. Needs a second 1.25 mm lead.

Checked after wiring: the report the CYD receives through the tap was identical to the Nano's USB report for every second compared (70 of 70), and second-to-second phase noise was unchanged (spread of diff_ns 9.8 ns before, 10.1 ns after).

## Nano settings (from `f2`/`f4`, 8 Nov 2026)

| Setting | Value |
|---|---|
| Time constant | 300 s (so lock needs 1500 good seconds, 25 minutes) |
| Gain | 655 |
| Damping | 3.00 |
| Prefilter divider | 2 (prefilter 150 s) |
| TIC offset | 500 ns |
| TIC linearisation | min 23.0, max 979, square 0.100 |
| Warm-up time | 300 s |
| Temperature sensors | type 11 (LM35 for both) |
| Temperature compensation | ref 1, coefficient 0 (off) |
| Stored DAC start value | 30196 |
| Restarts | 71 after 8 Nov 2026; about 2055 hours in service |

A verified backup of the original flash and EEPROM, with restore instructions, is in `nano/backup/`.

## GPS module

- NEO-M8N-0, "HW UBX-M8030 00080000", antenna supervisor reports OK.
- Serial: 9600 baud, NMEA 4.0 style (GN talker for RMC/GGA/GSA, no system ID field), GPS and GLONASS satellites. The output fills about 75% of 9600 baud when 20 or so satellites are in view.
- Timepulse (CFG-TP5, unchanged, see `ublox/settings-20261108.txt`): no pulse when not locked to GNSS time; when locked, 1 s period and 20% (200 ms) pulse, aligned to GPS time, rising edge on time. 25 ns cable delay.
- Antenna test on 8 Nov 2026: with the antenna unplugged the fix and the PPS stopped in the same second, and the Nano held its tuning ("No PPS") for the 4 minutes 17 seconds it was out. With the antenna back, the fix returned within seconds and the PPS 3 seconds later. So a total signal loss is handled; a weak or partial one, where the GPS keeps its time valid while the fix degrades, is what the display's GPS LOST alarm watches for.

## Behaviour worth knowing

- **Restarts:** a loose 12 V connection restarted the whole GPSDO twice on 8 Nov 2026 (both the Nano and the GPS printed their start-up text together). Each restart costs about 30 minutes: 5 minutes of warm-up, then 25 minutes of good seconds before Locked.
- **Missed PPS:** the Nano had quietly counted 6 missed pulses before the status line made them visible.
- **Oscillator:** over the 18 days stored in the Nano, the oven sat between 41.7 and 44.4 C and the DAC between about 30210 and 30302, following the temperature. In a 25-minute locked capture the phase against the GPS pulse had a spread of 13 ns and the DAC moved 66 counts.
- **Report timing:** the Nano starts its report 5.8 ms after each PPS edge (measured against NTP); the display uses that to time the second for its clock and NTP server.

## The display (CYD)

- ESP32-2432S028R with two USB sockets but an ILI9341 panel (build `env:cyd`, not `cyd2usb`); the screen is rotated 270 degrees (upside down relative to the board's printing) to suit how it is mounted.
- ESP32-D0WD-V3, 4 MB flash.
- Its light sensor reads 0 in a lit room; the dimming thresholds are still to be set from a dark-room reading.
