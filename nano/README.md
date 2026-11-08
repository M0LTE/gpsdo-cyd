# GPSDO Nano firmware

The Arduino Nano firmware for the Lars Walenius / PaulV GPSDO, as it runs in our unit (it reports "Rev. 3.4 paulv mods").

- `src/gpsdo_V3_4.ino` started as an exact copy of `gpsdo_V3_4.ino` from https://github.com/paulvee/Lars-GPSDO-V1 at commit 9a2d47c (2020-09-08; the file was later removed from that repo). Our changes are separate commits on top, so `git log -p nano/src` shows exactly what differs from PaulV's version.
- Licence: MIT, copyright (c) 2020 paulvee; see `LICENSE-MIT-paulvee`. Our changes are under the same MIT terms so the file stays usable upstream.

Build: `pio run -e nano` from the repository root. Programming needs the J6 jumper off (it stops the USB serial from resetting the Nano, which also stops the bootloader being reached).
