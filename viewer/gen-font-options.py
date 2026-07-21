#!/usr/bin/env python3
"""
Generate the inline font data for viewer/font-options.html.

Parses the Adafruit_GFX_Library headers directly.

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

# Every GFX font whose glyphs fit in ~8px. The Free* families are 9pt+ and are
# far too tall for a 64px list, so they are deliberately excluded.
SMALL_FONTS = ["TomThumb", "Picopixel", "Org_01", "Tiny3x3a2pt7b"]


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
    return "\n".join(lines)


def main():
    fonts = [parse_gfx_font(n) for n in SMALL_FONTS]
    glcd = parse_glcd()

    if "--verify" in sys.argv:
        sys.exit(0 if verify(fonts, glcd) else 1)

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
