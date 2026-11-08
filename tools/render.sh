#!/bin/sh
# Render all screens from the real capture to docs/screens/*.png using the PC simulator.
# usage: tools/render.sh [extra sim args]   (needs PlatformIO and ImageMagick)
set -e
cd "$(dirname "$0")/.."
PIO=${PIO:-pio}
$PIO run -e sim -s
mkdir -p docs/screens
out=$(mktemp -d)
.pio/build/sim/program captures/arduino-20261108.log "$out/locked" --fake-gps
.pio/build/sim/program captures/arduino-20261108.log "$out/unlocked" --fake-gps --unlock-at 1200
.pio/build/sim/program captures/arduino-20261108.log "$out/nogps"
for f in "$out"/*.ppm; do
  convert "$f" -filter point -resize 200% "docs/screens/$(basename "${f%.ppm}").png"
done
rm -rf "$out"
ls docs/screens
