#!/bin/bash
set -e
cd ~/TuyaOpen/apps/tuya.ai/blind_navigator/fonts
SRC=src
OUT=generated
export NVM_DIR=$HOME/.nvm
. $NVM_DIR/nvm.sh
mkdir -p $OUT
rm -f $OUT/*.c

SG_RANGES="-r 0x20-0x7F -r 0xA0-0xFF -r 0x2014 -r 0x2026 -r 0x2190 -r 0x2192"
JBM_RANGES="-r 0x20-0x7F -r 0xA0-0xFF -r 0x2014 -r 0x2026 -r 0x2190 -r 0x2192 -r 0x25B2 -r 0x25BC -r 0x25CF"

generate() {
    local out=$1 src=$2 size=$3 ranges=$4 name=$5
    echo "[gen] $name $size px from $src"
    lv_font_conv --bpp 4 --size $size --font "$SRC/$src" $ranges \
        --format lvgl --lv-include "lvgl.h" --no-compress \
        -o "$OUT/$out" 2>&1 | tail -2
}

generate font_iris_hero_44.c     SpaceGrotesk-Bold.ttf     44 "$SG_RANGES"  hero
generate font_iris_title_32.c    SpaceGrotesk-SemiBold.ttf 32 "$SG_RANGES"  title
generate font_iris_status_24.c   SpaceGrotesk-SemiBold.ttf 24 "$SG_RANGES"  status
generate font_iris_body_16.c     SpaceGrotesk-Regular.ttf  16 "$SG_RANGES"  body
generate font_iris_mono_sm_12.c  JetBrainsMono-Regular.ttf 12 "$JBM_RANGES" mono_sm
generate font_iris_mono_xs_10.c  JetBrainsMono-Regular.ttf 10 "$JBM_RANGES" mono_xs
generate font_iris_icons_24.c    JetBrainsMono-Regular.ttf 24 "$JBM_RANGES" icons_24

echo
echo "=== Generated files ==="
ls -la $OUT/*.c
