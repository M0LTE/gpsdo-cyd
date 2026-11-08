#!/usr/bin/env python3
"""Development aid: relay the GPSDO's own USB report into the CYD console as "N <line>".

Lets the CYD show live data before the taps are wired. Run on the box that has both on USB.
usage: bridge.py NANO_PORT CYD_PORT
Opening the Nano port does not reset it on this unit (checked 2026-11-08); this script never writes to it.
"""
import sys, serial

def main():
    nano = serial.Serial(sys.argv[1], 9600, timeout=1)
    cyd = serial.Serial(sys.argv[2], 115200, timeout=0)
    buf = b""
    while True:
        buf += nano.read(256)
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            cyd.write(b"N " + line.rstrip(b"\r") + b"\n")
        cyd.read(65536)  # drain the CYD console echo

if __name__ == "__main__":
    main()
