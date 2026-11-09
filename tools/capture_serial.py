#!/usr/bin/env python3
"""Capture a serial port to a file, one line per received line, prefixed with UTC receive time.

Usage: capture_serial.py PORT BAUD SECONDS OUTFILE [--append] [--send-at SECS:TEXT ...]
--append adds to OUTFILE instead of replacing it (for continuous logs that restart).
--send-at writes TEXT (with \\n appended) after SECS seconds; used once for the f1/f2/f4 help dumps.
The raw bytes are also kept in OUTFILE.raw so nothing is lost to decoding.
"""
import sys, time, datetime, termios, serial

def main():
    port, baud, secs, out = sys.argv[1], int(sys.argv[2]), float(sys.argv[3]), sys.argv[4]
    sends = []
    append = False
    args = sys.argv[5:]
    while args:
        if args[0] == "--send-at":
            t, text = args[1].split(":", 1)
            sends.append((float(t), text))
            args = args[2:]
        elif args[0] == "--append":
            append = True
            args = args[1:]
        else:
            sys.exit("unknown arg " + args[0])
    sends.sort()
    ser = serial.Serial(port, baud, timeout=0.2)
    # Keep DTR up after close so a later open does not reset the Nano again.
    attrs = termios.tcgetattr(ser.fd)
    attrs[2] &= ~termios.HUPCL
    termios.tcsetattr(ser.fd, termios.TCSANOW, attrs)
    start = time.monotonic()
    buf = b""
    with open(out, "a" if append else "w") as f, open(out + ".raw", "ab" if append else "wb") as raw:
        while time.monotonic() - start < secs:
            el = time.monotonic() - start
            while sends and el >= sends[0][0]:
                text = sends.pop(0)[1]
                ser.write((text + "\n").encode())
                f.write("# %s SENT %r\n" % (stamp(), text)); f.flush()
            data = ser.read(256)
            if not data:
                continue
            raw.write(data); raw.flush()
            buf += data
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                f.write(stamp() + " " + line.rstrip(b"\r").decode("ascii", "backslashreplace") + "\n")
            f.flush()
        if buf:
            f.write(stamp() + " [partial] " + buf.decode("ascii", "backslashreplace") + "\n")
    ser.close()

def stamp():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%fZ")[:-3]

if __name__ == "__main__":
    main()
