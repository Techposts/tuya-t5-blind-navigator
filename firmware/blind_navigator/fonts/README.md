# IRIS LVGL Bitmap Fonts

This directory contains the bitmap fonts used by the IRIS firmware UI. They are pre-generated and committed so the firmware can be built without the Node toolchain.

## Layout

```
fonts/
├── README.md                       (this file)
├── gen_fonts.sh                    (regenerator)
├── src/                            (TTF sources)
│   ├── SpaceGrotesk-VF.ttf         (variable, sliced into the 3 below)
│   ├── SpaceGrotesk-Bold.ttf
│   ├── SpaceGrotesk-SemiBold.ttf
│   ├── SpaceGrotesk-Regular.ttf
│   └── JetBrainsMono-Regular.ttf
└── generated/                      (LVGL bitmap fonts -- C source)
    ├── font_iris_hero_44.c         (Space Grotesk Bold      44pt)
    ├── font_iris_title_32.c        (Space Grotesk SemiBold  32pt)
    ├── font_iris_status_24.c       (Space Grotesk SemiBold  24pt)
    ├── font_iris_body_16.c         (Space Grotesk Regular   16pt)
    ├── font_iris_mono_sm_12.c      (JetBrains Mono Regular  12pt)
    ├── font_iris_mono_xs_10.c      (JetBrains Mono Regular  10pt)
    └── font_iris_icons_24.c        (JetBrains Mono Regular  24pt -- arrows + dot)
```

## Glyph coverage

All 4bpp antialiased. Glyph ranges:

- **All fonts**: ASCII (0x20-0x7F) + Latin-1 supplement (0xA0-0xFF, gives `·` `°` `±`) + em dash (0x2014), ellipsis (0x2026), arrows (0x2190, 0x2192).
- **JetBrains Mono fonts only**: geometric shapes — `▲` (0x25B2), `▼` (0x25BC), `●` (0x25CF). Space Grotesk does not have these glyphs.

## Regenerating from sources

If you ever need to regenerate (different size, additional glyph range, swapped typeface):

```bash
# Install lv_font_conv (one-time)
npm install -g lv_font_conv

# Regenerate all 7 fonts from the TTFs in fonts/src/
./gen_fonts.sh
```

The script outputs to `fonts/generated/` (overwriting). The wiring is via `include/iris_fonts.h` which declares all 7 `lv_font_t` symbols + UTF-8 byte-literals for the special glyphs.

## Why these typefaces

- **Space Grotesk** (display geometric sans) — high x-height, hard verticals, rounded counter-spaces. Carries display-size copy ("Tap to navigate", action verbs) with presence on a 320x480 LCD.
- **JetBrains Mono** (programmer monospace) — used for tag labels, technical metadata, sub-labels. The mono/sans split is a classic editorial trick that reads as "instrument" rather than "phone app."

## License

Both typefaces are SIL Open Font License (OFL). See:
- Space Grotesk: https://github.com/floriankarsten/space-grotesk
- JetBrains Mono: https://github.com/JetBrains/JetBrainsMono

## Build size cost

| Font | Source `.c` size | Compiled binary contribution |
|---|---|---|
| `font_iris_hero_44.c` | 441 KB | ~50 KB |
| `font_iris_title_32.c` | 240 KB | ~32 KB |
| `font_iris_status_24.c` | 158 KB | ~20 KB |
| `font_iris_body_16.c` | 91 KB | ~12 KB |
| `font_iris_mono_sm_12.c` | 59 KB | ~8 KB |
| `font_iris_mono_xs_10.c` | 52 KB | ~7 KB |
| `font_iris_icons_24.c` | 144 KB | ~18 KB |
| **Total** | **~1.2 MB** | **~150 KB** |

The 4× difference between source and binary is expected — the C source is `.dat` byte arrays in printable hex; compiled binary is the raw bytes. T5AI has 8 MB flash so the 150 KB cost is negligible.
