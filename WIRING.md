# Wiring the CYD to the GPSDO

The CYD only listens. It takes two signals and a ground from the GPSDO.

**Header numbering differs between boards.** PaulV's published V2.1 schematic shows J1 as the GPS serial header ("Ser2USB") and J4 as the Nano's serial ("Raspberry Pi Serial Monitor"). On our board both J1 and J4 carry the GPS serial, J4 is brought out to the case as "GPS / TRG", and the Nano's TX isn't broken out at all. So identify the pins by resistance, as below, rather than by label.

What we use on our board:
- **GPS output:** J4's left pin (100 ohm to the GPS TXD pin), via the case connector.
- **Nano report:** the Nano's own **TX1** pin, tapped directly.
- **Ground:** J4's right pin (all grounds are common).

Leave the GPS RXD pin (J4 middle, J1 middle) unconnected: it leads into the GPS.

## You need

- 1 x 10 k, 1 x 20 k (or two 10 k in series, or 22 k) and 1 x 1 k resistor
- female jumper sockets for the J4 pins, and thin wire for the Nano tap
- the 4-wire 1.25 mm lead that came with the CYD, for its P3 connector
- heatshrink and a multimeter

## 1. Identify the GPSDO pins (power off, meter on resistance)

Don't trust the labels alone; the resistance readings settle it.

**GPS output:** the header pin with about **100 ohm** to the GPS board's **TX/TXD** pin. On our board that's J4 left (and J1 top). The pin with 100 ohm to the GPS **RXD** is its input; leave it alone. The pin with 0 ohm to GPS GND is ground.

**Nano TX1:** pins 1 and 30 are at the end of the Nano furthest from its USB socket. TX1 is the corner pin of the row that carries D2 to D12; along that row from the far end the pins run TX1, RX0, RST, GND, D2... (the corner pin of the other row is VIN). Confirm by checking that the GND pin two places along reads 0 ohm to the GPS GND. Solder a thin wire to TX1 with the 10 k at that end.

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
 Nano TX1 -[ 10k ]---+------------------------> IO35
                     |
                  [ 20k ]
                     |
 GND (J4 right) -----+------------------------> GND

 GPS TXD (J4 left) -[ 1k ]--------------------> IO22

                                                IO21: not connected, taped off
```

- The Nano runs at 5 V; the divider brings it to 3.3 V (5 x 20 / 30). The GPS is already 3.3 V; the 1 k just protects both ends.
- Fit the resistors at the GPSDO end, under heatshrink: the 10 k at the Nano's TX1, the 1 k at the J4 socket.
- One ground wire is enough.
- Twist the three wires together, keep them away from the TIC area (C1, the HC4046 and the A0 input), and bring them out through a grommet.

## 4. Power on and check

1. Before powering up, check nothing touches the GPS RXD pin.
2. Power the CYD from its own USB supply, not from the GPSDO. An ordinary mains USB charger is isolated, so the shared ground doesn't make a loop. The GPSDO has no 3.3 V rail able to run the CYD, and its 5 V rails feed the TIC and OCXO, where the Wi-Fi's current bursts would show up as noise.
3. Open the CYD console (`pio device monitor`, 115200 baud). Lines from the taps start `N>` (the Nano, once a second) and `G>` (NMEA). The screen footer shows both as `0s`.
4. If the Nano's report appears as `G>` lines and NMEA as `N>`, swap the two signal wires at the CYD end.
5. Compare the GPSDO's own USB report with a capture from before wiring: the TIC `ns` and `diff_ns` should look the same, with no new offset or noise.

After any power-off the GPSDO shows unlocked for at least 5 x its time constant (25 minutes at tc 300) while the loop settles. That's expected.

## For a u-blox capture with a USB-serial adapter

J1 (or J4) is also the place for a temporary 3.3 V USB-serial adapter: adapter **RX** to the GPS TXD pin, adapter GND to GND, and leave the adapter's TX unconnected so it only listens. The NEO-M8N's default is 9600 baud.
