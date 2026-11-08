# GPSDO Nano firmware

The Arduino Nano firmware for the Lars Walenius / PaulV GPSDO, as it runs in our unit (it reports "Rev. 3.4 paulv mods").

- `src/gpsdo_V3_4.ino` started as an exact copy of `gpsdo_V3_4.ino` from https://github.com/paulvee/Lars-GPSDO-V1 at commit 9a2d47c (2020-09-08; the file was later removed from that repo). Our changes are separate commits on top, so `git log -p nano/src` shows exactly what differs from PaulV's version.
- Licence: MIT, copyright (c) 2020 paulvee; see `LICENSE-MIT-paulvee`. Our changes are under the same MIT terms so the file stays usable upstream.

Build: `pio run -d nano` from the repository root. Programming needs the J6 jumper off (it stops the USB serial from resetting the Nano, which also stops the bootloader being reached).

## Our change: the `$GPSDO` status line

After each normal report line (left exactly as it was) the Nano prints one machine-readable line:

    $GPSDO,1,<time>,<lock counter>,<lock target>,<lock-test TIC ns>,<lock-test phase change ns>,<missed PPS>,<s since missed PPS>,<mode>,<locked>,<DAC>,<restarts>*CS

- `1` is the format version. `CS` is the NMEA-style XOR checksum of everything between `$` and `*`.
- The lock counter counts good seconds; the GPSDO reports Locked once it passes the target (5 x the time constant). It restarts from 0 whenever the lock-test TIC is outside +-100 ns or the phase change is outside +-20 ns.
- Mode is `W` (start-up warm-up), `R` (running) or `H` (hold).
- The missed-PPS count is otherwise only visible with `f2`; the CYD logs each new miss as an event.

First flashed 2026-11-08 at about 22:00 UTC after the backup in `backup/`. The TIC is sampled in the capture interrupt before any printing, so the extra ~75 ms of output each second cannot affect the measurement.
