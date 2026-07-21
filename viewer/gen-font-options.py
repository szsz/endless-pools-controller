#!/usr/bin/env python3
"""
Generate the inline font data for viewer/font-options.html.

Parses the Adafruit_GFX_Library headers directly, and additionally builds the
hand-drawn custom fonts `Org3x5` (CUSTOM_ART), `Seg3x5` (SEG3_ART) and
`Seg5x7` (SEG5_ART) from the ASCII art below (see the "custom font" sections)
-- those are also written out as fonts/Org3x5.h, fonts/Seg3x5.h, fonts/Seg5x7.h.

IMPORTANT: the headers carry per-line C comments such as `/* 0x20 space */`
and `// 0x30 '0'` *inside* the arrays. A naive hex regex happily eats those
comment values as font data; offsets still line up so the result looks
plausible but every glyph renders as noise. So: strip comments first.

TomThumb also has `#if (TOMTHUMB_USE_EXTENDED)` blocks appended to both the
bitmap and the glyph arrays -- those must be dropped so only 0x20..0x7E remain.

Usage:  python3 viewer/gen-font-options.py            # rewrites the <script id="fontdata"> block
        python3 viewer/gen-font-options.py --verify   # ASCII-art check only
"""

import json
import os
import re
import sys

GFX_DIR = os.path.expanduser("~/Arduino/libraries/Adafruit_GFX_Library")
FONT_DIR = os.path.join(GFX_DIR, "Fonts")
HERE = os.path.dirname(os.path.abspath(__file__))
OUT_HTML = os.path.join(HERE, "font-options.html")
# standalone `<script src>`-able copy of Panel3x5, for the emulator page
OUT_PANEL_JS = os.path.join(HERE, "hub75-panel-font.js")

# Every GFX font whose glyphs fit in ~8px. The Free* families are 9pt+ and are
# far too tall for a 64px list, so they are deliberately excluded.
SMALL_FONTS = ["TomThumb", "Picopixel", "Org_01", "Tiny3x3a2pt7b"]

FONTS_OUT_DIR = os.path.join(os.path.dirname(HERE), "fonts")


# ============================================================== custom font
#
# Org3x5 -- hand-drawn, not an Adafruit font.
#
# Brief: "like Org_01 but 2px narrower". Org_01's digits are a 5x5 stencil at
# xAdvance 6, which only fits 12 chars in 64px so `125m 1:30 warmup` overflows.
# Org3x5 keeps Org_01's bold, closed, high-contrast look but draws the digits
# in a 3x5 cell at xAdvance 4 -- the same pitch as TomThumb/Picopixel, so the
# line holds just as much text, and yAdvance 6 keeps TomThumb's 10 list rows.
#
# The design rule for the digits: distinguish them by OUTLINE, never by a
# single interior pixel. That is TomThumb's failure mode (5/7/8/9 are near
# solid 3-wide blocks) and, to a lesser degree, Org_01's too (its 0 vs 8 and
# 5 vs 6 differ by exactly one row). Concretely:
#   0  corners notched off -> a round blob; 8 keeps flat full-width bars
#   1  flag + full-width foot serif (Org_01's `1` is a bare 1px column)
#   2  bottom-left diagonal, top bar stops short of the right edge
#   3  left column empty in the middle -> hugs the right
#   4  open top, right column runs to the floor
#   5  zig-zag: arm top-left, arm bottom-right
#   6  slanted flag entering top-left, closed bowl below
#   7  full top bar then a lone centre stem, no foot
#   8  the only glyph with three full-width bars AND both side walls
#   9  exact 180-degree rotation of 6 -> slanted tail leaving bottom-left
#
# Metrics: caps + digits occupy baseline-5..baseline-1, x-height letters
# baseline-4..baseline-1, descenders reach baseline+0. With yAdvance 6 the
# next row's tallest glyph starts at baseline+1, so descenders never collide.
#
# Format below: `@<hex code> <xAdvance> <yOffset>` then the glyph rows,
# '#' = lit. A header with no rows is a blank glyph (space).

