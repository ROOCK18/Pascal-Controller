#!/usr/bin/env bash
# Magyar ékezetes LVGL betűtípusok generálása (Montserrat, OFL licenc) a src/fonts/ mappába.
#
# Előfeltétel: Node.js (npx) és a PlatformIO által már letöltött lvgl könyvtár
#   (egyszer futtass egy `pio run`-t, hogy a .pio/libdeps/tdisplay/lvgl létrejöjjön).
# Futtatás a firmware/ mappából:   bash tools/gen_fonts.sh
# Utána a platformio.ini build_flags-éhez add hozzá:   -DHU_FONTS=1
set -e
SRC=".pio/libdeps/tdisplay/lvgl/scripts/built_in_font"
OUT="src/fonts"
mkdir -p "$OUT"

# ASCII + magyar ékezetek (á é í ó ö ő ú ü ű és a nagybetűik) + °
HU="0x20-0x7F,0xB0,0xC1,0xC9,0xCD,0xD3,0xD6,0x150,0xDA,0xDC,0x170,0xE1,0xE9,0xED,0xF3,0xF6,0x151,0xFA,0xFC,0x171"
# A DejaVu-ból: Ω (0x3A9) és β (0x3B2)
GREEK="0x3A9,0x3B2"
# LV_SYMBOL_OK, CLOSE, UP, DOWN, WIFI, BLUETOOTH
SYM="61452,61453,61559,61560,61931,62099"

for SZ in 12 14 16 20 28; do
  npx --yes lv_font_conv@1.5.3 --no-compress --no-prefilter --bpp 4 --size $SZ \
    --font "$SRC/Montserrat-Medium.ttf" -r "$HU" \
    --font "$SRC/DejaVuSans.ttf" -r "$GREEK" \
    --font "$SRC/FontAwesome5-Solid+Brands+Regular.woff" -r "$SYM" \
    --format lvgl --lv-include lvgl.h --lv-font-name lv_font_hu_$SZ \
    -o "$OUT/lv_font_hu_$SZ.c" --force-fast-kern-format
  echo "kész: lv_font_hu_$SZ.c"
done
