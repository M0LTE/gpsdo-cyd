# Wiring the CYD to the GPSDO

**Draft.** Two things are still to confirm before you solder, and both are marked below: the u-blox module's TX pin, which needs the module model, and the physical pin order of P3 on your CYD.

The CYD only listens. It takes two signals and a ground from inside the GPSDO, and nothing goes back the other way.

## What connects where

| From (inside the GPSDO) | Through | To (CYD connector P3) |
|---|---|---|
| Nano D1 (TX), 5 V logic | 10 k in series, then 20 k to ground (gives 3.3 V) | IO35 |
| u-blox TX, 3.3 V logic | 1 k in series | IO22 |
| GPSDO ground, near the taps | direct | GND |
| (nothing) | | IO21: leave unconnected, it drives the screen backlight |

Everything goes into **P3**, one 4-pin 1.25 mm Molex PicoBlade plug (the CYD comes with a matching lead). Don't use **P1**: it's the USB serial port. Don't use **CN1** either; it duplicates IO22 and isn't needed.

IO35 is an input-only pin, so even a firmware mistake can't drive anything back into the Nano.

```
 GPSDO                                              CYD, connector P3
 -----                                              -----------------
 Nano D1 (TX) ---[ 10k ]---+-----------------------> IO35
                           |
                        [ 20k ]
                           |
 GND ----------------------+-----------------------> GND

 u-blox TX -------[ 1k ]---------------------------> IO22

                                                     IO21  (leave unconnected)
```

The divider: 5 V x 20 / (10 + 20) = 3.33 V at IO35. Any 1/4 W or 0805 resistors are fine. The divider loads the Nano's TX with 30 k, which is negligible next to the Nano's own USB chip on the same line.

## Step 1: find Nano D1 (TX)

On an Arduino Nano, D1 is the pin labelled **TX1** (sometimes "D1" or "TX"). It's on the long edge with the digital pins, at the end next to **RX0**, just past the reset button. It's the same pin that sends the report you see on the front USB.

Check it before soldering. Measure from the pin to ground:

- **Meter:** about 5 V at rest, with a small dip or flicker once a second.
- **Scope:** idles high at 5 V, with a burst of 9600-baud data every second, about 70 to 100 ms long.

Solder the 10 k resistor at the Nano end, so only a short thin wire carries the signal away.

## Step 2: find the u-blox TX

**Still to confirm. Which module is it?** Tell me what's printed on the module or its board (for example NEO-6M, NEO-M8N, NEO-M8T) and I'll fill in the exact pin. In the meantime:

- On a breakout board, the header pin labelled **TX** or **TXD** is the module's serial output.
- On a bare NEO-6/NEO-M8 module, TXD is pin 20 and RXD is pin 21. Check this against your module's datasheet before relying on it.

Check it the same way. It should sit at about **3.3 V** at rest, with bursts of data once a second. If there are two candidates, it's TX that shows bursts with nothing connected to the other side. Only the PPS pin pulses once a second **without** data bursts; it's a short pulse, about 100 ms. Don't tap PPS.

Fit the 1 k resistor at the module end.

## Step 3: ground

Use a ground point close to the taps, such as the Nano's GND pin or the GPS board's GND.

## Step 4: the cable and the CYD end

- Use a twisted bundle of the three wires or a thin screened cable, and keep it away from the TIC area (C1, the HC4046 and the A0 input).
- Bring it out through a grommet in the case.
- **P3 pin order: still to confirm on your board.** The CYD documentation lists P3's pins as GND, IO35, IO22, IO21 but doesn't say which end is which. Before crimping or soldering, check with a meter, with the CYD powered on USB and the screen lit:
  - GND pin: 0 ohms to the USB socket's metal shell.
  - IO21 pin: about 3.3 V (it's the backlight drive).
  - The other two are IO35 and IO22. You don't need to tell them apart in advance: both are inputs behind series resistors, so swapping them does no harm. If the console (step 6) shows the Nano's report as `G>` lines, or garbage on both, swap those two wires.

## Step 5: power

Power the CYD from its own USB supply, **not** from the GPSDO's 5 V. Use an ordinary mains USB charger: they are isolated, so joining grounds through the tap doesn't create a ground loop. Avoid powering the CYD from a PC that also connects to the GPSDO.

## Step 6: check it works

1. Plug the CYD into a computer and open its console (`pio device monitor`, 115200 baud). You should see lines starting `N>` (the Nano report, once a second) and `G>` (NMEA sentences). The footer on the screen shows both as `0s`.
2. Compare the GPSDO's own USB report before and after connecting. The TIC `ns` and `diff_ns` values should look the same: no new offset and no extra noise.
3. If the Nano lines show garbage, the divider or ground is wrong. If the `G>` lines are garbage, the GPS baud rate isn't 9600; tell me and I'll set it from the u-blox capture.
