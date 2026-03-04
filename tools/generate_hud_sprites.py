#!/usr/bin/env python3
"""
Generate hud_sprites_ui.png -- 2048x2048 RGBA, 32x32 grid of 64x64 cells.

Two-layer glass design:
  1. Milky/satinato glass background panel behind rows 0-10 -- a soft,
     diffused, semi-opaque warm white surface like real sandblasted glass:
       - Base: rgba(235, 238, 242, 140)
       - Gaussian noise (sigma 1.5, amp 8) for micro-texture
       - Radial vignette (center brighter, edges greyer)
       - 1px glass-edge rim rgba(255,255,255,180)
  2. Nearly transparent frosted-glass icon cells that float on top:
       - Base fill: rgba(255, 255, 255, 55)  -- nearly see-through
       - Gloss arc (top 30%): white ellipse alpha 55, Gaussian-blurred
       - Rim: top/left rgba(255,255,255,160), bottom/right rgba(180,185,195,100)
       - Active state: very subtle cyan tint rgba(0,200,220,35)
       - Inactive state: plain frosted rgba(255,255,255,45)

The combination: milky glass surface creates the background glow; clear glass
icon cells sit transparently on top, their rounded edges revealing the milky
glass beneath.

Row assignments (from hud_sprite_ids.h):
  Row 0:  Toolbar tool-mode icons (active)        cols 0-4
  Row 1:  Toolbar tool-mode icons (inactive)       cols 0-4
  Row 2:  Zone sub-panel (active)                  cols 0-8
  Row 3:  Zone sub-panel (inactive)                cols 0-8
  Row 4:  Utilities sub-panel (active)             cols 0-3
  Row 5:  Utilities sub-panel (inactive)           cols 0-3
  Row 6:  Active tool indicator badges             cols 0-5
  Row 7:  Cursor shapes (reserved)                 cols 0-5
  Row 8:  Minimap overlay toggle (active)          col 0
  Row 9:  Minimap overlay toggle (inactive)        col 0
  Row 10: Notification / misc                      cols 0-3
  Rows 11-31: fully transparent (reserved)

Generator: tools/generate_hud_sprites.py
Render at 4x (256x256) then Lanczos-downsample to 64x64 for clean AA.
"""

import math
import os
from PIL import Image, ImageDraw, ImageFilter
import numpy as np

SHEET_SIZE = 2048
CELL = 64
COLS = 32
ROWS = 32

# Work at 4x resolution then downsample
SCALE = 4
HCELL = CELL * SCALE  # 256px working size

# -----------------------------------------------------------------------
# Color helpers
# -----------------------------------------------------------------------

def lerp_color(c1, c2, t):
    """Linearly interpolate between two RGBA tuples."""
    t = max(0.0, min(1.0, t))
    n = min(len(c1), len(c2))
    return tuple(int(c1[i] + (c2[i] - c1[i]) * t) for i in range(n))


def clamp(v, lo=0, hi=255):
    return max(lo, min(hi, int(v)))

# -----------------------------------------------------------------------
# Frosted-glass cell background
# -----------------------------------------------------------------------

def create_milkglass_cell(
        base_color=(255, 255, 255, 55),
        glow_color=None,
        corner_radius=40,
        tint_overlay=None):
    """
    Create a single frosted-glass chip cell at HCELL x HCELL.
    Nearly transparent (alpha ~55).  The gloss arc and rim are the main
    visible framing.  Icons sit on top of this glass chip.
    """
    S = HCELL
    cr = corner_radius
    M = 16  # margin from edge

    img = Image.new('RGBA', (S, S), (0, 0, 0, 0))

    # -- Active-state outer glow (behind everything) --
    if glow_color:
        glow = Image.new('RGBA', (S, S), (0, 0, 0, 0))
        gd = ImageDraw.Draw(glow)
        gd.rounded_rectangle((6, 6, S - 7, S - 7), radius=cr + 10,
                              outline=glow_color, width=7)
        glow = glow.filter(ImageFilter.GaussianBlur(radius=12))
        img = Image.alpha_composite(img, glow)

    # -- Very subtle drop shadow below the glass chip --
    shadow = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    sd = ImageDraw.Draw(shadow)
    sd.rounded_rectangle((M + 4, M + 6, S - M + 2, S - M + 4),
                         radius=cr, fill=(0, 0, 0, 25))
    shadow = shadow.filter(ImageFilter.GaussianBlur(radius=5))
    img = Image.alpha_composite(img, shadow)

    # -- Rounded-rect mask (binary shape: 255 inside, 0 outside) --
    mask = Image.new('L', (S, S), 0)
    md = ImageDraw.Draw(mask)
    md.rounded_rectangle((M, M, S - M - 1, S - M - 1), radius=cr, fill=255)

    # -- Base fill: nearly transparent frosted tint --
    # putalpha replaces the alpha channel, so we must scale the mask by the
    # desired alpha to get the correct per-pixel alpha (not 255 everywhere).
    r0, g0, b0, a0 = base_color
    base = Image.new('RGBA', (S, S), (r0, g0, b0, 255))
    base_alpha = np.array(mask, dtype=np.float32) * (a0 / 255.0)
    base.putalpha(Image.fromarray(base_alpha.astype(np.uint8), 'L'))
    img = Image.alpha_composite(img, base)

    # -- Optional tint overlay (for active/zone-colored states) --
    if tint_overlay:
        tr, tg, tb, ta = tint_overlay
        tint = Image.new('RGBA', (S, S), (tr, tg, tb, 255))
        tint_alpha = np.array(mask, dtype=np.float32) * (ta / 255.0)
        tint.putalpha(Image.fromarray(tint_alpha.astype(np.uint8), 'L'))
        img = Image.alpha_composite(img, tint)

    # -- Rim / bevel: the primary visible framing of the glass chip --
    # Top/left bright rim
    rim_bright = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    rbd = ImageDraw.Draw(rim_bright)
    rbd.rounded_rectangle((M, M, S - M - 1, S - M - 1),
                          radius=cr, outline=(255, 255, 255, 160), width=3)
    # Clip to top-left half (above the diagonal)
    rim_b_arr = np.array(rim_bright)
    for y in range(S):
        for x in range(S):
            if x + y > S:
                rim_b_arr[y, x, 3] = 0
    rim_bright = Image.fromarray(rim_b_arr, 'RGBA')
    img = Image.alpha_composite(img, rim_bright)

    # Bottom/right darker rim
    rim_dark = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    rdd = ImageDraw.Draw(rim_dark)
    rdd.rounded_rectangle((M, M, S - M - 1, S - M - 1),
                          radius=cr, outline=(180, 185, 195, 100), width=3)
    rim_d_arr = np.array(rim_dark)
    for y in range(S):
        for x in range(S):
            if x + y <= S:
                rim_d_arr[y, x, 3] = 0
    rim_dark = Image.fromarray(rim_d_arr, 'RGBA')
    img = Image.alpha_composite(img, rim_dark)

    # -- Gloss highlight arc (top 30%) -- the STRONGEST visual element --
    # This gives the glass look: a bright elliptical highlight in the top
    # portion of the cell.
    gloss = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    gloss_d = ImageDraw.Draw(gloss)
    gloss_top = M + 2
    gloss_bot = M + int((S - 2 * M) * 0.30)
    gloss_left = M + 14
    gloss_right = S - M - 14
    gloss_d.ellipse((gloss_left, gloss_top, gloss_right, gloss_bot),
                    fill=(255, 255, 255, 55))
    gloss = gloss.filter(ImageFilter.GaussianBlur(radius=10))
    # Clip to shape mask using min(gloss_alpha, mask)
    gloss_arr = np.array(gloss)
    mask_arr = np.array(mask)
    gloss_arr[:, :, 3] = np.minimum(gloss_arr[:, :, 3], mask_arr)
    gloss_clip = Image.fromarray(gloss_arr, 'RGBA')
    img = Image.alpha_composite(img, gloss_clip)

    return img


def downsample(img):
    """Downsample from HCELL to CELL using Lanczos."""
    return img.resize((CELL, CELL), Image.LANCZOS)


def add_icon_shadow(icon_layer, offset=8, alpha=60, blur=6):
    """Add a subtle drop shadow behind icon content at HCELL resolution."""
    S = icon_layer.size[0]
    a = icon_layer.split()[3]
    shadow_layer = Image.new('RGBA', (S, S), (0, 0, 0, alpha))
    shadow_layer.putalpha(a)
    shifted = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    shifted.paste(shadow_layer, (offset, offset))
    shifted = shifted.filter(ImageFilter.GaussianBlur(radius=blur))
    return Image.alpha_composite(shifted, icon_layer)


# -----------------------------------------------------------------------
# Glass palette presets
# -----------------------------------------------------------------------

def active_teal_cell():
    """Active toolbar cell: frosted glass + very subtle cyan-teal tint."""
    return create_milkglass_cell(
        base_color=(255, 255, 255, 55),
        glow_color=(0, 200, 220, 50),
        tint_overlay=(0, 200, 220, 35))

def inactive_cell():
    """Inactive cell: plain frosted glass."""
    return create_milkglass_cell(
        base_color=(255, 255, 255, 45),
        glow_color=None)

def zone_res_cell(active=True):
    if active:
        return create_milkglass_cell(
            base_color=(255, 255, 255, 55),
            glow_color=(60, 220, 90, 50),
            tint_overlay=(60, 220, 90, 30))
    return inactive_cell()