CUSTOM_ART = r"""
@20 2 0
@21 2 -5
#
#
#
.
#
@22 4 -5
#.#
#.#
@23 4 -5
#.#
###
#.#
###
#.#
@24 4 -5
.##
##.
.#.
.##
##.
@25 4 -5
#.#
..#
.#.
#..
#.#
@26 4 -5
.#.
#.#
.#.
#.#
.##
@27 2 -5
#
#
@28 3 -5
.#
#.
#.
#.
.#
@29 3 -5
#.
.#
.#
.#
#.
@2A 4 -5
#.#
.#.
#.#
@2B 4 -4
.#.
###
.#.
@2C 2 -1
#
#
@2D 4 -3
###
@2E 2 -1
#
@2F 4 -5
..#
..#
.#.
#..
#..
@30 4 -5
.#.
#.#
#.#
#.#
.#.
@31 4 -5
.#.
##.
.#.
.#.
###
@32 4 -5
##.
..#
.#.
#..
###
@33 4 -5
###
..#
.##
..#
###
@34 4 -5
#.#
#.#
###
..#
..#
@35 4 -5
###
#..
###
..#
###
@36 4 -5
.#.
#..
###
#.#
###
@37 4 -5
###
..#
.#.
.#.
.#.
@38 4 -5
###
#.#
###
#.#
###
@39 4 -5
###
#.#
###
..#
.#.
@3A 2 -4
#
.
#
@3B 2 -4
#
.
#
#
@3C 3 -4
.#
#.
.#
@3D 4 -4
###
...
###
@3E 3 -4
#.
.#
#.
@3F 4 -5
##.
..#
.#.
...
.#.
@40 4 -5
###
#.#
###
#..
.##
@41 4 -5
.#.
#.#
###
#.#
#.#
@42 4 -5
##.
#.#
##.
#.#
##.
@43 4 -5
.##
#..
#..
#..
.##
@44 4 -5
##.
#.#
#.#
#.#
##.
@45 4 -5
###
#..
##.
#..
###
@46 4 -5
###
#..
##.
#..
#..
@47 4 -5
.##
#..
#.#
#.#
.##
@48 4 -5
#.#
#.#
###
#.#
#.#
@49 4 -5
###
.#.
.#.
.#.
###
@4A 4 -5
..#
..#
..#
#.#
.#.
@4B 4 -5
#.#
#.#
##.
#.#
#.#
@4C 4 -5
#..
#..
#..
#..
###
@4D 4 -5
#.#
###
###
#.#
#.#
@4E 4 -5
#.#
##.
###
.##
#.#
@4F 4 -5
###
#.#
#.#
#.#
###
@50 4 -5
##.
#.#
##.
#..
#..
@51 4 -5
###
#.#
#.#
###
..#
@52 4 -5
##.
#.#
##.
#.#
#.#
@53 4 -5
.##
#..
.#.
..#
##.
@54 4 -5
###
.#.
.#.
.#.
.#.
@55 4 -5
#.#
#.#
#.#
#.#
###
@56 4 -5
#.#
#.#
#.#
#.#
.#.
@57 4 -5
#.#
#.#
###
###
#.#
@58 4 -5
#.#
#.#
.#.
#.#
#.#
@59 4 -5
#.#
#.#
.#.
.#.
.#.
@5A 4 -5
###
..#
.#.
#..
###
@5B 3 -5
##
#.
#.
#.
##
@5C 4 -5
#..
#..
.#.
..#
..#
@5D 3 -5
##
.#
.#
.#
##
@5E 4 -5
.#.
#.#
@5F 4 0
###
@60 3 -5
#.
.#
@61 4 -4
##.
.##
#.#
###
@62 4 -5
#..
#..
##.
#.#
##.
@63 4 -4
.##
#..
#..
.##
@64 4 -5
..#
..#
.##
#.#
.##
@65 4 -4
.#.
###
#..
.##
@66 4 -5
.##
#..
##.
#..
#..
@67 4 -4
.##
#.#
.##
..#
##.
@68 4 -5
#..
#..
##.
#.#
#.#
@69 4 -5
.#.
...
##.
.#.
###
@6A 4 -5
.#.
...
.#.
.#.
.#.
##.
@6B 4 -5
#..
#.#
##.
#.#
#.#
@6C 4 -5
##.
.#.
.#.
.#.
###
@6D 4 -4
###
###
#.#
#.#
@6E 4 -4
##.
#.#
#.#
#.#
@6F 4 -4
###
#.#
#.#
###
@70 4 -4
##.
#.#
##.
#..
#..
@71 4 -4
.##
#.#
.##
..#
..#
@72 4 -4
.##
#..
#..
#..
@73 4 -4
.##
#..
..#
##.
@74 4 -5
#..
###
#..
#..
.##
@75 4 -4
#.#
#.#
#.#
###
@76 4 -4
#.#
#.#
#.#
.#.
@77 4 -4
#.#
#.#
###
###
@78 4 -4
#.#
.#.
.#.
#.#
@79 4 -4
#.#
#.#
.##
..#
##.
@7A 4 -4
###
..#
#..
###
@7B 3 -5
.#
.#
#.
.#
.#
@7C 2 -5
#
#
#
#
#
@7D 3 -5
#.
#.
.#
#.
#.
@7E 4 -4
.##
##.
"""

DEFAULT_NOTE = [
    "// %s -- custom 3x5 pixel font (xAdvance 4), hand-drawn for the",
    "// 64x64 HUB75 swim list. Org_01's bold closed shapes at TomThumb's",
    "// 4px digit pitch; digits are separated by outline, not by a single",
    "// interior pixel. Generated by viewer/gen-font-options.py -- edit the",
    "// CUSTOM_ART block there, not this file.",
]

CUSTOM_NAME = "Org3x5"
CUSTOM_YADVANCE = 6


# ============================================================== custom font 2
#
# Seg3x5 / Seg5x7 -- hand-drawn seven-segment ("calculator LED") fonts.
#
# Brief: make the *numbers* read as a 7-segment display -- top / middle /
# bottom bars plus four corner verticals. Every digit is composed from the
# canonical segment set (a top, b upper-right, c lower-right, d bottom,
# e lower-left, f upper-left, g middle), so the shapes are the ones the eye
# already knows from every clock radio and lap timer.
#
# The verticals deliberately *overlap* the bar rows (f covers rows 0..2, e
# rows 2..4 of the 3x5 cell). Without that overlap a segment-off glyph such
# as lowercase `n` would be left with 1px stubs floating above the baseline.
#
# Seg3x5 is the primary variant: a 3x5 cell at xAdvance 4 and yAdvance 6 --
# exactly TomThumb/Org3x5 pitch, so the swim line still fits 64px and the
# list still holds 10 rows. Seg5x7 is the same design at 5x7 / xAdvance 6,
# which is a far more convincing 7-segment look but, like Org_01, is 6px per
# digit, so `125m 1:30 warmup` no longer fits in 64px (and yAdvance 8 drops
# the list to 8 rows). It is included for comparison, not as a recommendation.
#
# Letters use the standard 7-segment/calculator alphabet where one exists
# (A b C d E F G H I J L o P q r S t U y ...). K M N V W X Z have no segment
# form; those are drawn by hand but kept to horizontal/vertical strokes inside
# the same cell so they do not break the look.
#
# Seg3x5 is CAPS-ONLY: 0x61..0x7A are drawn with the *capital* shapes. The
# 7-segment lowercase alphabet mixes full-height (b d) and half-height (n o r
# u) forms, which at a 3x5 cell reads as noise rather than as text; capitals
# are uniform. The lowercase codepoints still carry real glyphs, so note text
# renders normally instead of going blank. Seg5x7 keeps the true lowercase
# forms -- at 5x7 there is enough room for them to read, and it also lets the
# page show both conventions side by side. Because Seg3x5 has no lowercase,
# it also has no descenders; Seg5x7 drops `g` and `y` one row below the
# baseline so they cannot be confused with `9`.
#
# Both fonts are generated from the same segment table -- see
# scripts note in the git history; the packed art is inlined below so this
# file stays the single source of truth.

