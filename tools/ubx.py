#!/usr/bin/env python3
"""Minimal u-blox UBX helper for the NEO-M8N timepulse change.

usage: ubx.py PORT read                    # MON-VER and CFG-TP5 (timepulse 0), read-only
       ubx.py PORT nopulse-without-fix     # set CFG-TP5 in RAM: no pulse unless locked to GNSS
       ubx.py PORT save                    # CFG-CFG: save current config to BBR and flash
"""
import struct, sys, time
import serial

def frame(cls, mid, payload=b""):
    body = bytes([cls, mid]) + struct.pack("<H", len(payload)) + payload
    a = b = 0
    for x in body:
        a = (a + x) & 0xFF
        b = (b + a) & 0xFF
    return b"\xb5\x62" + body + bytes([a, b])

def read_ubx(s, cls, mid, timeout=3.0):
    """Wait for a UBX message of the given class/id (or an ACK/NAK for it). Returns (cls, id, payload)."""
    buf = b""
    end = time.time() + timeout
    while time.time() < end:
        buf += s.read(512)
        while True:
            i = buf.find(b"\xb5\x62")
            if i < 0 or len(buf) < i + 6:
                break
            c, m, n = buf[i + 2], buf[i + 3], struct.unpack("<H", buf[i + 4:i + 6])[0]
            if len(buf) < i + 8 + n:
                break
            pl = buf[i + 6:i + 6 + n]
            if frame(c, m, pl) != buf[i:i + 8 + n]:
                buf = buf[i + 2:]
                continue
            buf = buf[i + 8 + n:]
            if (c, m) == (cls, mid) or (c == 0x05 and pl[:2] == bytes([cls, mid])):
                return c, m, pl
    return None

def poll(s, cls, mid, payload=b""):
    s.reset_input_buffer()
    s.write(frame(cls, mid, payload))
    return read_ubx(s, cls, mid)

def show_tp5(pl):
    (idx, ver, _r, cable, rfg, per, perl, plen, plenl, udel, flags) = struct.unpack("<BBHhhIIIIiI", pl)
    print(f"CFG-TP5 tp{idx}: period {per} us (locked {perl}), pulse length {plen} us (locked {plenl}), "
          f"cable delay {cable} ns, user delay {udel} ns, flags 0x{flags:08x}")
    names = ["active", "lockGnssFreq", "lockedOtherSet", "isFreq", "isLength", "alignToTow", "polarity"]
    print("  flags:", ", ".join(n for i, n in enumerate(names) if flags >> i & 1), f"gridUtcGnss={(flags >> 7) & 15}")
    return list(struct.unpack("<BBHhhIIIIiI", pl))

def main():
    port, cmd = sys.argv[1], sys.argv[2]
    s = serial.Serial(port, 9600, timeout=0.2)
    if cmd == "read":
        nmea = s.read(400).decode("ascii", "replace")
        print("NMEA seen:", "$GN" in nmea or "$GP" in nmea)
        r = poll(s, 0x0A, 0x04)
        if r and r[1] == 0x04:
            pl = r[2]
            print("MON-VER:", pl[:30].rstrip(b"\0").decode(), "/", pl[30:40].rstrip(b"\0").decode(),
                  "/", " | ".join(pl[i:i + 30].rstrip(b"\0").decode() for i in range(40, len(pl), 30)))
        r = poll(s, 0x06, 0x31, b"\x00")
        if not r or r[0] != 0x06:
            sys.exit("no CFG-TP5 reply")
        show_tp5(r[2])
        print("raw:", r[2].hex())
    elif cmd == "nopulse-without-fix":
        r = poll(s, 0x06, 0x31, b"\x00")
        f = show_tp5(r[2])
        # When not locked to GNSS: pulse length 0, i.e. no pulse. When locked: the current settings.
        # lockedOtherSet (bit 2) makes the receiver use the *Lock values once locked.
        idx, ver, res, cable, rfg, per, perl, plen, plenl, udel, flags = f
        isLength = flags >> 4 & 1
        new = [idx, ver, res, cable, rfg, per, per, 0, plen if plen else plenl, udel, flags | 0x1 | 0x2 | 0x4]
        if not isLength:
            sys.exit("pulse length is a ratio on this unit; not handled")
        s.reset_input_buffer()
        s.write(frame(0x06, 0x31, struct.pack("<BBHhhIIIIiI", *new)))
        ack = read_ubx(s, 0x06, 0x31)
        print("ACK" if ack and ack[0] == 0x05 and ack[1] == 0x01 else f"no ACK: {ack}")
        r = poll(s, 0x06, 0x31, b"\x00")
        print("now:")
        show_tp5(r[2])
    elif cmd == "save":
        # CFG-CFG: clearMask 0, saveMask all, loadMask 0, deviceMask BBR | flash
        s.reset_input_buffer()
        s.write(frame(0x06, 0x09, struct.pack("<IIIB", 0, 0x0000FFFF, 0, 0x03)))
        ack = read_ubx(s, 0x06, 0x09)
        print("saved (ACK)" if ack and ack[0] == 0x05 and ack[1] == 0x01 else f"no ACK: {ack}")
    else:
        sys.exit(__doc__)

if __name__ == "__main__":
    main()