def zone_com_cell(active=True):
    if active:
        return create_milkglass_cell(
            base_color=(255, 255, 255, 55),
            glow_color=(60, 130, 240, 50),
            tint_overlay=(60, 130, 240, 30))
    return inactive_cell()

def zone_ind_cell(active=True):
    if active:
        return create_milkglass_cell(
            base_color=(255, 255, 255, 55),
            glow_color=(220, 200, 50, 50),
            tint_overlay=(220, 200, 50, 30))
    return inactive_cell()

def demolish_cell(active=True):
    if active:
        return create_milkglass_cell(
            base_color=(255, 255, 255, 55),
            glow_color=(240, 70, 70, 50),
            tint_overlay=(240, 70, 70, 30))
    return inactive_cell()

def dark_badge_cell():
    """Badge cell: same frosted glass."""
    return create_milkglass_cell(
        base_color=(255, 255, 255, 50),
        corner_radius=36)

def dark_panel_cell():
    """Panel/misc cell: same frosted glass."""
    return create_milkglass_cell(
        base_color=(255, 255, 255, 50),
        corner_radius=38)

def water_cell(active=True):
    """Water utility cell: frosted glass + subtle blue tint."""
    if active:
        return create_milkglass_cell(
            base_color=(255, 255, 255, 55),
            glow_color=(60, 160, 230, 50),
            tint_overlay=(40, 140, 220, 30))
    return inactive_cell()


# -----------------------------------------------------------------------
# Pixel-art letter helper
# -----------------------------------------------------------------------

_BITMAPS = {
    'R': [[1,1,1,1,0],[1,0,0,0,1],[1,0,0,0,1],[1,1,1,1,0],
          [1,0,1,0,0],[1,0,0,1,0],[1,0,0,0,1]],
    'C': [[0,1,1,1,0],[1,0,0,0,1],[1,0,0,0,0],[1,0,0,0,0],
          [1,0,0,0,0],[1,0,0,0,1],[0,1,1,1,0]],
    'I': [[1,1,1,1,1],[0,0,1,0,0],[0,0,1,0,0],[0,0,1,0,0],
          [0,0,1,0,0],[0,0,1,0,0],[1,1,1,1,1]],
    'F': [[1,1,1,1,1],[1,0,0,0,0],[1,0,0,0,0],[1,1,1,1,0],
          [1,0,0,0,0],[1,0,0,0,0],[1,0,0,0,0]],
    'i': [[0,0,1,0,0],[0,0,0,0,0],[0,0,1,0,0],[0,0,1,0,0],
          [0,0,1,0,0],[0,0,1,0,0],[0,0,1,0,0]],
}

def _draw_pixel_letter(d, letter, x, y, color, scale=4):
    bm = _BITMAPS.get(letter, _BITMAPS['R'])
    for ri, row_data in enumerate(bm):
        for ci, val in enumerate(row_data):
            if val:
                px, py = x + ci * scale, y + ri * scale
                d.rectangle([(px, py), (px + scale - 1, py + scale - 1)],
                            fill=color)


# -----------------------------------------------------------------------
# Row 0/1 -- Toolbar tools
# Vivid/bright icon colors (designed for any background)
# -----------------------------------------------------------------------

def draw_zone_toolbar_icon(active=True):
    """Col 0 -- Zone: 2x2 mini grid of colored tiles with crosshair."""
    cell = active_teal_cell() if active else inactive_cell()
    S = HCELL
    icon = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(icon)
    cx, cy = S // 2, S // 2

    # 2x2 colored tile grid -- vivid colors
    tile_size = 42
    gap = 6
    ox = cx - tile_size - gap // 2
    oy = cy - tile_size - gap // 2

    colors = [
        (80, 200, 100, 220),   # green (residential)
        (80, 140, 220, 220),   # blue (commercial)
        (220, 200, 70, 220),   # yellow (industrial)
        (150, 155, 165, 200),  # grey (unzoned)
    ]
    positions = [(0, 0), (1, 0), (0, 1), (1, 1)]
    for idx, (gx, gy) in enumerate(positions):
        tx = ox + gx * (tile_size + gap)
        ty = oy + gy * (tile_size + gap)
        c = colors[idx]
        d.rectangle([(tx, ty), (tx + tile_size, ty + tile_size)], fill=c)
        # Highlight top edge
        d.line([(tx, ty), (tx + tile_size, ty)],
               fill=(255, 255, 255, 90), width=2)
        # Shadow bottom edge
        d.line([(tx, ty + tile_size), (tx + tile_size, ty + tile_size)],
               fill=(0, 0, 0, 60), width=1)

    # White crosshair overlay
    ch_col = (255, 255, 255, 180)
    d.line([(cx, cy - 55), (cx, cy + 55)], fill=ch_col, width=3)
    d.line([(cx - 55, cy), (cx + 55, cy)], fill=ch_col, width=3)
    d.ellipse([(cx - 3, cy - 3), (cx + 3, cy + 3)], fill=ch_col)

    icon = add_icon_shadow(icon)
    return downsample(Image.alpha_composite(cell, icon))


def draw_road_toolbar_icon(active=True):
    """Col 1 -- Road: top-down T-junction, grey asphalt, white dashes."""
    cell = active_teal_cell() if active else inactive_cell()
    S = HCELL
    icon = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(icon)

    road_color = (95, 100, 110, 230)
    curb = (65, 68, 75, 230)
    dash_col = (255, 255, 255, 200)

    # Horizontal road
    d.rectangle([(36, 80), (220, 160)], fill=road_color)
    # Vertical road going down
    d.rectangle([(100, 130), (156, 224)], fill=road_color)

    # Curb edges
    d.line([(36, 80), (220, 80)], fill=curb, width=4)
    d.line([(36, 160), (100, 160)], fill=curb, width=4)
    d.line([(156, 160), (220, 160)], fill=curb, width=4)
    d.line([(100, 160), (100, 224)], fill=curb, width=4)
    d.line([(156, 160), (156, 224)], fill=curb, width=4)
    d.line([(100, 224), (156, 224)], fill=curb, width=3)

    # White dashed center-lines
    for x in range(44, 96, 22):
        d.line([(x, 120), (x + 12, 120)], fill=dash_col, width=3)
    for x in range(164, 216, 22):
        d.line([(x, 120), (x + 12, 120)], fill=dash_col, width=3)
    for y in range(168, 218, 22):
        d.line([(128, y), (128, y + 12)], fill=dash_col, width=3)

    # Subtle road surface texture
    d.line([(36, 80), (220, 80)], fill=(255, 255, 255, 50), width=2)

    icon = add_icon_shadow(icon)
    return downsample(Image.alpha_composite(cell, icon))


def draw_utilities_toolbar_icon(active=True):
    """Col 2 -- Utilities: amber lightning bolt + blue water drop side by side."""
    cell = active_teal_cell() if active else inactive_cell()
    S = HCELL
    icon = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(icon)

    # Lightning bolt (left side) -- vivid amber
    bolt_pts = [(108, 38), (62, 118), (92, 118), (72, 210),
                (140, 102), (110, 102), (130, 38)]
    d.polygon(bolt_pts, fill=(255, 210, 50, 240))
    d.polygon(bolt_pts, outline=(220, 170, 20, 180), width=2)
    bolt_inner = [(106, 52), (70, 116), (92, 116), (78, 194),
                  (132, 108), (112, 108), (126, 52)]
    d.polygon(bolt_inner, fill=(255, 245, 140, 100))
    d.line([(106, 46), (70, 112)], fill=(255, 255, 200, 150), width=2)

    # Water drop (right side) -- crystal clear teardrop
    _draw_crystal_drop_small(d, cx=178, top_y=52, bot_y=200)

    icon = add_icon_shadow(icon)
    return downsample(Image.alpha_composite(cell, icon))


def _draw_crystal_drop_small(d, cx=178, top_y=52, bot_y=200):
    """Draw a small crystal-clear water drop for the toolbar utilities icon."""
    bulge_top = top_y + int((bot_y - top_y) * 0.42)
    d.ellipse([(cx - 30, bulge_top), (cx + 30, bot_y)],
              fill=(100, 185, 245, 180))
    d.polygon([(cx, top_y), (cx - 30, bulge_top + 14), (cx + 30, bulge_top + 14)],
              fill=(110, 195, 250, 180))
    # Dark blue rim outline
    d.ellipse([(cx - 30, bulge_top), (cx + 30, bot_y)],
              outline=(0, 80, 150, 130), width=2)
    d.line([(cx, top_y), (cx - 30, bulge_top + 14)],
           fill=(0, 80, 150, 120), width=2)
    d.line([(cx, top_y), (cx + 30, bulge_top + 14)],
           fill=(0, 80, 150, 120), width=2)
    # White crescent highlight upper-left
    d.ellipse([(cx - 18, top_y + 24), (cx - 4, top_y + 48)],
              fill=(220, 240, 255, 180))
    d.ellipse([(cx - 14, top_y + 30), (cx - 6, top_y + 42)],
              fill=(255, 255, 255, 200))
    # Tiny specular dot
    d.ellipse([(cx - 16, top_y + 18), (cx - 10, top_y + 24)],
              fill=(255, 255, 255, 230))


