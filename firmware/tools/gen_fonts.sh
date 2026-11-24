#!/usr/bin/env bash
# Magyar ékezetes LVGL betűtípusok generálása az app saját betűtípusából (Open Sans Semibold, Apache 2.0 licenc)
# a src/fonts/ mappába - így a kijelző betűi megegyeznek az appéval.
#
# Előfeltétel: Node.js (npx) és a PlatformIO által már letöltött lvgl könyvtár (a FontAwesome szimbólumokhoz;
#   egyszer futtass egy `pio run`-t, hogy a .pio/libdeps/tdisplay/lvgl létrejöjjön).
# Futtatás a firmware/ mappából:   bash tools/gen_fonts.sh
# Utána a platformio.ini build_flags-éhez add hozzá:   -DHU_FONTS=1
set -e
FONTAWESOME=".pio/libdeps/tdisplay/lvgl/scripts/built_in_font/FontAwesome5-Solid+Brands+Regular.woff"
OPENSANS="../app/Controll/Resources/Fonts/OpenSans-Semibold.ttf"
OUT="src/fonts"
mkdir -p "$OUT"

# ASCII + magyar ékezetek (á é í ó ö ő ú ü ű és a nagybetűik) + °
HU="0x20-0x7F,0xB0,0x3A9,0x3B2,0xC1,0xC9,0xCD,0xD3,0xD6,0x150,0xDA,0xDC,0x170,0xE1,0xE9,0xED,0xF3,0xF6,0x151,0xFA,0xFC,0x171"
# Az Open Sans a görög Ω (0x3A9) és β (0x3B2) jelet is tartalmazza
# LV_SYMBOL_OK, CLOSE, UP, DOWN, WIFI, BLUETOOTH
SYM="61452,61453,61559,61560,61931,62099"

for SZ in 12 14 16 20 28; do
  npx --yes lv_font_conv@1.5.3 --no-compress --no-prefilter --bpp 4 --size $SZ     --font "$OPENSANS" -r "$HU"     --font "$FONTAWESOME" -r "$SYM"     --format lvgl --lv-include lvgl.h --lv-font-name lv_font_hu_$SZ     -o "$OUT/lv_font_hu_$SZ.c" --force-fast-kern-format
  echo "kész: lv_font_hu_$SZ.c"
done
