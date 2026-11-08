# Backup of our unit's Nano, taken 2026-11-08 before any change

- `nano-flash-backup.hex`: the whole 32 KB flash (application and the old 57600-baud bootloader), read twice with identical results. Its application part is PaulV's 3.4 unmodified: same size to the byte (22780) and identical text strings to a clean build, with code bytes differing only by compiler version.
- `nano-eeprom-backup.hex`: the whole 1 KB EEPROM (settings at 991-1023 and the 3-hour history), checked against the `f4` dump.
- `settings-20261108.log`: the `f2` and `f4` dumps taken just before.

Restore the original firmware (press the Nano's reset button when `tools/nano_boot.py` says it is waiting; this board's serial reset is disabled):

    tools/nano_boot.py PORT 57600 120 -U flash:w:nano/backup/nano-flash-backup.hex:i

Restore the EEPROM the same way with `-U eeprom:w:nano/backup/nano-eeprom-backup.hex:i`. Flashing firmware does not touch the EEPROM.