def draw_demolish_toolbar_icon(active=True):
    """Col 3 -- Demolish: bold red X with sparks/debris at corners."""
    cell = demolish_cell(active)
    S = HCELL
    icon = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(icon)

    # Bold red X -- vivid
    x_col = (235, 60, 50, 245)
    d.line([(60, 56), (196, 200)], fill=x_col, width=22)
    d.line([(196, 56), (60, 200)], fill=x_col, width=22)

    # Highlight along top-left edges
    d.line([(60, 56), (196, 200)], fill=(255, 180, 160, 130), width=4)
    d.line([(196, 56), (60, 200)], fill=(255, 180, 160, 110), width=4)
    # Shadow
    d.line([(66, 62), (202, 206)], fill=(120, 20, 15, 80), width=4)

    # Spark/debris particles
    spark_positions = [(50, 46), (206, 46), (50, 210), (206, 210),
                       (40, 128), (216, 128), (128, 40), (128, 216)]
    for sx, sy in spark_positions:
        size = 3 + (sx % 4)
        d.ellipse([(sx - size, sy - size), (sx + size, sy + size)],
                  fill=(255, 220, 80, 200))
        d.ellipse([(sx - 1, sy - 1), (sx + 1, sy + 1)],
                  fill=(255, 255, 200, 230))

    icon = add_icon_shadow(icon)
    return downsample(Image.alpha_composite(cell, icon))


def draw_query_toolbar_icon(active=True):
    """Col 4 -- Query: magnifying glass with blue info 'i' inside lens."""
    cell = active_teal_cell() if active else inactive_cell()
    S = HCELL
    icon = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(icon)

    lcx, lcy, lr = 110, 100, 50

    # Lens glass fill
    d.ellipse([(lcx - lr, lcy - lr), (lcx + lr, lcy + lr)],
              fill=(160, 210, 255, 80))

    # Metallic rim
    for r_off in range(-5, 6):
        t = (r_off + 5) / 10.0
        c = lerp_color((220, 225, 238, 235), (120, 125, 140, 235), t)
        d.ellipse([(lcx - lr + r_off, lcy - lr + r_off),
                   (lcx + lr - r_off, lcy + lr - r_off)], outline=c, width=1)

    # Highlight arc on rim
    d.arc([(lcx - lr + 3, lcy - lr + 3), (lcx + lr - 3, lcy + lr - 3)],
          200, 340, fill=(255, 255, 255, 160), width=3)

    # Blue info "i" inside the lens
    i_col = (80, 160, 255, 230)
    d.ellipse([(lcx - 4, lcy - 28), (lcx + 4, lcy - 20)], fill=i_col)
    d.line([(lcx, lcy - 14), (lcx, lcy + 20)], fill=i_col, width=6)
    d.line([(lcx - 8, lcy - 14), (lcx + 8, lcy - 14)], fill=i_col, width=3)
    d.line([(lcx - 10, lcy + 20), (lcx + 10, lcy + 20)], fill=i_col, width=3)

    # Specular glint on lens
    d.ellipse([(lcx - 26, lcy - 34), (lcx - 14, lcy - 22)],
              fill=(255, 255, 255, 180))

    # Handle
    hx1, hy1 = lcx + lr - 12, lcy + lr - 12
    hx2, hy2 = 198, 204
    for i in range(-5, 6):
        t = (i + 5) / 10.0
        c = lerp_color((215, 220, 232, 235), (105, 110, 125, 235), t)
        d.line([(hx1 + i, hy1), (hx2 + i, hy2)], fill=c, width=2)
    d.line([(hx1 - 4, hy1 - 2), (hx2 - 4, hy2 - 2)],
           fill=(255, 255, 255, 110), width=2)

    icon = add_icon_shadow(icon)
    return downsample(Image.alpha_composite(cell, icon))


# -----------------------------------------------------------------------
# Row 2/3 -- Zone sub-panel (9 unique building icons)
# Vivid/bright icon colors
# -----------------------------------------------------------------------

def draw_res_low(active=True):
    """Small house, pitched roof, chimney with smoke puff."""
    cell = zone_res_cell(active)
    S = HCELL
    icon = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(icon)

    # House body (white walls)
    d.rectangle([(72, 130), (184, 206)], fill=(240, 240, 245, 230))
    d.line([(72, 130), (184, 130)], fill=(255, 255, 255, 160), width=2)
    d.line([(184, 130), (184, 206)], fill=(0, 0, 0, 50), width=2)

    # Pitched roof (red/brown)
    roof = [(62, 132), (128, 56), (194, 132)]
    d.polygon(roof, fill=(175, 65, 45, 235))
    d.polygon([(66, 130), (128, 62), (190, 130)], fill=(195, 80, 55, 220))
    d.line([(62, 132), (128, 56)], fill=(240, 130, 100, 140), width=2)
    d.line([(128, 56), (194, 132)], fill=(120, 40, 25, 100), width=2)

    # Window
    d.rectangle([(108, 148), (148, 180)], fill=(180, 215, 245, 200))
    d.rectangle([(108, 148), (148, 180)], outline=(100, 110, 130, 180), width=2)
    d.line([(128, 148), (128, 180)], fill=(100, 110, 130, 160), width=2)
    d.line([(108, 164), (148, 164)], fill=(100, 110, 130, 160), width=2)

    # Chimney
    d.rectangle([(158, 68), (178, 110)], fill=(160, 90, 70, 230))
    d.line([(158, 68), (178, 68)], fill=(180, 110, 85, 200), width=2)

    # Smoke puff
    d.ellipse([(156, 42), (180, 62)], fill=(200, 200, 210, 100))
    d.ellipse([(164, 30), (192, 54)], fill=(210, 210, 220, 80))

    # Door
    d.rectangle([(118, 184), (138, 206)], fill=(140, 95, 60, 220))
    d.ellipse([(132, 192), (136, 196)], fill=(220, 200, 100, 200))

    icon = add_icon_shadow(icon)
    return downsample(Image.alpha_composite(cell, icon))


def draw_res_med(active=True):
    """2-story townhouse, brick texture, multiple windows, dormer."""
    cell = zone_res_cell(active)
    S = HCELL
    icon = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(icon)

    # Building body (brick color)
    d.rectangle([(64, 90), (192, 210)], fill=(185, 130, 95, 235))
    for y in range(98, 210, 12):
        d.line([(64, y), (192, y)], fill=(165, 110, 78, 120), width=1)

    d.line([(64, 90), (192, 90)], fill=(220, 170, 130, 160), width=2)
    d.line([(192, 90), (192, 210)], fill=(0, 0, 0, 60), width=3)
    d.line([(64, 210), (192, 210)], fill=(0, 0, 0, 50), width=2)

    # Windows
    for floor_y in [102, 152]:
        for wx in [78, 116, 154]:
            d.rectangle([(wx, floor_y), (wx + 22, floor_y + 28)],
                        fill=(180, 215, 245, 195))
            d.rectangle([(wx, floor_y), (wx + 22, floor_y + 28)],
                        outline=(90, 70, 50, 170), width=2)
            d.line([(wx + 11, floor_y), (wx + 11, floor_y + 28)],
                   fill=(90, 70, 50, 140), width=1)

    # Pitched roof
    roof = [(56, 92), (128, 44), (200, 92)]
    d.polygon(roof, fill=(80, 75, 70, 230))
    d.line([(56, 92), (128, 44)], fill=(120, 115, 108, 160), width=2)
    d.line([(128, 44), (200, 92)], fill=(50, 48, 42, 100), width=2)

    # Dormer window
    dormer_pts = [(110, 88), (128, 60), (146, 88)]
    d.polygon(dormer_pts, fill=(185, 130, 95, 220))
    d.rectangle([(116, 68), (140, 88)], fill=(180, 215, 245, 180))
    d.rectangle([(116, 68), (140, 88)], outline=(80, 70, 60, 170), width=2)

    icon = add_icon_shadow(icon)
    return downsample(Image.alpha_composite(cell, icon))


def draw_res_high(active=True):
    """Tall apartment block, flat roof, grid of windows, balcony rails."""
    cell = zone_res_cell(active)
    S = HCELL
    icon = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(icon)

    # Tall body
    d.rectangle([(72, 36), (184, 214)], fill=(210, 215, 225, 235))
    d.line([(72, 36), (72, 214)], fill=(255, 255, 255, 100), width=2)
    d.line([(184, 36), (184, 214)], fill=(0, 0, 0, 70), width=3)
    d.line([(72, 36), (184, 36)], fill=(255, 255, 255, 130), width=2)

    # Flat roof cap
    d.rectangle([(68, 32), (188, 40)], fill=(170, 175, 185, 230))

    # Grid of windows
    for wy in range(48, 200, 28):
        for wx in [84, 118, 152]:
            d.rectangle([(wx, wy), (wx + 20, wy + 18)],
                        fill=(160, 200, 240, 190))
            d.rectangle([(wx, wy), (wx + 20, wy + 18)],
                        outline=(120, 130, 150, 150), width=1)

    # Balcony rails
    for wy in [70, 98, 126, 154, 182, 210]:
        d.line([(80, wy), (180, wy)], fill=(180, 185, 195, 130), width=2)
        for rx in [84, 118, 152, 176]:
            d.line([(rx, wy - 4), (rx, wy)], fill=(180, 185, 195, 130), width=1)

    icon = add_icon_shadow(icon)
    return downsample(Image.alpha_composite(cell, icon))