SEG3_ART = r"""
@20 2 0
@21 2 -5
#
#
#
.
#
@22 4 -5
#.#
#.#
@23 4 -5
#.#
###
#.#
###
#.#
@24 4 -5
.##
##.
.#.
.##
##.
@25 4 -5
#.#
..#
.#.
#..
#.#
@26 4 -5
.#.
#.#
.#.
#.#
.##
@27 2 -5
#
#
@28 3 -5
.#
#.
#.
#.
.#
@29 3 -5
#.
.#
.#
.#
#.
@2A 4 -5
#.#
.#.
#.#
@2B 4 -4
.#.
###
.#.
@2C 2 -1
#
#
@2D 4 -3
###
@2E 2 -1
#
@2F 4 -5
..#
..#
.#.
#..
#..
@30 4 -5
###
#.#
#.#
#.#
###
@31 4 -5
..#
..#
..#
..#
..#
@32 4 -5
###
..#
###
#..
###
@33 4 -5
###
..#
###
..#
###
@34 4 -5
#.#
#.#
###
..#
..#
@35 4 -5
###
#..
###
..#
###
@36 4 -5
###
#..
###
#.#
###
@37 4 -5
###
..#
..#
..#
..#
@38 4 -5
###
#.#
###
#.#
###
@39 4 -5
###
#.#
###
..#
###
@3A 2 -4
#
.
#
@3B 2 -4
#
.
#
#
@3C 3 -4
.#
#.
.#
@3D 4 -4
###
...
###
@3E 3 -4
#.
.#
#.
@3F 4 -5
##.
..#
.#.
...
.#.
@40 4 -5
###
#.#
###
#..
.##
@41 4 -5
###
#.#
###
#.#
#.#
@42 4 -5
#..
#..
###
#.#
###
@43 4 -5
###
#..
#..
#..
###
@44 4 -5
..#
..#
###
#.#
###
@45 4 -5
###
#..
###
#..
###
@46 4 -5
###
#..
###
#..
#..
@47 4 -5
###
#..
#.#
#.#
###
@48 4 -5
#.#
#.#
###
#.#
#.#
@49 4 -5
#..
#..
#..
#..
#..
@4A 4 -5
..#
..#
#.#
#.#
###
@4B 4 -5
#.#
#.#
###
#..
#..
@4C 4 -5
#..
#..
#..
#..
###
@4D 4 -5
###
#.#
#.#
#.#
#.#
@4E 4 -3
###
#.#
#.#
@4F 4 -5
###
#.#
#.#
#.#
###
@50 4 -5
###
#.#
###
#..
#..
@51 4 -5
###
#.#
###
..#
..#
@52 4 -3
###
#..
#..
@53 4 -5
###
#..
###
..#
###
@54 4 -5
#..
#..
###
#..
###
@55 4 -5
#.#
#.#
#.#
#.#
###
@56 4 -5
#.#
#.#
#.#
#.#
.#.
@57 4 -5
#.#
#.#
#.#
###
###
@58 4 -5
#.#
#.#
.#.
#.#
#.#
@59 4 -5
#.#
#.#
###
..#
###
@5A 4 -5
###
..#
###
#..
###
@5B 3 -5
##
#.
#.
#.
##
@5C 4 -5
#..
#..
.#.
..#
..#
@5D 3 -5
##
.#
.#
.#
##
@5E 4 -5
.#.
#.#
@5F 4 0
###
@60 3 -5
#.
.#
@61 4 -5
###
#.#
###
#.#
#.#
@62 4 -5
#..
#..
###
#.#
###
@63 4 -5
###
#..
#..
#..
###
@64 4 -5
..#
..#
###
#.#
###
@65 4 -5
###
#..
###
#..
###
@66 4 -5
###
#..
###
#..
#..
@67 4 -5
###
#..
#.#
#.#
###
@68 4 -5
#.#
#.#
###
#.#
#.#
@69 4 -5
#..
#..
#..
#..
#..
@6A 4 -5
..#
..#
#.#
#.#
###
@6B 4 -5
#.#
#.#
###
#..
#..
@6C 4 -5
#..
#..
#..
#..
###
@6D 4 -5
###
#.#
#.#
#.#
#.#
@6E 4 -3
###
#.#
#.#
@6F 4 -5
###
#.#
#.#
#.#
###
@70 4 -5
###
#.#
###
#..
#..
@71 4 -5
###
#.#
###
..#
..#
@72 4 -3
###
#..
#..
@73 4 -5
###
#..
###
..#
###
@74 4 -5
#..
#..
###
#..
###
@75 4 -5
#.#
#.#
#.#
#.#
###
@76 4 -5
#.#
#.#
#.#
#.#
.#.
@77 4 -5
#.#
#.#
#.#
###
###
@78 4 -5
#.#
#.#
.#.
#.#
#.#
@79 4 -5
#.#
#.#
###
..#
###
@7A 4 -5
###
..#
###
#..
###
@7B 3 -5
.#
.#
#.
.#
.#
@7C 2 -5
#
#
#
#
#
@7D 3 -5
#.
#.
.#
#.
#.
@7E 4 -4
.##
##.
"""

SEG3_NAME = "Seg3x5"
SEG3_YADVANCE = 6
SEG3_NOTE = [
    "// Seg3x5 -- custom seven-segment pixel font (3x5 cell, xAdvance 4),",
    "// hand-drawn for the 64x64 HUB75 swim list. Digits are built from the",
    "// canonical 7-segment set so they read like a clock/lap-timer display,",
    "// at TomThumb's 4px digit pitch and yAdvance 6 (10 list rows).",
    "// Caps-only: a-z (0x61..0x7A) are drawn with the capital shapes, so the",
    "// font has no descenders and note text still renders.",
    "// Generated by viewer/gen-font-options.py -- edit the SEG3_ART block",
    "// there, not this file.",
]

