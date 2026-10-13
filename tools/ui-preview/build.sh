#!/bin/bash
# Renders the bottom screen to PNGs on the host, without a ROM (the theme and the
# map/item art take their fallbacks; settings and debug pages draw as on the
# console). Usage: tools/ui-preview/build.sh [outdir]
#   TMC3DS_DEBUG_TOOLS=0 renders what a plain build shows (default 1).
# Needs gcc and python3. Shots and their taps: SHOTS below.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${1:-/tmp/tmc-ui-preview}
mkdir -p "$OUT/gen"
printf '#pragma once\n#define TMC3DS_DEBUG_TOOLS %s\n' "${TMC3DS_DEBUG_TOOLS:-1}" > "$OUT/gen/tmc3ds_build_config.h"
cat > "$OUT/gen/tmc3ds_version.h" <<H
#pragma once
#define TMC_PORT_VERSION "$(sh "$ROOT/platform/3ds/tools/version.sh")"
#define TMC_PORT_VERSION_LABEL TMC_PORT_VERSION
#define TMC_UPSTREAM_VERSION "$(tr -d '\r\n' < "$ROOT/platform/3ds/upstream_version.txt")"
#define TMC_PORT_IS_BETA 0
H
FLAGS="-std=gnu11 -O1 -w -DPC_PORT -DNON_MATCHING -DMULTI_REGION -DUSA -DENGLISH -DREVISION=0 -DTMC_3DS
       -include region.h -I$OUT/gen -I$ROOT -I$ROOT/include -I$ROOT/port -I$ROOT/port/ppu/include
       -I$ROOT/build/USA -I$ROOT/platform/3ds/source"
(cd "$OUT/gen" && gcc $FLAGS \
    "$ROOT/port/port_second_screen.c" "$ROOT/port/port_second_screen_theme.c" \
    "$ROOT/port/port_second_screen_render.c" "$ROOT/port/port_second_screen_quest.c" \
    "$ROOT/port/port_second_screen_dungeonmap.c" "$ROOT/port/port_second_screen_worldmap.c" \
    "$ROOT/port/port_text_i18n.c" "$ROOT/platform/3ds/source/ui_font_3ds.c" "$ROOT/tools/ui-preview/preview.c" -c)
gcc -std=gnu11 -O1 -w -I"$ROOT/platform/3ds/source" -I"$ROOT/port/ppu/include" -c "$ROOT/tools/ui-preview/stubs.c" -o "$OUT/gen/stubs.o"
gcc "$OUT"/gen/*.o -o "$OUT/preview" -lm -lpthread
# name:taps (x,y in bottom-screen pixels or tN = Nth tab, ';'-separated, replayed from the MAP tab)
SHOTS=(
  "map:"
  "items:t1"
  "quest:t2"
  "quest_kinstones:t2;35,50"
  "quest_techniques:t2;155,50"
  "options:t3"
  "options_frames:t3;200,40;200,40"
  "updates:t3;240,110"
)
[ "${TMC3DS_DEBUG_TOOLS:-1}" = 1 ] && SHOTS+=("debug:t4" "debug_recording:t4;300,107" "debug_switches:t4;280,63;100,64;100,120")
rm -f "$OUT"/*.ppm "$OUT"/*.png
"$OUT/preview" "$OUT" "${SHOTS[@]}" > /dev/null
python3 "$ROOT/tools/ui-preview/ppm2png.py" "$OUT"/*.ppm