def draw_com_low(active=True):
    """Small corner shop with flat awning, glass storefront, hanging sign."""
    cell = zone_com_cell(active)
    S = HCELL
    icon = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(icon)

    # Building body
    d.rectangle([(56, 90), (200, 210)], fill=(230, 230, 235, 230))
    d.line([(56, 90), (200, 90)], fill=(255, 255, 255, 140), width=2)
    d.line([(200, 90), (200, 210)], fill=(0, 0, 0, 50), width=2)

    # Flat awning in blue/white stripes
    for x in range(52, 204, 12):
        c = (70, 110, 200, 220) if (x // 12) % 2 == 0 else (240, 240, 250, 220)
        d.rectangle([(x, 126), (x + 11, 146)], fill=c)
    d.line([(52, 126), (204, 126)], fill=(255, 255, 255, 120), width=2)
    d.line([(52, 146), (204, 146)], fill=(0, 0, 0, 60), width=2)

    # Glass storefront
    d.rectangle([(66, 150), (190, 206)], fill=(170, 210, 240, 180))
    d.rectangle([(66, 150), (190, 206)], outline=(100, 115, 140, 180), width=2)
    d.line([(128, 150), (128, 206)], fill=(100, 115, 140, 140), width=2)
    d.line([(78, 160), (78, 196)], fill=(255, 255, 255, 80), width=2)
    d.line([(150, 158), (150, 194)], fill=(255, 255, 255, 60), width=2)

    # Upper window
    d.rectangle([(100, 98), (156, 118)], fill=(170, 210, 240, 170))
    d.rectangle([(100, 98), (156, 118)], outline=(100, 115, 140, 160), width=2)

    # Hanging sign
    d.rectangle([(200, 100), (232, 132)], fill=(220, 190, 70, 220))
    d.rectangle([(200, 100), (232, 132)], outline=(180, 150, 40, 200), width=2)
    d.line([(200, 100), (200, 132)], fill=(140, 140, 150, 200), width=3)

    icon = add_icon_shadow(icon)
    return downsample(Image.alpha_composite(cell, icon))


def draw_com_med(active=True):
    """4-story office, curtain-wall glass, horizontal floor bands, logo on top."""
    cell = zone_com_cell(active)
    S = HCELL
    icon = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(icon)

    # Body
    d.rectangle([(68, 48), (188, 214)], fill=(140, 170, 210, 230))
    d.line([(68, 48), (68, 214)], fill=(200, 220, 245, 130), width=2)
    d.line([(188, 48), (188, 214)], fill=(0, 0, 0, 70), width=3)
    d.line([(68, 48), (188, 48)], fill=(200, 220, 245, 150), width=2)

    # Floor bands
    for fy in [88, 128, 168, 208]:
        d.line([(68, fy), (188, fy)], fill=(90, 110, 140, 170), width=4)

    # Glass panels
    for fy in [52, 92, 132, 172]:
        for wx in [76, 110, 144]:
            d.rectangle([(wx, fy + 4), (wx + 28, fy + 30)],
                        fill=(120, 175, 235, 190))
            d.line([(wx + 4, fy + 6), (wx + 4, fy + 26)],
                   fill=(200, 230, 255, 90), width=2)

    # Logo on top
    d.rectangle([(112, 36), (144, 48)], fill=(220, 220, 230, 200))
    d.line([(112, 36), (144, 36)], fill=(255, 255, 255, 140), width=1)

    icon = add_icon_shadow(icon)
    return downsample(Image.alpha_composite(cell, icon))


def draw_com_high(active=True):
    """Sleek skyscraper, tapered spire, reflective blue glass, antenna."""
    cell = zone_com_cell(active)
    S = HCELL
    icon = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(icon)

    # Main tower body
    body = [(88, 36), (80, 214), (176, 214), (168, 36)]
    d.polygon(body, fill=(90, 145, 210, 235))

    d.line([(88, 36), (80, 214)], fill=(150, 200, 250, 120), width=2)
    d.line([(168, 36), (176, 214)], fill=(40, 70, 110, 100), width=3)

    # Reflective glass skin
    for wx in range(92, 166, 14):
        for wy in range(42, 210, 20):
            d.rectangle([(wx, wy), (wx + 10, wy + 16)],
                        fill=(70, 130, 200, 180))
            d.line([(wx + 2, wy + 2), (wx + 2, wy + 12)],
                   fill=(140, 200, 255, 80), width=1)

    # Tapered spire
    d.polygon([(118, 36), (128, 8), (138, 36)], fill=(160, 190, 230, 230))
    d.line([(128, 8), (118, 36)], fill=(200, 225, 255, 140), width=2)
    d.line([(128, 8), (138, 36)], fill=(60, 90, 140, 100), width=2)

    # Antenna
    d.line([(128, 8), (128, -4)], fill=(200, 210, 225, 210), width=3)
    d.ellipse([(124, -8), (132, 0)], fill=(220, 225, 235, 200))

    d.line([(88, 36), (168, 36)], fill=(200, 225, 255, 140), width=2)

    icon = add_icon_shadow(icon)
    return downsample(Image.alpha_composite(cell, icon))


def draw_ind_low(active=True):
    """Warehouse shed, corrugated roof, large rolling door, loading bay."""
    cell = zone_ind_cell(active)
    S = HCELL
    icon = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(icon)

    # Warehouse body
    d.rectangle([(48, 110), (208, 210)], fill=(190, 190, 195, 230))
    d.line([(48, 110), (208, 110)], fill=(255, 255, 255, 120), width=2)
    d.line([(208, 110), (208, 210)], fill=(0, 0, 0, 60), width=3)

    # Corrugated metal roof
    roof = [(38, 112), (128, 60), (218, 112)]
    d.polygon(roof, fill=(160, 165, 175, 235))
    for x in range(50, 210, 12):
        t = (x - 38) / 180.0
        ry = int(112 - 52 * (1 - abs(2 * t - 1)))
        d.line([(x, ry), (x, 112)], fill=(140, 145, 155, 80), width=1)
    d.line([(38, 112), (128, 60)], fill=(200, 205, 215, 160), width=2)
    d.line([(128, 60), (218, 112)], fill=(110, 115, 125, 120), width=2)

    # Large rolling door
    d.rectangle([(72, 128), (160, 208)], fill=(130, 135, 145, 220))
    for y in range(136, 208, 10):
        d.line([(74, y), (158, y)], fill=(110, 115, 125, 150), width=1)
    d.rectangle([(72, 128), (160, 208)], outline=(100, 105, 115, 200), width=3)

    # Loading bay
    d.rectangle([(48, 198), (208, 210)], fill=(150, 150, 158, 225))
    d.line([(48, 198), (208, 198)], fill=(180, 180, 188, 180), width=2)

    icon = add_icon_shadow(icon)
    return downsample(Image.alpha_composite(cell, icon))


def draw_ind_med(active=True):
    """Factory with sawtooth skylights on roof, single smokestack with smoke."""
    cell = zone_ind_cell(active)
    S = HCELL
    icon = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(icon)

    # Factory body
    d.rectangle([(48, 100), (200, 212)], fill=(195, 190, 185, 230))
    d.line([(48, 100), (200, 100)], fill=(230, 225, 220, 140), width=2)
    d.line([(200, 100), (200, 212)], fill=(0, 0, 0, 60), width=3)

    # Sawtooth skylights
    for i in range(3):
        bx = 48 + i * 52
        tooth = [(bx, 102), (bx + 26, 66), (bx + 52, 102)]
        d.polygon(tooth, fill=(170, 168, 162, 230))
        d.polygon([(bx + 26, 66), (bx + 52, 102), (bx + 26, 102)],
                  fill=(170, 210, 240, 160))
        d.line([(bx, 102), (bx + 26, 66)], fill=(210, 208, 200, 150), width=2)
        d.line([(bx + 26, 66), (bx + 52, 102)], fill=(100, 100, 98, 100), width=2)

    # Windows
    for wx in [64, 108, 152]:
        d.rectangle([(wx, 120), (wx + 30, 148)],
                    fill=(170, 210, 240, 170))
        d.rectangle([(wx, 120), (wx + 30, 148)],
                    outline=(100, 100, 98, 160), width=2)

    # Door
    d.rectangle([(110, 170), (146, 212)], fill=(130, 128, 122, 220))
    d.rectangle([(110, 170), (146, 212)], outline=(100, 100, 98, 180), width=2)

    # Smokestack
    d.rectangle([(188, 38), (210, 100)], fill=(160, 155, 150, 230))
    d.line([(188, 38), (210, 38)], fill=(195, 190, 185, 200), width=2)
    d.line([(210, 38), (210, 100)], fill=(0, 0, 0, 50), width=2)

    # Smoke
    d.ellipse([(184, 14), (214, 38)], fill=(190, 195, 205, 90))
    d.ellipse([(192, -2), (226, 26)], fill=(200, 205, 215, 70))
    d.ellipse([(178, 0), (210, 22)], fill=(195, 200, 210, 55))

    icon = add_icon_shadow(icon)
    return downsample(Image.alpha_composite(cell, icon))


def draw_ind_high(active=True):
    """Large industrial complex, two tall smokestacks, pipes on exterior."""
    cell = zone_ind_cell(active)
    S = HCELL
    icon = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(icon)

    # Main building body
    d.rectangle([(40, 90), (190, 214)], fill=(180, 178, 172, 230))
    d.line([(40, 90), (190, 90)], fill=(220, 218, 210, 140), width=2)
    d.line([(190, 90), (190, 214)], fill=(0, 0, 0, 60), width=3)

    # Secondary smaller building
    d.rectangle([(140, 120), (216, 214)], fill=(170, 168, 162, 225))
    d.line([(216, 120), (216, 214)], fill=(0, 0, 0, 50), width=2)

    # Windows grid
    for wy in [102, 140, 178]:
        for wx in [56, 96, 136]:
            d.rectangle([(wx, wy), (wx + 26, wy + 22)],
                        fill=(160, 200, 230, 160))
            d.rectangle([(wx, wy), (wx + 26, wy + 22)],
                        outline=(100, 100, 98, 130), width=1)

    # Two tall smokestacks
    for sx in [58, 102]:
        d.rectangle([(sx, 20), (sx + 18, 90)], fill=(155, 152, 148, 235))
        d.line([(sx, 20), (sx + 18, 20)], fill=(190, 188, 182, 200), width=2)
        d.line([(sx + 18, 20), (sx + 18, 90)], fill=(0, 0, 0, 50), width=2)
        d.line([(sx - 2, 20), (sx + 20, 20)], fill=(175, 172, 168, 180), width=3)

    # Smoke puffs
    for sx_center in [67, 111]:
        d.ellipse([(sx_center - 16, -4), (sx_center + 16, 22)],
                  fill=(195, 200, 210, 80))
        d.ellipse([(sx_center - 10, -18), (sx_center + 18, 6)],
                  fill=(200, 205, 215, 60))

    # Exterior pipes
    for py in [108, 116]:
        d.line([(40, py), (80, py)], fill=(160, 155, 150, 180), width=3)
    d.line([(42, 108), (42, 214)], fill=(155, 150, 145, 170), width=4)

    icon = add_icon_shadow(icon)
    return downsample(Image.alpha_composite(cell, icon))


# -----------------------------------------------------------------------
# Row 4/5 -- Utilities sub-panel
# -----------------------------------------------------------------------

def draw_util_power(active=True):
    """Power Plant: amber lightning bolt inside circular gauge ring with ticks."""
    cell = active_teal_cell() if active else inactive_cell()
    S = HCELL
    icon = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(icon)
    cx, cy = S // 2, S // 2
    r = 72

    # Gauge ring
    d.ellipse([(cx - r, cy - r), (cx + r, cy + r)],
              outline=(200, 200, 210, 200), width=6)
    d.arc([(cx - r, cy - r), (cx + r, cy + r)],
          200, 340, fill=(255, 255, 255, 120), width=3)

    # Tick marks
    for i in range(12):
        angle = math.radians(i * 30 - 90)
        x1 = cx + int((r - 2) * math.cos(angle))
        y1 = cy + int((r - 2) * math.sin(angle))
        x2 = cx + int((r + 8) * math.cos(angle))
        y2 = cy + int((r + 8) * math.sin(angle))
        w = 3 if i % 3 == 0 else 1
        d.line([(x1, y1), (x2, y2)], fill=(200, 200, 210, 180), width=w)

    # Lightning bolt -- vivid amber
    bolt = [(cx + 8, cy - 52), (cx - 22, cy + 4), (cx - 2, cy + 4),
            (cx - 14, cy + 52), (cx + 28, cy - 10), (cx + 8, cy - 10),
            (cx + 20, cy - 52)]
    d.polygon(bolt, fill=(255, 210, 50, 240))
    bolt_inner = [(cx + 6, cy - 42), (cx - 16, cy + 2), (cx - 2, cy + 2),
                  (cx - 10, cy + 40), (cx + 22, cy - 8), (cx + 8, cy - 8),
                  (cx + 16, cy - 42)]
    d.polygon(bolt_inner, fill=(255, 245, 140, 100))
    d.line([(cx + 6, cy - 46), (cx - 18, cy)], fill=(255, 255, 200, 140), width=2)

    icon = add_icon_shadow(icon)
    return downsample(Image.alpha_composite(cell, icon))


def draw_util_water(active=True):
    """Water Tower: crystal-clear teardrop water drop on blue-tinted glass panel."""
    cell = water_cell(active)
    S = HCELL
    icon = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(icon)
    cx = S // 2

    # -- Crystal-clear teardrop water drop --
    bulge_top = 110
    bulge_bot = 218
    bulge_left = cx - 56
    bulge_right = cx + 56

    # Body: mostly transparent with glass-blue tint
    d.ellipse([(bulge_left, bulge_top), (bulge_right, bulge_bot)],
              fill=(120, 200, 255, 180))
    d.polygon([(cx, 32), (bulge_left, bulge_top + 28), (bulge_right, bulge_top + 28)],
              fill=(130, 205, 255, 175))

    # Transparent inner body -- lighter center
    inner_margin = 12
    d.ellipse([(bulge_left + inner_margin, bulge_top + inner_margin),
               (bulge_right - inner_margin, bulge_bot - inner_margin)],
              fill=(160, 220, 255, 80))
    d.polygon([(cx, 48),
               (bulge_left + inner_margin + 4, bulge_top + 28),
               (bulge_right - inner_margin - 4, bulge_top + 28)],
              fill=(170, 225, 255, 70))

    # Dark blue rim outline
    d.ellipse([(bulge_left, bulge_top), (bulge_right, bulge_bot)],
              outline=(0, 80, 150, 130), width=3)
    d.line([(cx, 32), (bulge_left, bulge_top + 28)],
           fill=(0, 80, 150, 120), width=3)
    d.line([(cx, 32), (bulge_right, bulge_top + 28)],
           fill=(0, 80, 150, 120), width=3)

    # Bright white crescent highlight on upper-left
    crescent = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    cd = ImageDraw.Draw(crescent)
    cd.ellipse([(cx - 38, 62), (cx - 6, 118)], fill=(255, 255, 255, 200))
    cd.ellipse([(cx - 32, 70), (cx - 12, 108)], fill=(255, 255, 255, 240))
    crescent = crescent.filter(ImageFilter.GaussianBlur(radius=4))
    icon = Image.alpha_composite(icon, crescent)

    # Tiny bright specular dot
    d.ellipse([(cx - 34, 56), (cx - 24, 66)], fill=(255, 255, 255, 250))

    # Secondary highlight lower-right (refraction)
    d.ellipse([(cx + 14, bulge_top + 50), (cx + 28, bulge_top + 66)],
              fill=(255, 255, 255, 90))

    # Rim lighting
    d.arc([(bulge_left, bulge_top), (bulge_right, bulge_bot)],
          200, 340, fill=(140, 210, 255, 100), width=3)

    icon = add_icon_shadow(icon)
    return downsample(Image.alpha_composite(cell, icon))


def _composite_badge(badge_rgb, badge_alpha, cell, S):
    """Helper: composite a badge that was drawn on an opaque RGB canvas.

    badge_rgb  -- (S, S, 3) uint8 numpy array, the fully opaque badge artwork
    badge_alpha -- (S, S) uint8 numpy array, per-pixel alpha for the badge shape
    cell       -- the glass cell background image (RGBA)

    Returns a downsampled 64x64 RGBA image ready for placement.

    By drawing the badge on an opaque (non-transparent) canvas and providing
    the alpha mask separately we avoid grey AA fringe: every RGB pixel already
    has the correct color, anti-aliased against the badge's own background
    color -- not against transparent black (0,0,0,0).
    """
    icon = np.zeros((S, S, 4), dtype=np.uint8)
    icon[:, :, :3] = badge_rgb
    icon[:, :, 3] = badge_alpha
    icon_img = Image.fromarray(icon, 'RGBA')
    icon_img = add_icon_shadow(icon_img)
    return downsample(Image.alpha_composite(cell, icon_img))


def draw_util_fire(active=True):
    """Fire Station: red shield badge, white flame centered, golden border.

    Drawn on an opaque canvas to avoid grey AA fringe on flame/shield edges.
    """
    cell = active_teal_cell() if active else inactive_cell()
    S = HCELL
    cx, cy = S // 2, S // 2

    # --- Draw on an opaque canvas (no transparent-black AA fringe) ---
    # Use a neutral mid-grey background that will be masked away; the badge
    # shapes are drawn directly on it so AA blends against the badge colors.
    canvas = Image.new('RGB', (S, S), (128, 128, 128))
    dc = ImageDraw.Draw(canvas)

    # Shield outline (golden border)
    shield_outer = [(cx, 30), (62, 60), (48, 120), (56, 178), (86, 210),
                    (cx, 228), (170, 210), (200, 178), (208, 120), (194, 60)]
    dc.polygon(shield_outer, fill=(215, 185, 65))

    # Red shield body (inset)
    shield_inner = [(cx, 40), (70, 66), (56, 120), (64, 174), (90, 204),
                    (cx, 220), (166, 204), (192, 174), (200, 120), (186, 66)]
    dc.polygon(shield_inner, fill=(220, 70, 60))

    # Red gradient overlay (drawn opaquely blended by line)
    for y in range(40, 220):
        t = (y - 40) / 180.0
        r = int(220 + (245 - 220) * (1 - t) * 0.22)
        g = int(70 + (100 - 70) * (1 - t) * 0.22)
        b = int(60 + (90 - 60) * (1 - t) * 0.22)
        dc.line([(70, y), (186, y)], fill=(r, g, b), width=1)

    # Re-draw red body cleanly so the gradient does not spill outside
    dc.polygon(shield_inner, outline=(220, 70, 60), width=1)

    # White flame icon -- fully opaque white on the red badge surface
    flame_outer = [(cx, 72), (cx - 22, 118), (cx - 30, 156), (cx - 24, 178),
                   (cx, 162), (cx + 24, 178), (cx + 30, 156), (cx + 22, 118)]
    dc.polygon(flame_outer, fill=(255, 255, 255))

    # Inner flame detail -- cream tint drawn opaquely
    flame_inner = [(cx, 88), (cx - 14, 120), (cx - 20, 150), (cx - 14, 168),
                   (cx, 156), (cx + 14, 168), (cx + 20, 150), (cx + 14, 120)]
    dc.polygon(flame_inner, fill=(255, 240, 230))

    # Shield highlight arc
    dc.arc([(62, 44), (194, 130)], 200, 340, fill=(255, 230, 200), width=3)

    # --- Build alpha mask from the shield shape ---
    alpha = Image.new('L', (S, S), 0)
    ad = ImageDraw.Draw(alpha)
    ad.polygon(shield_outer, fill=235)

    badge_rgb = np.array(canvas)
    badge_a = np.array(alpha)

    return _composite_badge(badge_rgb, badge_a, cell, S)


def draw_util_police(active=True):
    """Police Station: blue shield badge, gold 5-point star, silver border.

    Drawn on an opaque canvas to avoid grey AA fringe on star/shield edges.
    """
    cell = active_teal_cell() if active else inactive_cell()
    S = HCELL
    cx, cy = S // 2, S // 2

    # --- Draw on an opaque canvas ---
    canvas = Image.new('RGB', (S, S), (128, 128, 128))
    dc = ImageDraw.Draw(canvas)

    # Shield outline (silver border)
    shield_outer = [(cx, 30), (62, 60), (48, 120), (56, 178), (86, 210),
                    (cx, 228), (170, 210), (200, 178), (208, 120), (194, 60)]
    dc.polygon(shield_outer, fill=(195, 200, 212))

    # Blue shield body (inset)
    shield_inner = [(cx, 40), (70, 66), (56, 120), (64, 174), (90, 204),
                    (cx, 220), (166, 204), (192, 174), (200, 120), (186, 66)]
    dc.polygon(shield_inner, fill=(70, 130, 220))

    # Blue gradient overlay (drawn opaquely blended by line)
    for y in range(40, 220):
        t = (y - 40) / 180.0
        r = int(70 + (100 - 70) * (1 - t) * 0.25)
        g = int(130 + (170 - 130) * (1 - t) * 0.25)
        b = int(220 + (245 - 220) * (1 - t) * 0.25)
        dc.line([(70, y), (186, y)], fill=(r, g, b), width=1)

    dc.polygon(shield_inner, outline=(70, 130, 220), width=1)

    # Gold 5-point star -- fully opaque on the blue badge surface
    star_cx, star_cy = cx, cy + 10
    star_r = 40
    star_pts = []
    for i in range(10):
        angle = math.radians(i * 36 - 90)
        r = star_r if i % 2 == 0 else star_r * 0.38
        star_pts.append((
            int(star_cx + r * math.cos(angle)),
            int(star_cy + r * math.sin(angle))
        ))
    dc.polygon(star_pts, fill=(255, 225, 80))
    dc.polygon(star_pts, outline=(210, 180, 50), width=2)
    # Tiny specular on star
    dc.ellipse([(star_cx - 8, star_cy - 14), (star_cx, star_cy - 6)],
               fill=(255, 255, 210))

    # Shield highlight arc
    dc.arc([(62, 44), (194, 130)], 200, 340, fill=(220, 225, 240), width=3)

    # --- Build alpha mask from the shield shape ---
    alpha = Image.new('L', (S, S), 0)
    ad = ImageDraw.Draw(alpha)
    ad.polygon(shield_outer, fill=235)

    badge_rgb = np.array(canvas)
    badge_a = np.array(alpha)

    return _composite_badge(badge_rgb, badge_a, cell, S)


# -----------------------------------------------------------------------
# Row 6 -- Active tool indicator badges
# -----------------------------------------------------------------------

def draw_indicator_icon(tool_type='none'):
    """Small compact indicator badge."""
    cell = dark_badge_cell()
    S = HCELL
    icon = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(icon)
    cx, cy = S // 2, S // 2

    if tool_type == 'none':
        d.line([(cx - 32, cy), (cx + 32, cy)], fill=(160, 165, 175, 210), width=6)
    elif tool_type == 'zone':
        for gx, gy, c in [(0, 0, (80, 200, 100, 200)),
                           (1, 0, (80, 140, 220, 200)),
                           (0, 1, (220, 200, 70, 200)),
                           (1, 1, (150, 155, 165, 180))]:
            d.rectangle([(cx - 36 + gx * 38, cy - 36 + gy * 38),
                         (cx - 36 + gx * 38 + 32, cy - 36 + gy * 38 + 32)], fill=c)
    elif tool_type == 'road':
        d.rectangle([(cx - 14, cy - 40), (cx + 14, cy + 40)],
                    fill=(110, 115, 125, 220))
        for y in range(cy - 32, cy + 32, 16):
            d.line([(cx, y), (cx, y + 8)], fill=(255, 255, 200, 190), width=3)
    elif tool_type == 'utilities':
        bolt = [(cx - 10, cy - 32), (cx - 24, cy), (cx - 12, cy),
                (cx - 18, cy + 32), (cx + 6, cy - 6), (cx - 6, cy - 6)]
        d.polygon(bolt, fill=(255, 210, 50, 220))
        d.ellipse([(cx + 8, cy - 2), (cx + 36, cy + 30)],
                  fill=(50, 130, 215, 220))
        d.polygon([(cx + 22, cy - 22), (cx + 8, cy + 8), (cx + 36, cy + 8)],
                  fill=(65, 150, 230, 220))
    elif tool_type == 'demolish':
        d.line([(cx - 28, cy - 28), (cx + 28, cy + 28)],
               fill=(235, 70, 55, 230), width=8)
        d.line([(cx + 28, cy - 28), (cx - 28, cy + 28)],
               fill=(235, 70, 55, 230), width=8)
    elif tool_type == 'query':
        d.ellipse([(cx - 28, cy - 32), (cx + 16, cy + 12)],
                  outline=(180, 210, 250, 220), width=5)
        d.line([(cx + 8, cy + 4), (cx + 34, cy + 34)],
               fill=(180, 210, 250, 220), width=6)

    icon = add_icon_shadow(icon, offset=6)
    return downsample(Image.alpha_composite(cell, icon))


# -----------------------------------------------------------------------
# Row 7 -- Reserved cursor shapes
# -----------------------------------------------------------------------

def draw_cursor_icon(cursor_type='default'):
    """Cursor shape placeholder."""
    cell = dark_panel_cell()
    S = HCELL
    icon = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(icon)
    cx, cy = S // 2, S // 2

    if cursor_type == 'default':
        arrow = [(88, 46), (88, 192), (116, 164), (146, 208), (162, 198),
                 (134, 154), (168, 150)]
        d.polygon(arrow, fill=(255, 255, 255, 235))
        d.polygon(arrow, outline=(0, 0, 0, 110), width=3)
        d.polygon([(92, 54), (92, 142), (118, 122)], fill=(255, 255, 255, 50))
    elif cursor_type == 'zone':
        c = (100, 255, 130, 210)
        d.line([(cx, 48), (cx, 208)], fill=c, width=5)
        d.line([(48, cy), (208, cy)], fill=c, width=5)
        d.ellipse([(cx - 26, cy - 26), (cx + 26, cy + 26)],
                  outline=c, width=3)
        d.ellipse([(cx - 4, cy - 4), (cx + 4, cy + 4)], fill=c)
    elif cursor_type == 'road':
        d.rectangle([(94, 54), (162, 202)], fill=(115, 118, 128, 215))
        for y in range(62, 194, 22):
            d.line([(cx, y), (cx, y + 12)], fill=(255, 255, 200, 195), width=3)
        d.line([(94, 54), (94, 202)], fill=(190, 190, 200, 160), width=2)
        d.line([(162, 54), (162, 202)], fill=(55, 55, 65, 130), width=2)
    elif cursor_type == 'utilities':
        d.line([(76, 188), (180, 84)], fill=(200, 205, 220, 225), width=10)
        d.ellipse([(162, 54), (200, 92)], outline=(200, 205, 220, 225), width=5)
        d.line([(80, 184), (176, 86)], fill=(255, 255, 255, 90), width=2)
    elif cursor_type == 'demolish':
        d.line([(70, 70), (186, 186)], fill=(255, 80, 60, 225), width=10)
        d.line([(186, 70), (70, 186)], fill=(255, 80, 60, 225), width=10)
        d.line([(70, 70), (186, 186)], fill=(255, 255, 255, 50), width=2)
        d.line([(186, 70), (70, 186)], fill=(255, 255, 255, 50), width=2)
    elif cursor_type == 'query':
        d.ellipse([(66, 50), (162, 146)], outline=(180, 215, 250, 225), width=7)
        d.ellipse([(74, 58), (154, 138)], fill=(160, 210, 250, 50))
        d.line([(150, 136), (196, 190)], fill=(180, 215, 250, 225), width=9)
        d.ellipse([(90, 70), (106, 86)], fill=(255, 255, 255, 140))

    icon = add_icon_shadow(icon, offset=6)
    return downsample(Image.alpha_composite(cell, icon))


# -----------------------------------------------------------------------
# Row 8/9 -- Minimap toggle
# -----------------------------------------------------------------------

def draw_minimap_toggle(active=True):
    """Teal shield with checkmark (active) or grey with grey checkmark."""
    if active:
        cell = create_milkglass_cell(
            base_color=(255, 255, 255, 55),
            glow_color=(0, 200, 220, 45),
            tint_overlay=(0, 200, 220, 30))
    else:
        cell = inactive_cell()

    S = HCELL
    icon = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(icon)
    cx, cy = S // 2, S // 2

    # Shield shape
    shield = [(cx, 32), (62, 62), (50, 118), (58, 174), (88, 204),
              (cx, 222), (168, 204), (198, 174), (206, 118), (194, 62)]

    if active:
        d.polygon(shield, fill=(40, 170, 180, 230))
        for y in range(32, 222):
            t = (y - 32) / 190.0
            c = lerp_color((80, 210, 220, 40), (15, 100, 110, 30), t)
            d.line([(66, y), (190, y)], fill=c)
        check_col = (255, 255, 255, 240)
    else:
        d.polygon(shield, fill=(110, 115, 125, 210))
        for y in range(32, 222):
            t = (y - 32) / 190.0
            c = lerp_color((150, 155, 165, 30), (80, 85, 95, 20), t)
            d.line([(66, y), (190, y)], fill=c)
        check_col = (160, 165, 175, 200)

    # Checkmark
    d.line([(88, cy + 10), (cx - 6, cy + 46), (cx + 46, cy - 32)],
           fill=check_col, width=12)
    d.line([(90, cy + 8), (cx - 6, cy + 44)],
           fill=(255, 255, 255, 60 if active else 30), width=3)

    # Shield highlight arc
    d.arc([(62, 38), (194, 130)], 200, 340,
          fill=(255, 255, 255, 70 if active else 40), width=3)

    icon = add_icon_shadow(icon)
    return downsample(Image.alpha_composite(cell, icon))


# -----------------------------------------------------------------------
# Row 10 -- Misc HUD icons
# -----------------------------------------------------------------------

def draw_notification_bell():
    """Golden bell with red unread dot badge top-right."""
    cell = dark_panel_cell()
    S = HCELL
    icon = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(icon)

    # Bell body
    bell_pts = [(92, 74), (72, 130), (58, 172), (198, 172), (184, 130), (164, 74)]
    d.polygon(bell_pts, fill=(220, 195, 60, 235))

    # Metallic gradient
    for y in range(74, 176):
        t = (y - 74) / 100.0
        c = lerp_color((255, 240, 120, 65), (180, 148, 30, 45), t)
        d.line([(66, y), (190, y)], fill=c)

    # Bell dome arc
    d.arc([(90, 50), (166, 84)], 180, 360, fill=(255, 240, 120, 220), width=5)

    # Bell rim
    d.line([(58, 172), (198, 172)], fill=(240, 225, 90, 225), width=7)
    d.line([(60, 170), (196, 170)], fill=(255, 255, 200, 110), width=2)

    # Clapper
    d.ellipse([(112, 180), (144, 208)], fill=(205, 182, 55, 225))
    d.ellipse([(118, 186), (138, 202)], fill=(235, 220, 90, 190))

    # Handle/loop
    d.arc([(110, 34), (146, 62)], 0, 360, fill=(225, 210, 95, 210), width=5)

    # Specular highlight
    d.ellipse([(96, 92), (118, 120)], fill=(255, 255, 200, 150))
    d.ellipse([(100, 96), (112, 112)], fill=(255, 255, 235, 100))

    # Red unread dot badge
    d.ellipse([(172, 36), (210, 74)], fill=(230, 45, 40, 240))
    d.ellipse([(178, 42), (204, 68)], fill=(245, 80, 70, 200))
    d.ellipse([(182, 44), (194, 54)], fill=(255, 180, 170, 160))

    icon = add_icon_shadow(icon)
    return downsample(Image.alpha_composite(cell, icon))


def draw_clock():
    """Analog clock, white dial, dark hands at 10:10."""
    cell = dark_panel_cell()
    S = HCELL
    icon = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(icon)
    cx, cy = S // 2, S // 2
    r = 70

    # Clock face
    d.ellipse([(cx - r, cy - r), (cx + r, cy + r)], fill=(245, 245, 250, 235))

    # Outer ring bevel
    d.arc([(cx - r, cy - r), (cx + r, cy + r)],
          210, 30, fill=(255, 255, 255, 190), width=5)
    d.arc([(cx - r, cy - r), (cx + r, cy + r)],
          30, 210, fill=(100, 100, 115, 130), width=5)

    # Hour markers
    for i in range(12):
        angle = math.radians(i * 30 - 90)
        x1 = cx + int((r - 14) * math.cos(angle))
        y1 = cy + int((r - 14) * math.sin(angle))
        x2 = cx + int((r - 6) * math.cos(angle))
        y2 = cy + int((r - 6) * math.sin(angle))
        w = 4 if i % 3 == 0 else 2
        d.line([(x1, y1), (x2, y2)], fill=(50, 50, 60, 210), width=w)

    # Hour hand (10 o'clock)
    angle_h = math.radians(300 - 90)
    hx = cx + int(38 * math.cos(angle_h))
    hy = cy + int(38 * math.sin(angle_h))
    d.line([(cx, cy), (hx, hy)], fill=(35, 35, 48, 240), width=6)

    # Minute hand (2 o'clock)
    angle_m = math.radians(60 - 90)
    mx = cx + int(54 * math.cos(angle_m))
    my = cy + int(54 * math.sin(angle_m))
    d.line([(cx, cy), (mx, my)], fill=(35, 35, 48, 240), width=4)

    # Center cap
    d.ellipse([(cx - 8, cy - 8), (cx + 8, cy + 8)], fill=(55, 55, 68, 245))
    d.ellipse([(cx - 4, cy - 4), (cx + 4, cy + 4)], fill=(125, 125, 140, 225))

    # Specular glint
    d.ellipse([(cx - 36, cy - 44), (cx - 18, cy - 26)],
              fill=(255, 255, 255, 160))

    icon = add_icon_shadow(icon)
    return downsample(Image.alpha_composite(cell, icon))


def draw_unsaved_dot():
    """Amber dot warning."""
    cell = dark_panel_cell()
    S = HCELL
    icon = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(icon)
    cx, cy = S // 2, S // 2

    # Outer amber glow
    for ring_r in range(56, 28, -2):
        alpha = int(40 * (56 - ring_r) / 28)
        d.ellipse([(cx - ring_r, cy - ring_r), (cx + ring_r, cy + ring_r)],
                  fill=(255, 185, 30, alpha))

    # Main amber dot
    d.ellipse([(cx - 30, cy - 30), (cx + 30, cy + 30)],
              fill=(255, 185, 30, 245))

    # Gradient highlight top-left
    d.ellipse([(cx - 22, cy - 26), (cx - 2, cy - 6)],
              fill=(255, 240, 120, 200))
    d.ellipse([(cx - 16, cy - 20), (cx - 6, cy - 10)],
              fill=(255, 255, 195, 160))

    # Shadow arc bottom-right
    d.arc([(cx - 30, cy - 30), (cx + 30, cy + 30)],
          30, 150, fill=(200, 120, 10, 120), width=3)

    icon = add_icon_shadow(icon)
    return downsample(Image.alpha_composite(cell, icon))


def draw_undo_arrow():
    """Curved arrow arc, white/silver, pointing left."""
    cell = dark_panel_cell()
    S = HCELL
    icon = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(icon)
    cx, cy = S // 2, S // 2

    # Thick arc with metallic gradient
    for w_off in range(-5, 6):
        t = (w_off + 5) / 10.0
        c = lerp_color((255, 255, 255, 235), (175, 180, 195, 210), t)
        d.arc([(cx - 58 + w_off, cy - 50 + w_off),
               (cx + 58 - w_off, cy + 58 - w_off)],
              160, 380, fill=c, width=2)

    # Bright highlight on upper arc
    d.arc([(cx - 54, cy - 46), (cx + 54, cy + 54)],
          200, 340, fill=(255, 255, 255, 250), width=3)

    # Arrowhead
    ax = cx - 58
    ay = cy + 4
    arrowhead = [
        (ax - 10, ay),
        (ax + 26, ay - 28),
        (ax + 26, ay + 28),
    ]
    d.polygon(arrowhead, fill=(255, 255, 255, 235))
    d.line([(ax - 10, ay), (ax + 26, ay - 28)],
           fill=(255, 255, 255, 250), width=3)
    d.line([(ax - 10, ay), (ax + 26, ay + 28)],
           fill=(175, 180, 195, 190), width=3)

    icon = add_icon_shadow(icon, offset=6)
    return downsample(Image.alpha_composite(cell, icon))


# -----------------------------------------------------------------------
# Milky / satinato glass background panel
# -----------------------------------------------------------------------

def draw_milkyglass_background(sheet, x0, y0, x1, y1, corner_radius=16):
    """
    Draw a milky / satinato glass background panel onto *sheet* covering the
    pixel rectangle (x0, y0) to (x1, y1).  This simulates the look of real
    frosted/sandblasted glass: soft, diffused, semi-opaque warm white that
    scatters light.

    Layers (bottom to top):
      1. Rounded-rect base fill  rgba(235, 238, 242, 140)
      2. Gaussian noise (sigma ~1.5, amplitude ~8) for micro-texture
      3. Radial vignette: center brighter, edges slightly greyer
      4. 1 px glass-edge rim  rgba(255, 255, 255, 180)
    """
    W = x1 - x0
    H = y1 - y0
    cr = corner_radius

    # -- Shape mask --
    mask = Image.new('L', (W, H), 0)
    md = ImageDraw.Draw(mask)
    md.rounded_rectangle((0, 0, W - 1, H - 1), radius=cr, fill=255)
    mask_arr = np.array(mask, dtype=np.float32)  # 0..255

    # -- 1. Base fill --
    base_r, base_g, base_b, base_a = 235, 238, 242, 140
    base = np.zeros((H, W, 4), dtype=np.uint8)
    base[:, :, 0] = base_r
    base[:, :, 1] = base_g
    base[:, :, 2] = base_b
    base[:, :, 3] = base_a

    # -- 2. Gaussian noise for frosted micro-texture --
    rng = np.random.default_rng(seed=42)
    noise = rng.normal(loc=0.0, scale=1.5, size=(H, W)) * 8.0
    for ch in range(3):
        base[:, :, ch] = np.clip(
            base[:, :, ch].astype(np.float32) + noise, 0, 255
        ).astype(np.uint8)

    # -- 3. Radial vignette: center brighter, edges greyer --
    cy_f, cx_f = H / 2.0, W / 2.0
    max_dist = math.sqrt(cx_f ** 2 + cy_f ** 2)
    yy, xx = np.mgrid[0:H, 0:W]
    dist = np.sqrt((xx - cx_f) ** 2 + (yy - cy_f) ** 2) / max_dist  # 0..1
    # center color (248,250,252), edge color (220,223,228)
    center = np.array([248.0, 250.0, 252.0])
    edge = np.array([220.0, 223.0, 228.0])
    for ch in range(3):
        vignette_ch = center[ch] + (edge[ch] - center[ch]) * dist
        # Blend: lerp between current base and vignette target at 50%
        cur = base[:, :, ch].astype(np.float32)
        blended = cur * 0.5 + vignette_ch * 0.5
        base[:, :, ch] = np.clip(blended, 0, 255).astype(np.uint8)

    # -- Apply shape mask to alpha channel --
    base[:, :, 3] = (mask_arr * (base_a / 255.0)).astype(np.uint8)

    panel = Image.fromarray(base, 'RGBA')

    # -- 4. Glass-edge rim --
    rim = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    rd = ImageDraw.Draw(rim)
    rd.rounded_rectangle((0, 0, W - 1, H - 1), radius=cr,
                         outline=(255, 255, 255, 180), width=2)
    # Clip rim to mask shape
    rim_arr = np.array(rim)
    rim_arr[:, :, 3] = np.minimum(rim_arr[:, :, 3], mask_arr.astype(np.uint8))
    rim = Image.fromarray(rim_arr, 'RGBA')
    panel = Image.alpha_composite(panel, rim)

    # -- Composite onto sheet --
    sheet.paste(panel, (x0, y0), panel)


# ============================================================
# MAIN ASSEMBLY
# ============================================================

def main():
    print("Generating HUD sprite sheet (2048x2048, frosted-glass style)...")
    sheet = Image.new('RGBA', (SHEET_SIZE, SHEET_SIZE), (0, 0, 0, 0))

    # --- Milky glass background panel behind all used rows (0-10) ---
    # Bounding box of used cells:
    #   rows 0-10 (y: 0..704), max col = 8 (row 2/3 zone sub-panel, x: 0..576)
    # Add 8 px padding on each side.
    PAD = 8
    bg_x0 = 0
    bg_y0 = 0
    bg_x1 = 9 * CELL + PAD   # col 0-8 = 576 px + 8 padding = 584
    bg_y1 = 11 * CELL + PAD   # row 0-10 = 704 px + 8 padding = 712
    print(f"  Background panel: ({bg_x0},{bg_y0}) to ({bg_x1},{bg_y1})")
    draw_milkyglass_background(sheet, bg_x0, bg_y0, bg_x1, bg_y1)

    def place(col, row, img):
        """Place a 64x64 icon at the given grid cell."""
        sheet.paste(img, (col * CELL, row * CELL), img)

    # --- Row 0: Toolbar tool-mode icons (active state) ---
    print("  Row 0: Toolbar active icons")
    place(0, 0, draw_zone_toolbar_icon(active=True))
    place(1, 0, draw_road_toolbar_icon(active=True))
    place(2, 0, draw_utilities_toolbar_icon(active=True))
    place(3, 0, draw_demolish_toolbar_icon(active=True))
    place(4, 0, draw_query_toolbar_icon(active=True))

    # --- Row 1: Toolbar tool-mode icons (inactive state) ---
    print("  Row 1: Toolbar inactive icons")
    place(0, 1, draw_zone_toolbar_icon(active=False))
    place(1, 1, draw_road_toolbar_icon(active=False))
    place(2, 1, draw_utilities_toolbar_icon(active=False))
    place(3, 1, draw_demolish_toolbar_icon(active=False))
    place(4, 1, draw_query_toolbar_icon(active=False))

    # --- Row 2: Zone sub-panel (active state) ---
    print("  Row 2: Zone sub-panel active")
    zone_draw_fns = {
        'res_low': draw_res_low, 'res_med': draw_res_med,
        'res_high': draw_res_high,
        'com_low': draw_com_low, 'com_med': draw_com_med,
        'com_high': draw_com_high,
        'ind_low': draw_ind_low, 'ind_med': draw_ind_med,
        'ind_high': draw_ind_high,
    }
    zone_order = [
        'res_low', 'res_med', 'res_high',
        'com_low', 'com_med', 'com_high',
        'ind_low', 'ind_med', 'ind_high',
    ]
    for i, key in enumerate(zone_order):
        place(i, 2, zone_draw_fns[key](active=True))

    # --- Row 3: Zone sub-panel (inactive state) ---
    print("  Row 3: Zone sub-panel inactive")
    for i, key in enumerate(zone_order):
        place(i, 3, zone_draw_fns[key](active=False))

    # --- Row 4: Utilities sub-panel (active state) ---
    print("  Row 4: Utilities sub-panel active")
    place(0, 4, draw_util_power(active=True))
    place(1, 4, draw_util_water(active=True))
    place(2, 4, draw_util_fire(active=True))
    place(3, 4, draw_util_police(active=True))

    # --- Row 5: Utilities sub-panel (inactive state) ---
    print("  Row 5: Utilities sub-panel inactive")
    place(0, 5, draw_util_power(active=False))
    place(1, 5, draw_util_water(active=False))
    place(2, 5, draw_util_fire(active=False))
    place(3, 5, draw_util_police(active=False))

    # --- Row 6: Active tool indicator badges ---
    print("  Row 6: Indicator badges")
    for i, itype in enumerate(['none', 'zone', 'road',
                                'utilities', 'demolish', 'query']):
        place(i, 6, draw_indicator_icon(tool_type=itype))

    # --- Row 7: Cursor shapes (reserved) ---
    print("  Row 7: Cursor shapes")
    for i, ctype in enumerate(['default', 'zone', 'road',
                                'utilities', 'demolish', 'query']):
        place(i, 7, draw_cursor_icon(cursor_type=ctype))

    # --- Row 8: Minimap overlay toggle (active) ---
    print("  Row 8: Minimap toggle active")
    place(0, 8, draw_minimap_toggle(active=True))

    # --- Row 9: Minimap overlay toggle (inactive) ---
    print("  Row 9: Minimap toggle inactive")
    place(0, 9, draw_minimap_toggle(active=False))

    # --- Row 10: Notification / misc ---
    print("  Row 10: Notification / misc icons")
    place(0, 10, draw_notification_bell())
    place(1, 10, draw_clock())
    place(2, 10, draw_unsaved_dot())
    place(3, 10, draw_undo_arrow())

    # Save
    output_path = '/workspace/assets/textures/ui/hud_sprites_ui.png'
    sheet.save(output_path, 'PNG')
    print(f"Saved {output_path} ({sheet.size[0]}x{sheet.size[1]})")

    # Verify
    verify = Image.open(output_path)
    assert verify.size == (2048, 2048), f"Size mismatch: {verify.size}"
    assert verify.mode == 'RGBA', f"Mode mismatch: {verify.mode}"
    print("Verification passed: 2048x2048 RGBA PNG")

    # Export DDS (RGBA8 UNORM, no mips) so the runtime texture stays in sync.
    import subprocess, sys
    script_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.dirname(script_dir)
    export_tool = os.path.join(script_dir, "export_textures.py")
    dds_output = os.path.join(repo_root, "assets", "textures", "ui",
                              "hud_sprites_ui.dds")
    subprocess.run(
        [sys.executable, export_tool,
         "--input",  output_path,
         "--output", dds_output,
         "--format", "rgba8",
         "--overwrite"],
        check=True
    )
    print("DDS exported.")


if __name__ == '__main__':
    main()