SEG5_ART = r"""
@20 3 0
@21 3 -7
#
#
#
#
#
.
#
@22 4 -7
#.#
#.#
@23 6 -6
.#.#.
#####
.#.#.
#####
.#.#.
@24 6 -7
..#..
.####
#.#..
.###.
..#.#
####.
..#..
@25 6 -7
##..#
##..#
...#.
..#..
.#...
#..##
#..##
@26 6 -7
.##..
#..#.
#..#.
.##..
#..#.
#...#
.###.
@27 3 -7
#
#
@28 3 -7
.#
#.
#.
#.
#.
#.
.#
@29 3 -7
#.
.#
.#
.#
.#
.#
#.
@2A 6 -6
..#..
#.#.#
.###.
#.#.#
..#..
@2B 6 -5
..#..
..#..
#####
..#..
..#..
@2C 3 -1
.#
#.
@2D 6 -4
#####
@2E 3 -1
#
@2F 6 -7
....#
....#
...#.
..#..
.#...
#....
#....
@30 6 -7
#####
#...#
#...#
#...#
#...#
#...#
#####
@31 6 -7
....#
....#
....#
....#
....#
....#
....#
@32 6 -7
#####
....#
....#
#####
#....
#....
#####
@33 6 -7
#####
....#
....#
#####
....#
....#
#####
@34 6 -7
#...#
#...#
#...#
#####
....#
....#
....#
@35 6 -7
#####
#....
#....
#####
....#
....#
#####
@36 6 -7
#####
#....
#....
#####
#...#
#...#
#####
@37 6 -7
#####
....#
....#
....#
....#
....#
....#
@38 6 -7
#####
#...#
#...#
#####
#...#
#...#
#####
@39 6 -7
#####
#...#
#...#
#####
....#
....#
#####
@3A 3 -6
#
.
.
.
#
@3B 3 -6
#
.
.
.
#
#
@3C 4 -6
..#
.#.
#..
.#.
..#
@3D 6 -5
#####
.....
#####
@3E 4 -6
#..
.#.
..#
.#.
#..
@3F 6 -7
.###.
#...#
....#
..##.
..#..
.....
..#..
@40 6 -7
.###.
#...#
#.##.
#.#.#
#.##.
#....
.###.
@41 6 -7
#####
#...#
#...#
#####
#...#
#...#
#...#
@42 6 -7
#....
#....
#....
#####
#...#
#...#
#####
@43 6 -7
#####
#....
#....
#....
#....
#....
#####
@44 6 -7
....#
....#
....#
#####
#...#
#...#
#####
@45 6 -7
#####
#....
#....
#####
#....
#....
#####
@46 6 -7
#####
#....
#....
#####
#....
#....
#....
@47 6 -7
#####
#....
#....
#...#
#...#
#...#
#####
@48 6 -7
#...#
#...#
#...#
#####
#...#
#...#
#...#
@49 6 -7
#....
#....
#....
#....
#....
#....
#....
@4A 6 -7
....#
....#
....#
#...#
#...#
#...#
#####
@4B 6 -7
#...#
#...#
#...#
#####
#....
#....
#....
@4C 6 -7
#....
#....
#....
#....
#....
#....
#####
@4D 6 -7
#####
#...#
#...#
#...#
#...#
#...#
#...#
@4E 6 -4
#####
#...#
#...#
#...#
@4F 6 -7
#####
#...#
#...#
#...#
#...#
#...#
#####
@50 6 -7
#####
#...#
#...#
#####
#....
#....
#....
@51 6 -7
#####
#...#
#...#
#####
....#
....#
....#
@52 6 -4
#####
#....
#....
#....
@53 6 -7
#####
#....
#....
#####
....#
....#
#####
@54 6 -7
#....
#....
#....
#####
#....
#....
#####
@55 6 -7
#...#
#...#
#...#
#...#
#...#
#...#
#####
@56 6 -7
#...#
#...#
#...#
#...#
#...#
.#.#.
..#..
@57 6 -7
#...#
#...#
#...#
#####
#...#
#...#
#####
@58 6 -7
#...#
#...#
.#.#.
..#..
.#.#.
#...#
#...#
@59 6 -7
#...#
#...#
#...#
#####
....#
....#
#####
@5A 6 -7
#####
....#
....#
#####
#....
#....
#####
@5B 3 -7
##
#.
#.
#.
#.
#.
##
@5C 6 -7
#....
#....
.#...
..#..
...#.
....#
....#
@5D 3 -7
##
.#
.#
.#
.#
.#
##
@5E 6 -7
..#..
.#.#.
#...#
@5F 6 0
#####
@60 3 -7
#.
.#
@61 6 -7
#####
#...#
#...#
#####
#...#
#...#
#...#
@62 6 -7
#....
#....
#....
#####
#...#
#...#
#####
@63 6 -4
#####
#....
#....
#####
@64 6 -7
....#
....#
....#
#####
#...#
#...#
#####
@65 6 -7
#####
#....
#....
#####
#....
#....
#####
@66 6 -7
#####
#....
#....
#####
#....
#....
#....
@67 6 -6
#####
#...#
#...#
#####
....#
....#
#####
@68 6 -7
#....
#....
#....
#####
#...#
#...#
#...#
@69 3 -7
#
.
#
#
#
#
#
@6A 6 -7
....#
.....
....#
....#
....#
....#
#...#
.###.
@6B 6 -7
#...#
#...#
#...#
#####
#....
#....
#....
@6C 6 -7
#....
#....
#....
#....
#....
#....
#####
@6D 6 -7
#####
#...#
#...#
#...#
#...#
#...#
#...#
@6E 6 -4
#####
#...#
#...#
#...#
@6F 6 -4
#####
#...#
#...#
#####
@70 6 -7
#####
#...#
#...#
#####
#....
#....
#....
@71 6 -7
#####
#...#
#...#
#####
....#
....#
....#
@72 6 -4
#####
#....
#....
#....
@73 6 -7
#####
#....
#....
#####
....#
....#
#####
@74 6 -7
#....
#....
#....
#####
#....
#....
#####
@75 6 -4
#...#
#...#
#...#
#####
@76 6 -4
#...#
#...#
.#.#.
..#..
@77 6 -4
#...#
#...#
#####
#####
@78 6 -4
#...#
.#.#.
.#.#.
#...#
@79 6 -6
#...#
#...#
#...#
#####
....#
....#
#####
@7A 6 -5
#####
...#.
..#..
.#...
#####
@7B 4 -7
.##
.#.
.#.
#..
.#.
.#.
.##
@7C 3 -7
#
#
#
#
#
#
#
@7D 4 -7
##.
.#.
.#.
..#
.#.
.#.
##.
@7E 6 -5
.##.#
#..#.
"""

SEG5_NAME = "Seg5x7"
SEG5_YADVANCE = 8
SEG5_NOTE = [
    "// Seg5x7 -- custom seven-segment pixel font (5x7 cell, xAdvance 6),",
    "// the large variant of Seg3x5. Much more convincing as an LED display,",
    "// but 6px per digit means a 16-char swim line no longer fits 64px and",
    "// yAdvance 8 leaves only 8 list rows.",
    "// Generated by viewer/gen-font-options.py -- edit the SEG5_ART block",
    "// there, not this file.",
]


# ============================================================== custom font 3
#
# Panel3x5 -- the hybrid actually intended for the swim list.
#
# It is not drawn by hand: it is *composed* from the two fonts above, so a fix
# to either source art propagates here automatically (see merge_art()).
#
#   digits 0-9 (0x30..0x39)  <- Seg3x5   (7-segment shapes: the digit forms the
#                                         eye already knows from clock radios)
#   everything else          <- Org3x5   (bold, silhouette-differentiated
#                                         letters, punctuation and symbols,
#                                         including real lowercase forms)
#
# Rationale: Seg3x5's digits win on familiarity but its alphabet is caps-only
# and reads as noise at 3x5; Org3x5's alphabet is the better one but its digits
# are an invented outline scheme nobody has seen before. Taking the best half
# of each keeps the shared 3x5 cell / xAdvance 4 / yAdvance 6 metrics, so the
# 64px line still holds 16 chars and the list still holds 10 rows.
#
# Lowercase is deliberately kept real (not folded to capitals): the caller
# decides whether to upper-case at render time (there is a user setting), so
# the font has to be able to draw both.

