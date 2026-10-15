#!/usr/bin/env bash
#
# Genera lo sketch per l'Arduino IDE a partire dai sorgenti in src/.
# L'IDE pretende che il file .ino abbia lo stesso nome della cartella.
#
#   ./tools/make_arduino_sketch.sh [cartella-di-destinazione]
#
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${1:-$root/arduino/EspGpsTracker}"

mkdir -p "$out"
cp "$root"/src/*.h       "$out/"
cp "$root/src/main.cpp"  "$out/$(basename "$out").ino"

echo "Sketch pronto in: $out"
echo "Apri $(basename "$out").ino con l'Arduino IDE, scheda \"ESP32 Dev Module\","
echo "Partition Scheme: \"No OTA (2MB APP/2MB SPIFFS)\"."
