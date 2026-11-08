#!/usr/bin/env python3
"""Catch the Nano's bootloader after a manual reset, then run avrdude with the given -U operations.

usage: nano_boot.py PORT BAUD WAIT_SECONDS avrdude-args...
Sends the STK500 sync request (0x30 0x20) every 30 ms. When the bootloader answers (0x14 0x10),
which it does only in the second or so after a reset, it hands the port to avrdude straight away.
Use when the board's serial reset is disabled (a capacitor on RESET), so avrdude cannot reset it.
"""
import subprocess, sys, time
import serial

port, baud, wait = sys.argv[1], int(sys.argv[2]), float(sys.argv[3])
extra = sys.argv[4:]
s = serial.Serial(port, baud, timeout=0.03)
end = time.time() + wait
print(f"waiting up to {wait:.0f} s for a reset at {baud} baud", flush=True)
while time.time() < end:
    s.reset_input_buffer()
    s.write(b"\x30\x20")
    if b"\x14\x10" in s.read(32):
        print("bootloader answered", flush=True)
        s.close()
        r = subprocess.run(["avrdude", "-p", "atmega328p", "-c", "arduino", "-P", port, "-b", str(baud)] + extra)
        sys.exit(r.returncode)
print("no bootloader seen", flush=True)
sys.exit(2)