PANEL3_NAME = "Panel3x5"
PANEL3_YADVANCE = 6
PANEL3_DIGITS = frozenset(range(0x30, 0x3A))     # '0'..'9' come from Seg3x5
PANEL3_NOTE = [
    "// Panel3x5 -- hybrid 3x5 pixel font (xAdvance 4, yAdvance 6) for the",
    "// 64x64 HUB75 swim list. Digits 0-9 are Seg3x5's seven-segment shapes;",
    "// letters A-Z / a-z, punctuation and every other symbol are Org3x5's.",
    "// Real lowercase forms are kept -- capitalisation is applied by the",
    "// caller at render time, not baked into the font.",
    "// Generated by viewer/gen-font-options.py -- it is composed from the",
    "// CUSTOM_ART (Org3x5) and SEG3_ART (Seg3x5) blocks there, so fix a glyph",
    "// in one of those and it propagates. Do not edit this file.",
]


def parse_custom_art(art: str):
    """-> {code: (rows, xAdvance, yOffset)} for the whole 0x20..0x7E range."""
    out, cur = {}, None
    for line in art.strip("\n").split("\n"):
        if line.startswith("@"):
            code, xadv, yoff = line[1:].split()
            cur = (int(code, 16), int(xadv), int(yoff))
            out[cur[0]] = ([], cur[1], cur[2])
        elif line.strip():
            out[cur[0]][0].append(line.rstrip())
    return out


def merge_art(base_art: str, overlay_art: str, codes) -> str:
    """Compose one art block out of two: `overlay_art`'s cells for the code
    points in `codes`, `base_art`'s for all the rest.

    This re-serialises the *parsed* art rather than copying pixels by hand, so
    editing a glyph in either source block is picked up here on the next run.
    """
    base, over = parse_custom_art(base_art), parse_custom_art(overlay_art)
    lines = []
    for code in range(0x20, 0x7F):
        src = over if code in codes else base
        if code not in src:
            raise ValueError("merge_art: source is missing 0x%02X" % code)
        rows, xadv, yoff = src[code]
        lines.append("@%02X %d %d" % (code, xadv, yoff))
        lines.extend(rows)
    return "\n".join(lines) + "\n"


def build_custom_font(name: str, art: str, yadv: int, note=None):
    """Pack the ASCII art into a GFXfont dict (same shape as parse_gfx_font)."""
    cells = parse_custom_art(art)
    missing = [c for c in range(0x20, 0x7F) if c not in cells]
    if missing:
        raise ValueError("custom font %s missing 0x%02X.." % (name, missing[0]))

    bitmaps, glyphs = [], []
    for code in range(0x20, 0x7F):
        rows, xadv, yoff = cells[code]
        gh = len(rows)
        gw = max((len(r) for r in rows), default=0)
        if any(len(r) != gw for r in rows):
            raise ValueError("ragged glyph 0x%02X in %s" % (code, name))
        off = len(bitmaps)
        acc, nbits = 0, 0
        for r in rows:
            for ch in r:
                acc = (acc << 1) | (1 if ch == "#" else 0)
                nbits += 1
                if nbits == 8:
                    bitmaps.append(acc)
                    acc, nbits = 0, 0
        if nbits:
            bitmaps.append((acc << (8 - nbits)) & 0xFF)
        if gh == 0:
            off = len(bitmaps)          # blank glyph consumes no bytes
        glyphs.append([off, gw, gh, xadv, 0, yoff if gh else 0])

    return {"name": name, "first": 0x20, "last": 0x7E, "yAdvance": yadv,
            "bitmaps": bitmaps, "glyphs": glyphs, "custom": True,
            "note": note or DEFAULT_NOTE}


def emit_c_header(font) -> str:
    """Adafruit Fonts/*.h layout, so the firmware can `#include` it as-is."""
    name = font["name"]
    L = [line % name if "%s" in line else line
         for line in font.get("note", DEFAULT_NOTE)]
    L += ["#pragma once",
         "",
         "const uint8_t %sBitmaps[] PROGMEM = {" % name]

    # one commented line of bytes per glyph
    for i, g in enumerate(font["glyphs"]):
        off, gw, gh = g[0], g[1], g[2]
        n = (gw * gh + 7) // 8
        code = 0x20 + i
        label = "space" if code == 0x20 else "'%s'" % chr(code)
        if n == 0:
            L.append("  /* 0x%02X %-5s */" % (code, label))
            continue
        body = ", ".join("0x%02X" % b for b in font["bitmaps"][off:off + n])
        L.append("  %s,%s/* 0x%02X %s */" % (body, " " * max(1, 40 - len(body)),
                                             code, label))
    L.append("};")
    L.append("")
    L.append("const GFXglyph %sGlyphs[] PROGMEM = {" % name)
    for i, g in enumerate(font["glyphs"]):
        code = 0x20 + i
        label = "space" if code == 0x20 else "'%s'" % chr(code)
        L.append("  {%5d, %2d, %2d, %2d, %2d, %2d},   // 0x%02X %s"
                 % (g[0], g[1], g[2], g[3], g[4], g[5], code, label))
    L.append("};")
    L.append("")
    L.append("const GFXfont %s PROGMEM = {" % name)
    L.append("  (uint8_t  *)%sBitmaps," % name)
    L.append("  (GFXglyph *)%sGlyphs," % name)
    L.append("  0x20, 0x7E, %d};" % font["yAdvance"])
    L.append("")
    L.append("// Approx. %d bytes" % (len(font["bitmaps"]) +
                                      len(font["glyphs"]) * 7 + 7))
    L.append("")
    return "\n".join(L)


def strip_comments(src: str) -> str:
    """Remove /* ... */ and //... . Newlines inside block comments are kept."""
    out = []
    i, n = 0, len(src)
    while i < n:
        if src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * src.count("\n", i, j))
            i = j
        elif src.startswith("//", i):
            j = src.find("\n", i)
            i = n if j < 0 else j
        else:
            out.append(src[i])
            i += 1
    return "".join(out)


