#!/usr/bin/env python3
"""
Generate per-resolution Irrlicht XML bitmap fonts for AI Town HUD.

Run from repo root:
  python3 tools/generate_bitmap_fonts.py

Outputs (for each resolution tier):
  assets/fonts/hud_font_720.xml  + hud_font_720.png   (DejaVu Sans, cell_h=22)
  assets/fonts/hud_font_1080.xml + hud_font_1080.png  (DejaVu Sans, cell_h=33)
  assets/fonts/hud_font_1440.xml + hud_font_1440.png  (DejaVu Sans, cell_h=44)
  assets/fonts/hud_mono_font_720.xml  + hud_mono_font_720.png   (DejaVu Sans Mono, cell_h=22)
  assets/fonts/hud_mono_font_1080.xml + hud_mono_font_1080.png  (DejaVu Sans Mono, cell_h=33)
  assets/fonts/hud_mono_font_1440.xml + hud_mono_font_1440.png  (DejaVu Sans Mono, cell_h=44)

Cell height to resolution suffix mapping:
  22 -> 720   (≈720p HUD)
  33 -> 1080  (≈1080p HUD)
  44 -> 1440  (≈1440p HUD)

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
  All glyph rects share the same height (cell_h = ascent + descent).  Glyphs
  are drawn at (glyph_x, row_y) so PIL places the tallest ascender at the top
  of the cell and the baseline at row_y + ascent.  When Irrlicht renders all
  glyph rects with their tops at the same screen Y, every character shares the
  same visual baseline.

PNG format:
  RGBA, white glyphs on transparent background.  Irrlicht multiplies the white
  glyph pixels by the text colour set on the GUI element, so any text colour works.

Note: texture filename in the XML uses .png (NOT _0.png) — Irrlicht's CGUIFont
bitmap font loader reads PNG natively.
"""

import argparse
import math
import os
from PIL import Image, ImageDraw, ImageFont

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

CHARS = [chr(c) for c in range(32, 127)]  # ASCII 32 (space) .. 126 (~)
PNG_MAX_W = 512          # max row width in the atlas PNG
CELL_PAD = 1             # 1 px padding between atlas cells

# Cell height to resolution suffix
CELL_HEIGHT_TO_SUFFIX = {
    22: '720',
    33: '1080',
    44: '1440',
}

# Default cell heights matching 720p/1080p/1440p tiers
DEFAULT_CELL_HEIGHTS = [22, 33, 44]

# TTF sources (relative to repo root — script must be run from repo root)
TTF_SANS      = 'assets/fonts/source/DejaVuSans.ttf'
TTF_MONO      = 'assets/fonts/source/DejaVuSansMono.ttf'

# ---------------------------------------------------------------------------
# XML helpers
# ---------------------------------------------------------------------------

_XML_ESCAPES = {'&': '&amp;', '<': '&lt;', '>': '&gt;',
                '"': '&quot;', "'": '&apos;'}


def xml_char(ch):
    return _XML_ESCAPES.get(ch, ch)


# ---------------------------------------------------------------------------
# Font-size estimation: find the largest font size whose metrics produce
# exactly the requested cell height (ascent + descent == cell_h).
# Pillow/FreeType returns discrete integer metrics, so we search linearly.
# ---------------------------------------------------------------------------

def find_font_size_for_cell_height(ttf_path, target_cell_h):
    """Return the largest font size (pt/px) whose cell height equals target_cell_h."""
    size = target_cell_h  # start slightly above and scan downward
    for candidate in range(size, 0, -1):
        font = ImageFont.truetype(ttf_path, candidate)
        ascent, descent = font.getmetrics()
        if ascent + descent <= target_cell_h:
            return candidate
    return 1


# ---------------------------------------------------------------------------
# Main generator
# ---------------------------------------------------------------------------

def build(ttf_path, out_stem, face_name, cell_h):
    """Generate one bitmap font pair (XML + PNG) for the given cell height."""

    font_size = find_font_size_for_cell_height(ttf_path, cell_h)
    font = ImageFont.truetype(ttf_path, font_size)
    ascent, descent = font.getmetrics()
    # Use the requested cell_h as the authoritative height so all tiers are consistent.
    actual_cell_h = cell_h

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
    rows = []
    cur_row = []
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
    img_h = len(rows) * (actual_cell_h + CELL_PAD * 2)
    img = Image.new('RGBA', (PNG_MAX_W, img_h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    xml_entries = []

    for ri, row in enumerate(rows):
        row_y = ri * (actual_cell_h + CELL_PAD * 2)

        for g, gx, left_ext in row:
            # Draw position: rect left edge = gx + CELL_PAD.
            # For chars with negative left bearing the rect is extended left_ext pixels
            # to the left, so the glyph draw x is gx + CELL_PAD + left_ext
            # (= the advance origin within the rect).
            draw_x = gx + CELL_PAD + left_ext
            draw_y = row_y   # PIL places baseline at draw_y + ascent

            if g['ch'] != ' ':
                d.text((draw_x, draw_y), g['ch'],
                       font=font, fill=(255, 255, 255, 255))

            rect_l = gx + CELL_PAD - left_ext
            rect_t = row_y
            rect_r = gx + CELL_PAD + g['adv']
            rect_b = row_y + actual_cell_h

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
    # Save PNG  (NOTE: .png, NOT _0.png — Irrlicht CGUIFont loads .png natively)
    # ------------------------------------------------------------------
    png_filename = os.path.basename(out_stem) + '.png'
    png_path = out_stem + '.png'
    img.save(png_path)

    # ------------------------------------------------------------------
    # Write XML
    # ------------------------------------------------------------------
    xml_path = out_stem + '.xml'
    lines = [
        '<?xml version="1.0"?>',
        '<font type="bitmap">',
        f'  <Texture filename="{png_filename}"'
        f' index="0" hasAlpha="true" />',
        f'  <!-- {len(CHARS)} characters, ASCII 32-126.'
        f' Font: {face_name} {font_size}px.'
        f' Cell height: {actual_cell_h}px (ascent={ascent}, descent={descent}).'
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
          f' font_size={font_size}px, cell_h={actual_cell_h}'
          f' (ascent={ascent}, descent={descent})')


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description='Generate per-resolution Irrlicht bitmap font pairs for AI Town HUD.')
    parser.add_argument(
        '--cell-heights',
        default=','.join(str(h) for h in DEFAULT_CELL_HEIGHTS),
        help='Comma-separated list of cell heights (px). Default: 22,33,44')
    parser.add_argument(
        '--output-dir',
        default='assets/fonts',
        help='Directory to write generated font files. Default: assets/fonts')
    args = parser.parse_args()

    cell_heights = [int(h.strip()) for h in args.cell_heights.split(',')]
    output_dir = args.output_dir

    os.makedirs(output_dir, exist_ok=True)

    fonts = [
        (TTF_SANS, 'hud_font',      'DejaVu Sans'),
        (TTF_MONO, 'hud_mono_font', 'DejaVu Sans Mono'),
    ]

    for cell_h in cell_heights:
        suffix = CELL_HEIGHT_TO_SUFFIX.get(cell_h, str(cell_h))
        for ttf_path, stem_base, face_name in fonts:
            out_stem = os.path.join(output_dir, f'{stem_base}_{suffix}')
            build(ttf_path, out_stem, face_name, cell_h)


if __name__ == '__main__':
    main()
