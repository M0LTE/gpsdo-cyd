#!/usr/bin/env python3
"""Replay captures into the CYD over its USB console, paced in real time (or faster).

usage: replay.py PORT ARDUINO_CAPTURE [--nmea NMEA_CAPTURE] [--speed X]

Each captured line is sent as "N <line>" (GPSDO report) or "G <line>" (NMEA); the firmware feeds
them in as if they came from the taps. Capture lines are "<ISO UTC time> <line>".
"""
import argparse, datetime, sys, time
import serial

def load(path, tag):
    out = []
    with open(path, encoding="ascii", errors="replace") as f:
        for l in f:
            l = l.rstrip("\n")
            if not l or l.startswith("#") or " " not in l:
                continue
            ts, body = l.split(" ", 1)
            try:
                t = datetime.datetime.fromisoformat(ts.rstrip("Z")).timestamp()
            except ValueError:
                continue  # continuation of a line split by a bare \r in the capture
            out.append((t, tag, body))
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("capture")
    ap.add_argument("--nmea")
    ap.add_argument("--speed", type=float, default=1.0)
    a = ap.parse_args()
    ev = load(a.capture, "N") + (load(a.nmea, "G") if a.nmea else [])
    ev.sort(key=lambda e: e[0])
    ser = serial.Serial(a.port, 115200, timeout=0)
    start, t0 = time.monotonic(), ev[0][0]
    for t, tag, body in ev:
        wait = (t - t0) / a.speed - (time.monotonic() - start)
        if wait > 0:
            time.sleep(wait)
        ser.write(f"{tag} {body}\n".encode("ascii", "replace"))
        ser.read(4096)  # discard the console echo
    print(f"replayed {len(ev)} lines", file=sys.stderr)

if __name__ == "__main__":
    main()
