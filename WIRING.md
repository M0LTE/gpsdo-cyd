# Wiring the CYD to the GPSDO

The CYD only listens. It takes two signals and a ground from headers that are already on the GPSDO controller board (PaulV's V2.1 layout), so nothing needs soldering on the GPSDO itself.

- **J4, "Raspberry Pi Serial Monitor":** the Nano's TX (through R10, 100 ohm), its RX, and ground.
- **J1, "Ser2USB":** a header meant for a USB-serial adapter on the GPS module (a NEO-M8N here). Its pins are labelled from the adapter's point of view, so the GPS **output** is the pin marked **RXD** (through R3, 100 ohm).

Leave J4's RX pin and J1's TXD pin unconnected: those lead into the Nano and the GPS.

## You need

- 1 x 10 k, 1 x 20 k (or two 10 k in series, or 22 k) and 1 x 1 k resistor
- 3 female jumper sockets for the header pins
- the 4-wire 1.25 mm lead that came with the CYD, for its P3 connector
- heatshrink and a multimeter

## 1. Identify the GPSDO pins (power off, meter on resistance)

Don't trust the labels alone; the resistance readings settle it.

**J4 (Nano report):**
- TX pin: about **100 ohm** to the Nano pin marked **TX1**.
- GND pin: **0 ohm** to the Nano's **GND** pin.
- The third pin is the Nano's RX. Leave it alone.

**J1 (GPS output):**
- The signal pin with about **100 ohm** to the GPS board's **TX/TXD** pin. It should be the one marked RXD.
- The other signal pin goes to the GPS's input. Leave it alone.

## 2. Identify the CYD's P3 lead wires

P3 carries GND, IO35, IO22 and IO21. With the lead plugged in:
- **GND:** 0 ohm to the metal shell of a USB socket on the CYD.
- **IO21:** with the CYD powered over USB, about 3.3 V to GND (it drives the backlight). Don't connect it; tape it off.
- The other two are **IO35** and **IO22**. Which is which doesn't matter yet: both are inputs behind resistors, so swapping them does no harm. Step 4 shows if they're crossed.

Don't use the CYD's P1 (it's the USB serial port) or CN1 (not needed).

## 3. Make up the lead

```
 GPSDO                                         CYD, P3 lead
 -----                                         ------------
 J4 TX  ---[ 10k ]---+------------------------> IO35
                     |
                  [ 20k ]
                     |
 J4 GND -------------+------------------------> GND

 J1 RXD ---[ 1k ]-----------------------------> IO22

                                                IO21: not connected, taped off
```

- The Nano runs at 5 V; the divider brings it to 3.3 V (5 x 20 / 30). The GPS is already 3.3 V; the 1 k just protects both ends.
- Fit the resistors at the GPSDO end, on the jumper sockets, under heatshrink.
- One ground wire is enough; J1's GND pin stays unused.
- Twist the three wires together, keep them away from the TIC area (C1, the HC4046 and the A0 input), and bring them out through a grommet.

## 4. Power on and check

1. Before powering up, check nothing touches J4's RX pin or J1's TXD pin.
2. Power the CYD from its own USB supply, not the GPSDO's 5 V. An ordinary mains USB charger is isolated, so the shared ground doesn't make a loop.
3. Open the CYD console (`pio device monitor`, 115200 baud). Lines from the taps start `N>` (the Nano, once a second) and `G>` (NMEA). The screen footer shows both as `0s`.
4. If the Nano's report appears as `G>` lines and NMEA as `N>`, swap the two signal wires at the CYD end.
5. Compare the GPSDO's own USB report with a capture from before wiring: the TIC `ns` and `diff_ns` should look the same, with no new offset or noise.

After any power-off the GPSDO shows unlocked for at least 5 x its time constant (25 minutes at tc 300) while the loop settles. That's expected.

## For a u-blox capture with a USB-serial adapter

J1 is also the place for a temporary 3.3 V USB-serial adapter: adapter **RX** to J1 **RXD**, adapter GND to GND, and leave the adapter's TX unconnected so it only listens. The NEO-M8N's default is 9600 baud.
