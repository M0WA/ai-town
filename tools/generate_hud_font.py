#!/usr/bin/env python3
"""
Generate Irrlicht XML bitmap fonts for AI Town HUD.

Run from repo root:
  python3 tools/generate_hud_font.py

Outputs:
  assets/fonts/hud_font.xml + hud_font_0.png      (DejaVu Sans 18 px)
  assets/fonts/hud_mono_font.xml + hud_mono_font_0.png (DejaVu Sans Mono 18 px)

Font format (Irrlicht XML bitmap font):
  <c c="A" r="left,top,right,bottom" u="0" o="overhang" i="0" />
  - r:  pixel rect of the glyph in the PNG (left, top, right, bottom).
        rect height is uniform = ascent + descent for every character.
        rect width = advance width + any left-bearing extension.
  - o:  overhang — added to rect width to compute advance:
        advance = (right - left) + o.  Negative o reduces advance.
        Used to compensate when rect is widened to cover a negative left bearing.
  - u:  underhang (unused here, always 0).

Baseline alignment:
  All glyph rects share the same height (cell_h = ascent + descent = 22 px for
  18 px DejaVu Sans).  Glyphs are drawn at (glyph_x, row_y) so PIL places the
  tallest ascender at row_y + 3 and the baseline at row_y + 17.  When Irrlicht
  renders all glyph rects with their tops at the same screen Y, every character
  shares the same visual baseline.  This is the fix for the "irregular /
  vertically misaligned" appearance caused by the previous 11 px top-aligned font.

PNG format:
  RGBA, white glyphs on transparent background.  Irrlicht multiplies the white
  glyph pixels by the text colour set on the GUI element, so any text colour works.
"""

import math
import os
from PIL import Image, ImageDraw, ImageFont

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

FONT_SIZE = 18           # physical pixels; replaces the old 11 px font
CHARS = [chr(c) for c in range(32, 127)]  # ASCII 32 (space) .. 126 (~)
PNG_MAX_W = 512          # max row width in the atlas PNG
CELL_PAD = 1             # 1 px padding between atlas cells

FONTS = [
    (
        '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',
        'assets/fonts/hud_font',
        'DejaVu Sans',
    ),
    (
        '/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf',
        'assets/fonts/hud_mono_font',
        'DejaVu Sans Mono',
    ),
]

# ---------------------------------------------------------------------------
# XML helpers
# ---------------------------------------------------------------------------

_XML_ESCAPES = {'&': '&amp;', '<': '&lt;', '>': '&gt;',
                '"': '&quot;', "'": '&apos;'}


def xml_char(ch):
    return _XML_ESCAPES.get(ch, ch)


# ---------------------------------------------------------------------------
# Main generator
# ---------------------------------------------------------------------------

def build(ttf_path, out_stem, face_name):
    font = ImageFont.truetype(ttf_path, FONT_SIZE)
    ascent, descent = font.getmetrics()
    cell_h = ascent + descent   # height shared by every glyph rect

    # ------------------------------------------------------------------
    # Pass 1: measure every character
    # ------------------------------------------------------------------
    glyphs = []
    for ch in CHARS:
        adv = max(1, round(font.getlength(ch)))

        if ch == ' ':
            # Space: visible advance, no glyph pixels.
            glyphs.append({'ch': ch, 'adv': adv, 'left_b': 0})
            continue

        bbox = font.getbbox(ch)          # (left, top, right, bottom) at draw pos (0,0)
        left_b = bbox[0] if bbox else 0  # left bearing (negative = glyph extends left)
        glyphs.append({'ch': ch, 'adv': adv, 'left_b': left_b})

    # ------------------------------------------------------------------
    # Pass 2: pack glyphs into rows
    # ------------------------------------------------------------------
    rows: list[list] = []
    cur_row: list = []
    x = 0

    for g in glyphs:
        # rect width = advance + any left-bearing extension
        left_ext = max(0, -g['left_b'])   # pixels to extend rect left of advance origin
        rect_w = g['adv'] + left_ext + CELL_PAD * 2

        if x + rect_w > PNG_MAX_W and cur_row:
            rows.append(cur_row)
            cur_row = []
            x = 0

        cur_row.append((g, x, left_ext))
        x += rect_w

    if cur_row:
        rows.append(cur_row)

    # ------------------------------------------------------------------
    # Pass 3: render glyphs into atlas PNG
    # ------------------------------------------------------------------
    img_h = len(rows) * (cell_h + CELL_PAD * 2)
    img = Image.new('RGBA', (PNG_MAX_W, img_h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    xml_entries = []

    for ri, row in enumerate(rows):
        row_y = ri * (cell_h + CELL_PAD * 2)

        for g, gx, left_ext in row:
            # Draw position: rect left edge = gx + CELL_PAD.
            # For chars with negative left bearing (e.g. J, T, Y) the rect is
            # extended left_ext pixels to the left, so the glyph draw x is
            # gx + CELL_PAD + left_ext (= the advance origin within the rect).
            draw_x = gx + CELL_PAD + left_ext
            draw_y = row_y   # PIL places baseline at draw_y + ascent

            if g['ch'] != ' ':
                d.text((draw_x, draw_y), g['ch'],
                       font=font, fill=(255, 255, 255, 255))

            rect_l = gx + CELL_PAD - left_ext
            rect_t = row_y
            rect_r = gx + CELL_PAD + g['adv']
            rect_b = row_y + cell_h

            # Overhang compensates for rect width being wider than the advance.
            # advance = (rect_r - rect_l) + o  →  o = adv - (rect_r - rect_l)
            overhang = g['adv'] - (rect_r - rect_l)   # = -left_ext (≤ 0)

            xml_entries.append({
                'ch':  g['ch'],
                'l':   rect_l,
                't':   rect_t,
                'r':   rect_r,
                'b':   rect_b,
                'o':   overhang,
            })

    # ------------------------------------------------------------------
    # Save PNG
    # ------------------------------------------------------------------
    png_path = out_stem + '_0.png'
    img.save(png_path)

    # ------------------------------------------------------------------
    # Write XML
    # ------------------------------------------------------------------
    xml_path = out_stem + '.xml'
    lines = [
        '<?xml version="1.0"?>',
        '<font type="bitmap">',
        f'  <Texture filename="{os.path.basename(out_stem)}_0.png"'
        f' index="0" hasAlpha="true" />',
        f'  <!-- {len(CHARS)} characters, ASCII 32-126.'
        f' Font: {face_name} {FONT_SIZE}px.'
        f' Cell height: {cell_h}px (ascent={ascent}, descent={descent}).'
        f' Baseline at y=ascent={ascent} within every cell. -->',
    ]
    for e in xml_entries:
        lines.append(
            f'  <c c="{xml_char(e["ch"])}"'
            f' r="{e["l"]},{e["t"]},{e["r"]},{e["b"]}"'
            f' u="0" o="{e["o"]}" i="0" />'
        )
    lines += ['</font>', '']

    with open(xml_path, 'w') as f:
        f.write('\n'.join(lines))

    print(f'Generated {xml_path} + {png_path}')
    print(f'  {img.width}x{img.height} px, {len(rows)} rows,'
          f' cell_h={cell_h} (ascent={ascent}, descent={descent})')


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

if __name__ == '__main__':
    for ttf, stem, face in FONTS:
        build(ttf, stem, face)