def strip_preproc_blocks(src: str) -> str:
    """Drop `#if ... #endif` regions (TomThumb's extended-glyph sections)."""
    out, depth = [], 0
    for line in src.split("\n"):
        s = line.strip()
        if s.startswith("#if"):
            depth += 1
            continue
        if s.startswith("#endif"):
            depth = max(0, depth - 1)
            continue
        if depth == 0:
            out.append(line)
    return "\n".join(out)


def array_body(src: str, decl: str) -> str:
    """Text between the `{` that follows `decl` and its matching `}`."""
    start = src.index(decl)
    start = src.index("{", start)
    depth, i = 0, start
    while i < len(src):
        if src[i] == "{":
            depth += 1
        elif src[i] == "}":
            depth -= 1
            if depth == 0:
                return src[start + 1:i]
        i += 1
    raise ValueError("unbalanced braces for " + decl)


def parse_gfx_font(name: str):
    with open(os.path.join(FONT_DIR, name + ".h")) as f:
        raw = f.read()
    src = strip_preproc_blocks(strip_comments(raw))

    bitmaps = [int(x, 0) for x in
               re.findall(r"0[xX][0-9a-fA-F]+", array_body(src, name + "Bitmaps"))]

    glyphs = []
    for m in re.finditer(r"\{([^{}]*)\}", array_body(src, name + "Glyphs")):
        nums = [int(v, 0) for v in re.findall(r"-?\d+", m.group(1))]
        if len(nums) == 6:
            glyphs.append(nums)

    foot = re.search(
        r"const\s+GFXfont\s+" + name + r"\s+PROGMEM\s*=\s*\{(.*?)\};", src, re.S)
    parts = [p.strip() for p in foot.group(1).split(",")]
    first, last, yadv = int(parts[2], 0), int(parts[3], 0), int(parts[4], 0)

    glyphs = glyphs[:last - first + 1]
    assert len(glyphs) == last - first + 1, (name, len(glyphs))
    return {"name": name, "first": first, "last": last,
            "yAdvance": yadv, "bitmaps": bitmaps, "glyphs": glyphs}


def parse_glcd():
    with open(os.path.join(GFX_DIR, "glcdfont.c")) as f:
        src = strip_comments(f.read())
    body = array_body(src, "font[]")
    data = [int(x, 0) for x in re.findall(r"0[xX][0-9a-fA-F]+", body)]
    assert len(data) == 256 * 5, len(data)
    return data


# ---------------------------------------------------------------- verification
def glyph_rows_gfx(font, ch):
    """Mirror of drawCharGFX() in the page: returns list of row strings."""
    c = ord(ch)
    if not (font["first"] <= c <= font["last"]):
        return []
    off, gw, gh, xadv, xoff, yoff = font["glyphs"][c - font["first"]]
    rows, bo, bits, bit = [], off, 0, 0
    for _ in range(gh):
        row = ""
        for _ in range(gw):
            if (bit & 7) == 0:
                bits = font["bitmaps"][bo]
                bo += 1
            bit += 1
            row += "#" if bits & 0x80 else "."
            bits = (bits << 1) & 0xFF
        rows.append(row)
    return rows


def glyph_rows_glcd(data, ch):
    """Mirror of drawCharBuiltin(): 5 column bytes, bit j = row j."""
    c = ord(ch) & 0xFF
    cols = data[c * 5:c * 5 + 5]
    return ["".join("#" if col & (1 << j) else "." for col in cols)
            for j in range(8)]


def text_rows_gfx(font, text, top, bottom):
    """Render `text` into ASCII rows spanning y=top..bottom relative to baseline.

    Mirrors drawText()/drawCharGFX() in the page: pen starts at x=0 on the
    baseline, each glyph is blitted at (pen + xOffset, baseline + yOffset).
    Returns (rows, width_in_px).
    """
    grid, pen = {}, 0
    for ch in text:
        c = ord(ch)
        if not (font["first"] <= c <= font["last"]):
            continue
        off, gw, gh, xadv, xoff, yoff = font["glyphs"][c - font["first"]]
        for j, row in enumerate(glyph_rows_gfx(font, ch)):
            for i, p in enumerate(row):
                if p == "#":
                    grid[(pen + xoff + i, yoff + j)] = True
        pen += xadv
    rows = ["".join("#" if (x, y) in grid else "." for x in range(pen))
            for y in range(top, bottom + 1)]
    return rows, pen


def wrap_to_width(font, text, maxw):
    """Split `text` into runs that each fit `maxw` px -- so an alphabet strip
    shows every letter instead of being cut off at the panel edge."""
    out, cur, w = [], "", 0
    for ch in text:
        a = font["glyphs"][ord(ch) - font["first"]][3]
        if cur and w + a > maxw:
            out.append(cur)
            cur, w = "", 0
        cur += ch
        w += a
    if cur:
        out.append(cur)
    return out


def verify_custom(font):
    """Deep check of the hand-drawn font: every glyph, then the real strips."""
    top = -(font["yAdvance"] - 1)      # tallest glyph starts here (Org3x5: -5)
    adv = font["glyphs"][ord("0") - font["first"]][3]
    print("=" * 66)
    print("CUSTOM %s  yAdvance=%d  bitmapBytes=%d  glyphs=%d"
          % (font["name"], font["yAdvance"], len(font["bitmaps"]),
             len(font["glyphs"])))

    print("\n-- digits, side by side --")
    rows, w = text_rows_gfx(font, "0123456789", top, 0)
    for r in rows:
        print("   " + r)
    print("   " + "".join(str(i // adv) if i % adv == 0 else " "
                          for i in range(w)))

    print("\n-- key glyphs --")
    for ch in "0123456789:mrestA":
        g = font["glyphs"][ord(ch) - font["first"]]
        print("  '%s'  off=%d w=%d h=%d xAdv=%d xOff=%d yOff=%d"
              % (ch, g[0], g[1], g[2], g[3], g[4], g[5]))
        for r in glyph_rows_gfx(font, ch):
            print("      " + r)

    print("\n-- sample strips (panel is 64px wide) --")
    ok = True
    for s in ("125m 1:30 warmup", "rest 0:20 easy", "0123456789"):
        rows, w = text_rows_gfx(font, s, top, 0)
        flag = "fits" if w <= 64 else "OVERFLOWS"
        print('  "%s"  -> %dpx of 64  [%s]' % (s, w, flag))
        for r in rows:
            print("      |" + r.ljust(64) + "|")
        if w > 64 and not font.get("oversized"):
            ok = False

    print("\n-- alphabet, every letter (wrapped at 64px, never truncated) --")
    for label, alpha in (("A-Z", "".join(chr(c) for c in range(0x41, 0x5B))),
                         ("a-z", "".join(chr(c) for c in range(0x61, 0x7B)))):
        _, full_w = text_rows_gfx(font, alpha, top, 0)
        print('  %s  -> %dpx of 64  [%s]'
              % (label, full_w, "fits" if full_w <= 64 else
                 "wraps onto %d strips" % ((full_w + 63) // 64)))
        for chunk in wrap_to_width(font, alpha, 64):
            rows, w = text_rows_gfx(font, chunk, top, 0)
            for r in rows:
                print("      |" + r.ljust(64) + "|")
            print("      +" + "-" * 64 + "+  \"%s\" %dpx" % (chunk, w))

    # caps-only fonts must draw 0x61..0x7A with the 0x41..0x5A shapes
    caps = all(glyph_rows_gfx(font, chr(c)) ==
               glyph_rows_gfx(font, chr(c - 0x20)) for c in range(0x61, 0x7B))
    print("\n-- a-z drawn as capitals? %s --" % ("YES (caps-only font)" if caps
                                                 else "no (has lowercase forms)"))
    if caps:
        for c in range(0x61, 0x7B):
            lo = font["glyphs"][c - font["first"]]
            up = font["glyphs"][c - 0x20 - font["first"]]
            if lo[1:] != up[1:]:
                print("  !! '%s' metrics differ from '%s'"
                      % (chr(c), chr(c - 0x20)))
                ok = False

    print("\n-- two stacked rows at yAdvance %d (descender clearance) --"
          % font["yAdvance"])
    a, _ = text_rows_gfx(font, "gjpqy 90", top, 0)
    b, _ = text_rows_gfx(font, "125m Ab", top, 0)
    for r in a:
        print("      " + r)
    for r in b:
        print("      " + r)

    # every printable code point must decode and stay inside the bitmap array
    for i, g in enumerate(font["glyphs"]):
        need = g[0] + (g[1] * g[2] + 7) // 8
        if need > len(font["bitmaps"]):
            print("  !! 0x%02X bitmap overrun" % (0x20 + i))
            ok = False
    print("\n-- full charset --")
    for start in range(0x20, 0x7F, 16):
        chunk = "".join(chr(c) for c in range(start, min(start + 16, 0x7F)))
        rows, _ = text_rows_gfx(font, chunk, top, 0)
        for r in rows:
            print("   " + r)
        label = ""
        for ch in chunk:
            label += (ch if ch != " " else "_").ljust(
                font["glyphs"][ord(ch) - font["first"]][3])
        print("   " + label + "  (0x%02X..)" % start)
    return ok


def same_glyph(a, b, ch):
    """Identical metrics *and* identical pixels for `ch` in both fonts."""
    ga = a["glyphs"][ord(ch) - a["first"]]
    gb = b["glyphs"][ord(ch) - b["first"]]
    return (ga[1:] == gb[1:] and
            glyph_rows_gfx(a, ch) == glyph_rows_gfx(b, ch))


def side_by_side(fonts, text, top=-5, bottom=0, indent="   "):
    """Print the same text once per font, baseline-aligned, stacked."""
    width = max(len(f["name"]) for f in fonts)
    for f in fonts:
        rows, w = text_rows_gfx(f, text, top, bottom)
        for i, r in enumerate(rows):
            tag = f["name"].rjust(width) if i == 0 else " " * width
            print("%s%s  |%s|%s" % (indent, tag, r,
                                    "  %dpx" % w if i == 0 else ""))
        print("%s%s  +%s+" % (indent, " " * width, "-" * w))


def verify_panel(panel, org, seg3):
    """Prove Panel3x5's digits are Seg3x5's and its letters/rest are Org3x5's."""
    ok = True
    print("=" * 66)
    print("HYBRID %s -- provenance check (digits<-%s, everything else<-%s)"
          % (panel["name"], seg3["name"], org["name"]))

    print("\n-- required glyphs, panel vs both sources --")
    for ch in "0123456789:ABabmrest":
        src = seg3 if ord(ch) in PANEL3_DIGITS else org
        other = org if src is seg3 else seg3
        rows_p = text_rows_gfx(panel, ch, -5, 0)[0]
        rows_s = text_rows_gfx(src, ch, -5, 0)[0]
        rows_o = text_rows_gfx(other, ch, -5, 0)[0]
        match = same_glyph(panel, src, ch)
        if not match:
            ok = False
        print("  '%s'  expected from %s: %s" %
              (ch, src["name"], "MATCH" if match else "!! MISMATCH"))
        print("        %-10s %-10s %-10s" %
              (panel["name"], src["name"] + " (src)", other["name"]))
        for a, b, c in zip(rows_p, rows_s, rows_o):
            print("        %-10s %-10s %-10s" % (a, b, c))

    print("\n-- provenance over the whole 0x20..0x7E range --")
    bad = []
    for code in range(0x20, 0x7F):
        ch = chr(code)
        src = seg3 if code in PANEL3_DIGITS else org
        if not same_glyph(panel, src, ch):
            bad.append("0x%02X '%s' != %s" % (code, ch, src["name"]))
    if bad:
        ok = False
        for b in bad:
            print("  !! " + b)
    else:
        print("  all 95 code points match their intended source"
              " (10 digits from %s, 85 from %s)" % (seg3["name"], org["name"]))

    # the digits must actually differ from Org3x5's, else the merge did nothing
    changed = [c for c in PANEL3_DIGITS if not same_glyph(panel, org, chr(c))]
    print("  digits that differ from %s (i.e. really came from %s): %s"
          % (org["name"], seg3["name"],
             " ".join(chr(c) for c in sorted(changed)) or "NONE -- merge no-op"))
    letters = [c for c in list(range(0x41, 0x5B)) + list(range(0x61, 0x7B))
               if not same_glyph(panel, seg3, chr(c))]
    print("  letters that differ from %s: %d of 52"
          % (seg3["name"], len(letters)))

    print("\n-- A-Z: panel vs sources --")
    for chunk in wrap_to_width(panel, "".join(chr(c) for c in range(0x41, 0x5B)), 64):
        side_by_side([panel, org, seg3], chunk)
    print("-- a-z: panel vs sources --")
    for chunk in wrap_to_width(panel, "".join(chr(c) for c in range(0x61, 0x7B)), 64):
        side_by_side([panel, org, seg3], chunk)
    print("-- 0-9 and ':' : panel vs sources --")
    side_by_side([panel, seg3, org], "0123456789:")

    print("\n-- sample strips vs the 64px panel --")
    for s in ("125m 1:30 warmup", "rest 0:20 easy", "0123456789"):
        rows, w = text_rows_gfx(panel, s, -5, 0)
        print('  "%s" -> %dpx of 64 [%s]'
              % (s, w, "fits" if w <= 64 else "OVERFLOWS"))
        for r in rows:
            print("      |" + r.ljust(64) + "|")
        if w > 64:
            ok = False

    lower_ok = any(glyph_rows_gfx(panel, chr(c)) !=
                   glyph_rows_gfx(panel, chr(c - 0x20)) for c in range(0x61, 0x7B))
    print("\n-- distinct lowercase retained? %s --"
          % ("YES" if lower_ok else "NO -- font went caps-only"))
    ok = ok and lower_ok
    return ok


def verify(fonts, glcd):
    probe = "A0834rest125m:"
    ok = True
    for f in fonts:
        print("=" * 60)
        print(f"{f['name']}  first={f['first']} last={f['last']} "
              f"yAdvance={f['yAdvance']} bitmapBytes={len(f['bitmaps'])} "
              f"glyphs={len(f['glyphs'])}")
        for ch in probe:
            rows = glyph_rows_gfx(f, ch)
            print(f"  '{ch}'  {f['glyphs'][ord(ch) - f['first']]}")
            for r in rows:
                print("      " + r)
        # sanity: last glyph must not run past the bitmap array
        off, gw, gh, *_ = f["glyphs"][-1]
        need = off + (gw * gh + 7) // 8
        if need > len(f["bitmaps"]):
            print("  !! bitmap overrun", need, len(f["bitmaps"]))
            ok = False
    print("=" * 60)
    print("GLCD 6x8 built-in")
    for ch in probe:
        print(f"  '{ch}'")
        for r in glyph_rows_glcd(glcd, ch):
            print("      " + r)
    return ok


# ---------------------------------------------------------------- emit
def emit(fonts, glcd):
    lines = ["window.GLCD_FONT = " + json.dumps(glcd) + ";",
             "window.GFX_FONTS = {"]
    for f in fonts:
        lines.append('  "%s": %s,' % (f["name"], json.dumps(
            {k: f[k] for k in ("first", "last", "yAdvance", "bitmaps", "glyphs")})))
    lines.append("};")
    # names the page badges as hand-drawn rather than stock Adafruit
    lines.append("window.CUSTOM_FONTS = " + json.dumps(
        [f["name"] for f in fonts if f.get("custom")]) + ";")
    return "\n".join(lines)


def emit_panel_js(font) -> str:
    """Standalone `window.PANEL_FONT = {...}` file -- same object shape as
    `window.TOMTHUMB` in viewer/hub75-fonts.js, so an emulator page can pull
    the font in with a plain <script src> and no build step."""
    return "\n".join([
        "// %s -- hybrid 3x5 pixel font: Seg3x5 digits + Org3x5 letters,"
        % font["name"],
        "// punctuation and symbols. Real lowercase forms are kept; the caller",
        "// decides whether to upper-case at render time.",
        "// Same object shape as window.TOMTHUMB in hub75-fonts.js:",
        "//   glyph = [bitmapOffset, w, h, xAdvance, xOffset, yOffset],"
        " cursor y = baseline.",
        "// Generated by viewer/gen-font-options.py -- do not edit.",
        "window.PANEL_FONT = " + json.dumps(
            {k: font[k] for k in
             ("first", "last", "yAdvance", "bitmaps", "glyphs")}) + ";",
        "",
    ])


def build_customs():
    """Every hand-drawn font, in the order they should appear on the page."""
    org = build_custom_font(CUSTOM_NAME, CUSTOM_ART, CUSTOM_YADVANCE)
    seg3 = build_custom_font(SEG3_NAME, SEG3_ART, SEG3_YADVANCE, SEG3_NOTE)
    seg5 = build_custom_font(SEG5_NAME, SEG5_ART, SEG5_YADVANCE, SEG5_NOTE)
    seg5["oversized"] = True        # 6px digit pitch: the swim line will not fit
    panel = build_custom_font(PANEL3_NAME,
                              merge_art(CUSTOM_ART, SEG3_ART, PANEL3_DIGITS),
                              PANEL3_YADVANCE, PANEL3_NOTE)
    return [org, seg3, seg5, panel]


def main():
    fonts = [parse_gfx_font(n) for n in SMALL_FONTS]
    customs = build_customs()
    fonts.extend(customs)
    glcd = parse_glcd()

    if "--verify" in sys.argv:
        ok = verify(fonts, glcd)
        for c in customs:
            ok = verify_custom(c) and ok
        by = {c["name"]: c for c in customs}
        ok = verify_panel(by[PANEL3_NAME], by[CUSTOM_NAME], by[SEG3_NAME]) and ok
        sys.exit(0 if ok else 1)

    os.makedirs(FONTS_OUT_DIR, exist_ok=True)
    for c in customs:
        hdr = os.path.join(FONTS_OUT_DIR, c["name"] + ".h")
        with open(hdr, "w") as f:
            f.write(emit_c_header(c))
        print("wrote %s (%d bitmap bytes)" % (hdr, len(c["bitmaps"])))

    panel = next(c for c in customs if c["name"] == PANEL3_NAME)
    with open(OUT_PANEL_JS, "w") as f:
        f.write(emit_panel_js(panel))
    print("wrote %s" % OUT_PANEL_JS)

    js = emit(fonts, glcd)
    with open(OUT_HTML) as f:
        html = f.read()
    # NB: keep this non-greedy match anchored on `</script>` itself -- an earlier
    # version matched `\n</script>` and, because the placeholder block's newline
    # had already been consumed, ran on and ate the *next* script block whole.
    new, n = re.subn(
        r'(<script id="fontdata">)(?:(?!</script>)[\s\S])*(</script>)',
        lambda m: m.group(1) + "\n" + js + "\n" + m.group(2), html, count=1)
    if n == 0:
        sys.exit('error: no <script id="fontdata"> block in ' + OUT_HTML)
    with open(OUT_HTML, "w") as f:
        f.write(new)
    print("wrote %s (%d bytes of font JS)" % (OUT_HTML, len(js)))


if __name__ == "__main__":
    main()
