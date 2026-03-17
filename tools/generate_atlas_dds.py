#!/usr/bin/env python3
"""
Generate building atlas DDS files for AI Town Phase 11e.

Outputs:
  assets/textures/buildings/buildings_atlas_d.dds     4096x4096 DXT1 sRGB, 5 mip levels
  assets/textures/buildings/buildings_atlas_d_2k.dds  2048x2048 DXT1 sRGB, 4 mip levels
  assets/textures/buildings/buildings_atlas_d.png     2048x2048 source PNG (Check #28)

DDS format: standard 128-byte header (no DX10 extension), FourCC = "DXT1".
This matches the spec byte-size table in architecture/asset-standards/2d-texture-standards.md.

Expected sizes:
  buildings_atlas_d.dds    = 11,174,016 bytes  (128 header + 11,173,888 data)
  buildings_atlas_d_2k.dds =  2,785,408 bytes  (128 header +  2,785,280 data)
"""

import struct
import os
import random
import math


# ---------------------------------------------------------------------------
# Atlas constants
# ---------------------------------------------------------------------------
GRID_COLS = 8
GRID_ROWS = 8
CELL_SIZE_4K = 512
CELL_SIZE_2K = 256
CELL_BORDER = 8


# ---------------------------------------------------------------------------
# Utility drawing functions (operate on a flat pixel buffer)
# ---------------------------------------------------------------------------

def _clamp(v, lo=0, hi=255):
    return max(lo, min(hi, int(v)))


def _fill_rect(buf, W, x0, y0, x1, y1, r, g, b):
    """Fill a rectangle [x0,x1) x [y0,y1) in buf (list of (r,g,b))."""
    for y in range(max(0, y0), min(y1, len(buf) // W)):
        for x in range(max(0, x0), min(x1, W)):
            buf[y * W + x] = (r, g, b)


def _fill_rect_blend(buf, W, H, x0, y0, x1, y1, r, g, b, alpha):
    """Fill a rectangle with alpha blending over existing content."""
    a = alpha
    inv = 1.0 - a
    for y in range(max(0, y0), min(y1, H)):
        for x in range(max(0, x0), min(x1, W)):
            idx = y * W + x
            pr, pg, pb = buf[idx]
            buf[idx] = (_clamp(pr * inv + r * a),
                        _clamp(pg * inv + g * a),
                        _clamp(pb * inv + b * a))


def _hline(buf, W, H, y, x0, x1, r, g, b):
    if 0 <= y < H:
        for x in range(max(0, x0), min(x1, W)):
            buf[y * W + x] = (r, g, b)


def _vline(buf, W, H, x, y0, y1, r, g, b):
    if 0 <= x < W:
        for y in range(max(0, y0), min(y1, H)):
            buf[y * W + x] = (r, g, b)


def _draw_window(buf, W, H, wx, wy, ww, wh, glass_rgb, frame_rgb=None, frame_w=2):
    """Draw a window with optional frame."""
    if frame_rgb:
        _fill_rect(buf, W, wx - frame_w, wy - frame_w,
                   wx + ww + frame_w, wy + wh + frame_w,
                   *frame_rgb)
    _fill_rect(buf, W, wx, wy, wx + ww, wy + wh, *glass_rgb)


def _add_noise(buf, W, H, intensity=8, density=0.3, seed=42):
    """Add subtle per-pixel noise to the buffer."""
    rng = random.Random(seed)
    for i in range(W * H):
        if rng.random() < density:
            r, g, b = buf[i]
            d = rng.randint(-intensity, intensity)
            buf[i] = (_clamp(r + d), _clamp(g + d), _clamp(b + d))


def _brick_coursing(buf, W, H, y0, y1, course_h, joint_color, brick_offset=True, seed=100):
    """Draw horizontal brick course lines with staggered vertical joints."""
    rng = random.Random(seed)
    jr, jg, jb = joint_color
    for y in range(y0, min(y1, H)):
        row_in_brick = (y - y0) % course_h
        if row_in_brick == 0:
            _hline(buf, W, H, y, 0, W, jr, jg, jb)
        elif brick_offset and row_in_brick == course_h // 2:
            # Subtle stagger marks at half-brick positions
            course_idx = (y - y0) // course_h
            offset = (course_h * 3) if (course_idx % 2 == 0) else 0
            for x in range(offset, W, course_h * 6):
                if 0 <= x < W:
                    buf[y * W + x] = (jr, jg, jb)


def _corrugation_lines(buf, W, H, y0, y1, spacing, base_rgb, amplitude=15):
    """Draw horizontal corrugation effect (alternating lighter/darker bands)."""
    br, bg, bb = base_rgb
    for y in range(max(0, y0), min(y1, H)):
        phase = ((y - y0) % spacing) / max(1, spacing - 1)
        offset = int(amplitude * math.sin(phase * math.pi * 2))
        for x in range(W):
            r, g, b = buf[y * W + x]
            buf[y * W + x] = (_clamp(r + offset), _clamp(g + offset), _clamp(b + offset))


def _draw_window_grid(buf, W, H, cols, rows, win_w, win_h, glass_rgb,
                      start_x=None, start_y=None, spacing_x=None, spacing_y=None,
                      frame_rgb=None, frame_w=2):
    """Draw a regular grid of windows."""
    if spacing_x is None:
        spacing_x = (W - 2 * 20) // max(1, cols)
    if spacing_y is None:
        spacing_y = (H - 80) // max(1, rows)
    if start_x is None:
        total_w = cols * win_w + (cols - 1) * (spacing_x - win_w)
        start_x = (W - total_w) // 2
    if start_y is None:
        start_y = 60
    for r in range(rows):
        for c in range(cols):
            wx = start_x + c * spacing_x
            wy = start_y + r * spacing_y
            _draw_window(buf, W, H, wx, wy, win_w, win_h, glass_rgb,
                         frame_rgb=frame_rgb, frame_w=frame_w)


# ---------------------------------------------------------------------------
# Per-cell texture drawing functions
# Each function takes (buf, W, H) and draws into buf (list of W*H rgb tuples).
# W and H are the cell dimensions (e.g. 512x512 for 4K atlas).
# ---------------------------------------------------------------------------

GLASS_DARK = (32, 35, 42)
GLASS_BLUE = (42, 55, 78)


def draw_res_low_01(buf, W, H):
    """Detached house, red brick."""
    base = (165, 70, 48)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    _brick_coursing(buf, W, H, 0, H, max(2, H // 64), (130, 52, 35), seed=101)
    _add_noise(buf, W, H, intensity=6, density=0.25, seed=1001)
    # Windows: 3 cols x 2 rows
    ww, wh = W * 60 // 512, H * 70 // 512
    sp_x = W // 4
    sp_y = H // 4
    for r in range(2):
        for c in range(3):
            wx = W // 6 + c * sp_x
            wy = H // 6 + r * sp_y
            _draw_window(buf, W, H, wx, wy, ww, wh, (48, 50, 55),
                         frame_rgb=(85, 80, 75), frame_w=max(1, W // 256))
    # Door
    dw, dh = W * 80 // 512, H * 90 // 512
    dx = (W - dw) // 2
    dy = H - dh - H // 10
    _fill_rect(buf, W, dx, dy, dx + dw, dy + dh, 90, 55, 35)
    _fill_rect(buf, W, dx + 2, dy + 2, dx + dw - 2, dy + dh - 2, 72, 42, 28)


def draw_res_low_02(buf, W, H):
    """Semi-detached, cream render."""
    base = (218, 208, 182)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Render band joints every 32px scaled
    sp = max(2, H * 32 // 512)
    for y in range(0, H, sp):
        _hline(buf, W, H, y, 0, W, 205, 195, 170)
    # String course
    sc_y = H * 170 // 512
    for dy in range(max(1, H * 4 // 512)):
        _hline(buf, W, H, sc_y + dy, 0, W, 180, 170, 148)
    # Windows 4x3
    ww, wh = W * 50 // 512, H * 65 // 512
    _draw_window_grid(buf, W, H, 4, 3, ww, wh, GLASS_DARK,
                      frame_rgb=(235, 230, 220), frame_w=max(1, W // 256))
    _add_noise(buf, W, H, intensity=5, density=0.2, seed=1002)


def draw_res_low_03(buf, W, H):
    """Terraced, tan sandstone."""
    base = (190, 165, 115)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Stone block coursing
    ch = max(2, H * 24 // 512)
    jw = max(2, W * 48 // 512)
    joint_clr = (155, 132, 88)
    for y in range(0, H, ch):
        _hline(buf, W, H, y, 0, W, *joint_clr)
        row_idx = y // ch
        offset = (jw // 2) if (row_idx % 2 == 1) else 0
        for x in range(offset, W, jw):
            _vline(buf, W, H, x, y, min(y + ch, H), *joint_clr)
    # Windows 2x3
    ww, wh = W * 55 // 512, H * 75 // 512
    _draw_window_grid(buf, W, H, 2, 3, ww, wh, GLASS_DARK,
                      frame_rgb=(170, 148, 100), frame_w=max(1, W // 256))
    # Bay window
    bx = W * 120 // 512
    by = H * 300 // 512
    bw = W * 180 // 512
    bh = H * 100 // 512
    _fill_rect(buf, W, bx, by, bx + bw, by + bh, 175, 150, 105)
    for i in range(3):
        sx = bx + 10 + i * (bw // 3)
        _draw_window(buf, W, H, sx, by + 8, bw // 4, bh - 16, GLASS_DARK)
    # Stone sill bands
    for r in range(3):
        sy = H // 6 + r * (H // 4) + wh + 2
        for dy in range(max(1, H * 3 // 512)):
            _hline(buf, W, H, sy + dy, 0, W, 165, 140, 95)
    _add_noise(buf, W, H, intensity=5, density=0.2, seed=1003)


def draw_res_low_04(buf, W, H):
    """Bungalow, grey pebbledash."""
    base = (152, 150, 146)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    _add_noise(buf, W, H, intensity=8, density=0.45, seed=1004)
    # Bargeboards (diagonal hatching at top)
    bh = H * 80 // 512
    for y in range(bh):
        for x in range(W):
            if (x + y) % (max(2, W // 32)) < max(1, W // 64):
                idx = y * W + x
                r, g, b = buf[idx]
                buf[idx] = (_clamp(r - 20), _clamp(g - 20), _clamp(b - 20))
    # Wide shallow windows: 3 horizontal strips
    ww, wh = W * 90 // 512, H * 45 // 512
    for i in range(3):
        wx = W // 8 + i * (W // 3)
        wy = H * 200 // 512
        _draw_window(buf, W, H, wx, wy, ww, wh, GLASS_DARK,
                     frame_rgb=(175, 172, 168), frame_w=max(1, W // 256))
    # Ground floor dominates (single storey so door area)
    dw = W * 70 // 512
    dh = H * 100 // 512
    dx = (W - dw) // 2
    dy = H - dh - H // 12
    _fill_rect(buf, W, dx, dy, dx + dw, dy + dh, 120, 82, 55)


def draw_res_med_01(buf, W, H):
    """Walk-up apartments, buff brick."""
    base = (198, 174, 118)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    _brick_coursing(buf, W, H, 0, H, max(2, H * 10 // 512), (172, 148, 92), seed=201)
    # Concrete floor bands
    fb_h = max(1, H * 10 // 512)
    sp = max(2, H * 96 // 512)
    for y in range(sp, H, sp):
        _fill_rect(buf, W, 0, y, W, y + fb_h, 175, 175, 170)
    # Windows 4x5
    ww, wh = W * 45 // 512, H * 60 // 512
    _draw_window_grid(buf, W, H, 4, 5, ww, wh, GLASS_DARK,
                      frame_rgb=(230, 225, 218), frame_w=max(1, W // 256))
    # Entrance band
    ew = W // 5
    _fill_rect(buf, W, (W - ew) // 2, H * 400 // 512, (W + ew) // 2, H, 165, 155, 130)
    _add_noise(buf, W, H, intensity=5, density=0.2, seed=1201)


def draw_res_med_02(buf, W, H):
    """Walk-up apartments, white render + balcony stripe."""
    base = (235, 235, 230)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    sp = max(2, H * 20 // 512)
    for y in range(0, H, sp):
        _hline(buf, W, H, y, 0, W, 222, 222, 218)
    # Windows 4x5
    ww, wh = W * 45 // 512, H * 55 // 512
    sp_y = H * 96 // 512
    for r in range(5):
        wy = H // 10 + r * sp_y
        # Balcony recess on even floors
        if r % 2 == 0:
            bal_y = wy + wh + 2
            bal_h = max(1, H * 30 // 512)
            _fill_rect(buf, W, 0, bal_y, W, bal_y + bal_h, 180, 180, 178)
            # Guard rail line
            _hline(buf, W, H, bal_y + bal_h - 1, 0, W, 100, 100, 98)
        for c in range(4):
            wx = W // 8 + c * (W * 100 // 512)
            _draw_window(buf, W, H, wx, wy, ww, wh, GLASS_DARK,
                         frame_rgb=(210, 210, 208), frame_w=max(1, W // 256))
    _add_noise(buf, W, H, intensity=4, density=0.15, seed=1202)


def draw_res_med_03(buf, W, H):
    """Walk-up, terracotta tile panels."""
    base = (188, 98, 60)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Tile grid
    th = max(2, H * 40 // 512)
    tv = max(2, W * 20 // 512)
    for y in range(0, H, th):
        _hline(buf, W, H, y, 0, W, 155, 75, 42)
    for x in range(0, W, tv):
        _vline(buf, W, H, x, 0, H, 155, 75, 42)
    # Spandrel bands between floors
    sp_h = max(1, H * 18 // 512)
    sp_sp = max(2, H * 100 // 512)
    for y in range(sp_sp, H, sp_sp):
        _fill_rect(buf, W, 0, y, W, y + sp_h, 80, 80, 78)
    # Windows 3x5
    ww, wh = W * 50 // 512, H * 60 // 512
    _draw_window_grid(buf, W, H, 3, 5, ww, wh, GLASS_DARK,
                      frame_rgb=(150, 85, 50), frame_w=max(1, W // 256))
    _add_noise(buf, W, H, intensity=5, density=0.2, seed=1203)


def draw_res_med_04(buf, W, H):
    """Walk-up, pale render + green spandrel."""
    base = (208, 212, 198)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Floor lines
    sp = max(2, H * 100 // 512)
    for y in range(sp, H, sp):
        _hline(buf, W, H, y, 0, W, 165, 168, 155)
    # Windows 4x5 with green spandrel below each
    ww, wh = W * 45 // 512, H * 55 // 512
    green_h = max(1, H * 35 // 512)
    sp_y = max(2, H * 95 // 512)
    for r in range(5):
        for c in range(4):
            wx = W // 8 + c * (W * 110 // 512)
            wy = H // 12 + r * sp_y
            _draw_window(buf, W, H, wx, wy, ww, wh, GLASS_DARK)
            # Green spandrel below
            _fill_rect(buf, W, wx, wy + wh, wx + ww, wy + wh + green_h, 95, 140, 95)
    # Ground floor taller openings
    gh = H * 80 // 512
    for c in range(4):
        wx = W // 8 + c * (W * 110 // 512)
        wy = H - gh - H // 20
        _draw_window(buf, W, H, wx, wy, ww, gh, GLASS_DARK)
    _add_noise(buf, W, H, intensity=4, density=0.15, seed=1204)


def draw_res_high_01(buf, W, H):
    """Tower block, blue-grey curtain wall."""
    base = (110, 138, 165)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    grid_clr = (55, 70, 85)
    fh = max(2, H * 48 // 512)
    cv = max(2, W * 64 // 512)
    lw = max(1, W * 2 // 512)
    # Horizontal floor lines
    for y in range(0, H, fh):
        for dy in range(lw):
            _hline(buf, W, H, y + dy, 0, W, *grid_clr)
    # Vertical column lines
    for x in range(0, W, cv):
        for dx in range(lw):
            _vline(buf, W, H, x + dx, 0, H, *grid_clr)
    # Window fills slightly lighter
    for y in range(0, H, fh):
        for x in range(0, W, cv):
            wy0 = y + lw
            wy1 = min(y + fh - lw, H)
            wx0 = x + lw
            wx1 = min(x + cv - lw, W)
            _fill_rect(buf, W, wx0, wy0, wx1, wy1, 125, 155, 185)
    # Spandrel bands
    sp_h = max(1, H * 18 // 512)
    for y in range(fh - sp_h, H, fh):
        _fill_rect(buf, W, 0, y, W, y + sp_h, 72, 95, 118)
    _add_noise(buf, W, H, intensity=4, density=0.15, seed=1301)


def draw_res_high_02(buf, W, H):
    """Tower block, warm beige precast panels."""
    base = (200, 185, 155)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Panel joints
    pv = max(2, W * 80 // 512)
    ph = max(2, H * 96 // 512)
    jw = max(1, W * 3 // 512)
    for x in range(0, W, pv):
        for dx in range(jw):
            _vline(buf, W, H, x + dx, 0, H, 110, 95, 68)
    for y in range(0, H, ph):
        for dy in range(jw):
            _hline(buf, W, H, y + dy, 0, W, 110, 95, 68)
    # Windows 3x8
    ww, wh = W * 65 // 512, H * 55 // 512
    _draw_window_grid(buf, W, H, 3, 8, ww, wh, GLASS_DARK,
                      frame_rgb=(160, 145, 118), frame_w=max(1, W * 4 // 512))
    _add_noise(buf, W, H, intensity=4, density=0.2, seed=1302)


def draw_res_high_03(buf, W, H):
    """Tower block, white + horizontal bands."""
    base = (238, 238, 235)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Bold horizontal bands every 96px
    bh = max(2, H * 20 // 512)
    sp = max(2, H * 96 // 512)
    for y in range(sp, H, sp):
        _fill_rect(buf, W, 0, y, W, y + bh, 170, 170, 168)
        # Thin dark line above and below
        _hline(buf, W, H, y - 1, 0, W, 130, 130, 128)
        _hline(buf, W, H, y + bh, 0, W, 130, 130, 128)
    # Windows 5x8
    ww, wh = W * 48 // 512, H * 52 // 512
    _draw_window_grid(buf, W, H, 5, 8, ww, wh, (150, 165, 185),
                      frame_rgb=(200, 200, 198), frame_w=max(1, W // 256))
    _add_noise(buf, W, H, intensity=3, density=0.12, seed=1303)


def draw_res_high_04(buf, W, H):
    """Tower block, amber glass + concrete."""
    base = (190, 150, 72)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Concrete column strips
    cw = max(2, W * 20 // 512)
    cv = max(2, W * 128 // 512)
    for x in range(0, W, cv):
        _fill_rect(buf, W, x, 0, x + cw, H, 120, 115, 108)
    # Floor bands
    fb_h = max(1, H * 12 // 512)
    sp = max(2, H * 96 // 512)
    for y in range(sp, H, sp):
        _fill_rect(buf, W, 0, y, W, y + fb_h, 120, 115, 108)
    # Glass fill between columns
    for y in range(0, H, sp):
        for x in range(cw, W, cv):
            gx0 = x
            gx1 = min(x + cv - cw, W)
            gy0 = y + fb_h
            gy1 = min(y + sp, H)
            _fill_rect(buf, W, gx0, gy0, gx1, gy1, 195, 158, 82)
    _add_noise(buf, W, H, intensity=5, density=0.2, seed=1304)


def draw_com_low_01(buf, W, H):
    """Retail unit, red shopfront."""
    # Upper: brick
    base = (180, 80, 55)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    _brick_coursing(buf, W, H, 0, H * 65 // 100, max(2, H * 10 // 512), (148, 62, 40), seed=401)
    # Ground floor shopfront (bottom 30%)
    sf_y = H * 70 // 100
    _fill_rect(buf, W, 0, sf_y, W, H, 200, 35, 35)
    # Fascia band
    fas_h = max(2, H * 40 // 512)
    _fill_rect(buf, W, 0, sf_y, W, sf_y + fas_h, 245, 245, 245)
    # Large glazed panel
    gw = W * 70 // 100
    gh = (H - sf_y - fas_h) * 55 // 100
    gx = (W - gw) // 2
    gy = sf_y + fas_h + 4
    _fill_rect(buf, W, gx, gy, gx + gw, gy + gh, 38, 38, 42)
    # Upper windows 3x2
    ww, wh = W * 55 // 512, H * 50 // 512
    _draw_window_grid(buf, W, H, 3, 2, ww, wh, GLASS_DARK,
                      start_y=H * 60 // 512,
                      spacing_y=H * 120 // 512,
                      frame_rgb=(160, 65, 42), frame_w=max(1, W // 256))
    _add_noise(buf, W, H, intensity=5, density=0.2, seed=1401)


def draw_com_low_02(buf, W, H):
    """Commercial, blue-grey cladding."""
    base = (105, 125, 148)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Cladding panel joints
    ph = max(2, H * 80 // 512)
    pv = max(2, W * 64 // 512)
    for y in range(0, H, ph):
        _hline(buf, W, H, y, 0, W, 60, 75, 95)
        _hline(buf, W, H, y + 1, 0, W, 60, 75, 95)
    for x in range(0, W, pv):
        _vline(buf, W, H, x, 0, H, 60, 75, 95)
        _vline(buf, W, H, x + 1, 0, H, 60, 75, 95)
    # Windows 4x4
    ww, wh = W * 50 // 512, H * 55 // 512
    _draw_window_grid(buf, W, H, 4, 4, ww, wh, GLASS_DARK)
    # Ground floor: large frameless glazing
    gw = W * 80 // 100
    gh = H * 25 // 100
    gx = (W - gw) // 2
    gy = H - gh - H // 20
    _fill_rect(buf, W, gx, gy, gx + gw, gy + gh, 35, 38, 45)
    _fill_rect(buf, W, gx - 3, gy - 3, gx + gw + 3, gy, 58, 62, 72)
    _add_noise(buf, W, H, intensity=4, density=0.15, seed=1402)


def draw_com_low_03(buf, W, H):
    """Corner commercial, brick + signage band."""
    # Lower: dark red-brown brick
    base = (158, 65, 48)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    _brick_coursing(buf, W, H, 0, H * 60 // 100, max(2, H * 12 // 512), (128, 48, 32), seed=403)
    # Upper 40%: buff brick
    uy = H * 60 // 100
    _fill_rect(buf, W, 0, 0, W, uy - H * 40 // 100, 185, 160, 112)
    _brick_coursing(buf, W, H, 0, uy - H * 40 // 100, max(2, H * 12 // 512), (158, 135, 88), seed=404)
    # String course at 60% height
    sc_y = H * 60 // 100
    sc_h = max(2, H * 25 // 512)
    _fill_rect(buf, W, 0, sc_y, W, sc_y + sc_h, 218, 198, 155)
    # Windows 3x3
    ww, wh = W * 55 // 512, H * 65 // 512
    _draw_window_grid(buf, W, H, 3, 3, ww, wh, GLASS_DARK,
                      frame_rgb=(138, 55, 38), frame_w=max(1, W // 256))
    # Ground floor: large shop glazing (3 bays)
    bay_w = W * 28 // 100
    bay_h = H * 25 // 100
    bay_y = H - bay_h - H // 20
    for i in range(3):
        bx = W // 10 + i * (W * 30 // 100)
        _fill_rect(buf, W, bx, bay_y, bx + bay_w, bay_y + bay_h, 32, 32, 38)
    _add_noise(buf, W, H, intensity=5, density=0.2, seed=1403)


def draw_com_low_04(buf, W, H):
    """Small market hall, white render + green fascia."""
    base = (232, 232, 228)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Plaster lines
    sp = max(2, H * 32 // 512)
    for y in range(0, H, sp):
        _hline(buf, W, H, y, 0, W, 218, 218, 215)
    # Green fascia top 20%
    fh = H * 20 // 100
    _fill_rect(buf, W, 0, 0, W, fh, 45, 105, 58)
    # 4 arched windows
    aw = W * 60 // 512
    ah = H * 100 // 512
    for i in range(4):
        ax = W // 10 + i * (W * 22 // 100)
        ay = fh + H * 40 // 512
        _fill_rect(buf, W, ax, ay, ax + aw, ay + ah, 42, 45, 52)
        # Arch top (semicircle approximation with rect + trapezoid)
        arc_h = max(2, ah // 4)
        _fill_rect(buf, W, ax + aw // 6, ay - arc_h, ax + aw - aw // 6, ay, 42, 45, 52)
        # Keystone
        _fill_rect(buf, W, ax + aw // 2 - 2, ay - arc_h - 3, ax + aw // 2 + 2, ay - arc_h, 200, 195, 185)
    _add_noise(buf, W, H, intensity=4, density=0.15, seed=1404)


def draw_com_med_01(buf, W, H):
    """Office block, silver aluminium cladding."""
    base = (185, 190, 195)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    fh = max(2, H * 64 // 512)
    cv = max(2, W * 96 // 512)
    lw = max(1, W * 2 // 512)
    # Grid
    for y in range(0, H, fh):
        for dy in range(lw):
            _hline(buf, W, H, y + dy, 0, W, 90, 95, 100)
    for x in range(0, W, cv):
        for dx in range(lw):
            _vline(buf, W, H, x + dx, 0, H, 90, 95, 100)
    # Window infill
    for y in range(0, H, fh):
        for x in range(0, W, cv):
            _fill_rect(buf, W, x + lw + 2, y + lw + 2,
                       min(x + cv - lw - 2, W), min(y + fh - lw - 2, H),
                       145, 155, 165)
    # Spandrel panels
    sp_h = max(1, H * 22 // 512)
    for y in range(fh - sp_h, H, fh):
        _fill_rect(buf, W, 0, y, W, y + sp_h, 200, 205, 210)
    # Column reveals
    rl = max(1, W * 4 // 512)
    for x in range(0, W, cv):
        _fill_rect(buf, W, x + lw, 0, x + lw + rl, H, 120, 125, 130)
    _add_noise(buf, W, H, intensity=3, density=0.1, seed=1501)


def draw_com_med_02(buf, W, H):
    """Office block, dark blue reflective glass."""
    base = (28, 52, 100)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Subtle vertical gradient
    for y in range(H):
        frac = y / max(1, H - 1)
        lighten = int(12 * (1.0 - frac))
        for x in range(W):
            r, g, b = buf[y * W + x]
            buf[y * W + x] = (_clamp(r + lighten), _clamp(g + lighten), _clamp(b + lighten))
    # Grid
    fh = max(2, H * 64 // 512)
    cv = max(2, W * 96 // 512)
    for y in range(0, H, fh):
        _hline(buf, W, H, y, 0, W, 18, 32, 65)
        _hline(buf, W, H, y + 1, 0, W, 18, 32, 65)
    for x in range(0, W, cv):
        _vline(buf, W, H, x, 0, H, 18, 32, 65)
        _vline(buf, W, H, x + 1, 0, H, 18, 32, 65)
    # Window fills slightly lighter
    for y in range(0, H, fh):
        for x in range(0, W, cv):
            _fill_rect(buf, W, x + 3, y + 3,
                       min(x + cv - 3, W), min(y + fh - 3, H),
                       40, 72, 135)
    # Column bands
    cb_w = max(1, W * 20 // 512)
    for x in range(0, W, cv):
        _fill_rect(buf, W, x, 0, x + cb_w, H, 18, 32, 65)
    _add_noise(buf, W, H, intensity=3, density=0.1, seed=1502)


def draw_com_med_03(buf, W, H):
    """Office block, cream stone facade."""
    base = (215, 205, 180)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Rusticated base (lower 25%)
    rb = H * 25 // 100
    _fill_rect(buf, W, 0, H - rb, W, H, 200, 190, 165)
    rsp = max(2, H * 32 // 512)
    for y in range(H - rb, H, rsp):
        _hline(buf, W, H, y, 0, W, 165, 155, 130)
        _hline(buf, W, H, y + 1, 0, W, 165, 155, 130)
    # Pilaster rhythm
    pv = max(2, W * 96 // 512)
    pw = max(1, W * 8 // 512)
    for x in range(0, W, pv):
        _fill_rect(buf, W, x, 0, x + pw, H, 225, 215, 190)
    # Windows 3x6
    ww, wh = W * 55 // 512, H * 70 // 512
    _draw_window_grid(buf, W, H, 3, 6, ww, wh, GLASS_DARK,
                      frame_rgb=(225, 218, 195), frame_w=max(1, W * 3 // 512))
    # Cornice line at 85%
    cy = H * 85 // 100
    ch = max(2, H * 20 // 512)
    _fill_rect(buf, W, 0, cy, W, cy + ch, 228, 218, 195)
    _hline(buf, W, H, cy, 0, W, 185, 175, 150)
    _hline(buf, W, H, cy + ch, 0, W, 185, 175, 150)
    _add_noise(buf, W, H, intensity=4, density=0.15, seed=1503)


def draw_com_med_04(buf, W, H):
    """Office block, bronze-tinted curtain wall."""
    base = (145, 105, 52)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Mullion grid
    fh = max(2, H * 64 // 512)
    cv = max(2, W * 96 // 512)
    lw = max(1, W * 3 // 512)
    for y in range(0, H, fh):
        for dy in range(lw):
            _hline(buf, W, H, y + dy, 0, W, 80, 55, 22)
    for x in range(0, W, cv):
        for dx in range(lw):
            _vline(buf, W, H, x + dx, 0, H, 80, 55, 22)
    # Spandrel
    sp_h = max(1, H * 20 // 512)
    for y in range(fh - sp_h, H, fh):
        _fill_rect(buf, W, 0, y, W, y + sp_h, 165, 120, 60)
    # Window fills
    for y in range(0, H, fh):
        for x in range(0, W, cv):
            _fill_rect(buf, W, x + lw + 1, y + lw + 1,
                       min(x + cv - lw - 1, W), min(y + fh - sp_h - 1, H),
                       160, 120, 65)
    # Corner fins
    fw = max(1, W * 8 // 512)
    _fill_rect(buf, W, 0, 0, fw, H, 100, 72, 30)
    _fill_rect(buf, W, W - fw, 0, W, H, 100, 72, 30)
    _add_noise(buf, W, H, intensity=4, density=0.15, seed=1504)


def draw_com_high_01(buf, W, H):
    """Skyscraper, deep blue mirror glass."""
    base = (22, 42, 92)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    fh = max(2, H * 48 // 512)
    cv = max(2, W * 72 // 512)
    # Grid
    for y in range(0, H, fh):
        _hline(buf, W, H, y, 0, W, 12, 22, 55)
        _hline(buf, W, H, y + 1, 0, W, 12, 22, 55)
    for x in range(0, W, cv):
        _vline(buf, W, H, x, 0, H, 12, 22, 55)
        _vline(buf, W, H, x + 1, 0, H, 12, 22, 55)
    # 3-tone window fills
    colors = [(28, 50, 110), (40, 70, 148), (35, 58, 125)]
    ci = 0
    for y in range(0, H, fh):
        for x in range(0, W, cv):
            c = colors[ci % 3]
            _fill_rect(buf, W, x + 2, y + 2, min(x + cv - 2, W), min(y + fh - 2, H), *c)
            ci += 1
    # Reflection streak (faint diagonal lighter band)
    streak_w = W * 60 // 512
    for y in range(H):
        sx = (y * 2 // 3) % W
        for dx in range(streak_w):
            px = sx + dx
            if 0 <= px < W:
                idx = y * W + px
                r, g, b = buf[idx]
                buf[idx] = (_clamp(r + 8), _clamp(g + 12), _clamp(b + 18))
    # Mechanical penthouse top 10%
    mh = H * 10 // 100
    _fill_rect(buf, W, 0, 0, W, mh, 80, 85, 95)
    _add_noise(buf, W, H, intensity=3, density=0.1, seed=1601)


def draw_com_high_02(buf, W, H):
    """Skyscraper, silver steel + clear glass."""
    base = (195, 202, 210)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Steel column lines
    cv = max(2, W * 96 // 512)
    cw = max(1, W * 4 // 512)
    for x in range(0, W, cv):
        for dx in range(cw):
            _vline(buf, W, H, x + dx, 0, H, 100, 105, 112)
    # Floor bands
    fh = max(2, H * 48 // 512)
    fb_h = max(1, H * 16 // 512)
    for y in range(0, H, fh):
        _fill_rect(buf, W, 0, y, W, y + fb_h, 160, 165, 170)
    # Clear glass between columns
    for y in range(0, H, fh):
        for x in range(cw, W, cv):
            _fill_rect(buf, W, x, y + fb_h, min(x + cv - cw, W), min(y + fh, H),
                       165, 185, 215)
    # Mechanical top 8%
    mh = H * 8 // 100
    _fill_rect(buf, W, 0, 0, W, mh, 130, 135, 140)
    _add_noise(buf, W, H, intensity=3, density=0.1, seed=1602)


def draw_com_high_03(buf, W, H):
    """Skyscraper, green-tinted glass."""
    base = (45, 115, 80)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    fh = max(2, H * 48 // 512)
    cv = max(2, W * 72 // 512)
    # Grid
    for y in range(0, H, fh):
        _hline(buf, W, H, y, 0, W, 22, 65, 42)
        _hline(buf, W, H, y + 1, 0, W, 22, 65, 42)
    for x in range(0, W, cv):
        _vline(buf, W, H, x, 0, H, 22, 65, 42)
        _vline(buf, W, H, x + 1, 0, H, 22, 65, 42)
    # Spandrel
    sp_h = max(1, H * 16 // 512)
    for y in range(fh - sp_h, H, fh):
        _fill_rect(buf, W, 0, y, W, y + sp_h, 55, 130, 90)
    # Window fills
    for y in range(0, H, fh):
        for x in range(0, W, cv):
            _fill_rect(buf, W, x + 2, y + 2,
                       min(x + cv - 2, W), min(y + fh - sp_h - 1, H),
                       65, 145, 105)
    # Shimmer band
    shimmer_period = max(2, H * 200 // 512)
    for y in range(0, H, shimmer_period):
        for dy in range(max(1, H * 8 // 512)):
            if y + dy < H:
                for x in range(W):
                    idx = (y + dy) * W + x
                    r, g, b = buf[idx]
                    buf[idx] = (_clamp(r + 6), _clamp(g + 8), _clamp(b + 5))
    # Crown
    mh = H * 12 // 100
    _fill_rect(buf, W, W // 10, 0, W - W // 10, mh, 38, 95, 65)
    _add_noise(buf, W, H, intensity=3, density=0.1, seed=1603)


def draw_com_high_04(buf, W, H):
    """Skyscraper, gold reflective glass."""
    base = (188, 155, 42)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    fh = max(2, H * 48 // 512)
    cv = max(2, W * 80 // 512)
    # Grid
    for y in range(0, H, fh):
        _hline(buf, W, H, y, 0, W, 108, 82, 18)
        _hline(buf, W, H, y + 1, 0, W, 108, 82, 18)
    for x in range(0, W, cv):
        _vline(buf, W, H, x, 0, H, 108, 82, 18)
        _vline(buf, W, H, x + 1, 0, H, 108, 82, 18)
    # Spandrel
    sp_h = max(1, H * 18 // 512)
    for y in range(fh - sp_h, H, fh):
        _fill_rect(buf, W, 0, y, W, y + sp_h, 205, 170, 52)
    # Window fills
    for y in range(0, H, fh):
        for x in range(0, W, cv):
            _fill_rect(buf, W, x + 2, y + 2,
                       min(x + cv - 2, W), min(y + fh - sp_h - 1, H),
                       175, 142, 38)
    # Corner bands
    cbw = max(1, W * 20 // 512)
    _fill_rect(buf, W, 0, 0, cbw, H, 138, 108, 28)
    _fill_rect(buf, W, W - cbw, 0, W, H, 138, 108, 28)
    # Crown feature top 15%
    mh = H * 15 // 100
    _fill_rect(buf, W, W // 8, 0, W - W // 8, mh, 158, 128, 35)
    _add_noise(buf, W, H, intensity=3, density=0.1, seed=1604)


def draw_ind_low_01(buf, W, H):
    """Industrial shed, corrugated steel grey."""
    base = (145, 150, 155)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    _corrugation_lines(buf, W, H, 0, H, max(2, H * 12 // 512), base, amplitude=15)
    # Roller door
    dw = W * 60 // 100
    dh = H * 55 // 100
    dx = (W - dw) // 2
    dy = H - dh
    _fill_rect(buf, W, dx, dy, dx + dw, dy + dh, 55, 55, 58)
    # Ribs
    rib_sp = max(2, H * 20 // 512)
    for y in range(dy, dy + dh, rib_sp):
        _hline(buf, W, H, y, dx, dx + dw, 68, 68, 72)
    # Gable shadow
    for y in range(H * 15 // 100):
        frac = y / max(1, H * 15 // 100)
        dark = int(25 * (1.0 - frac))
        for x in range(W):
            idx = y * W + x
            r, g, b = buf[idx]
            buf[idx] = (_clamp(r - dark), _clamp(g - dark), _clamp(b - dark))
    _add_noise(buf, W, H, intensity=5, density=0.2, seed=1701)


def draw_ind_low_02(buf, W, H):
    """Industrial shed, red/rust metal cladding."""
    base = (162, 62, 35)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    _corrugation_lines(buf, W, H, 0, H, max(2, H * 10 // 512), base, amplitude=12)
    # Rust patches
    rng = random.Random(1702)
    for _ in range(8):
        px = rng.randint(0, W - W * 40 // 512)
        py = rng.randint(0, H - H * 40 // 512)
        pw = W * 40 // 512
        ph = H * 40 // 512
        _fill_rect_blend(buf, W, H, px, py, px + pw, py + ph, 120, 42, 20, 0.6)
    # Loading bay
    lw = W * 40 // 100
    lh = H * 50 // 100
    _fill_rect(buf, W, 0, H - lh, lw, H, 32, 28, 25)
    # Bolted joint line at 55%
    jy = H * 55 // 100
    _hline(buf, W, H, jy, 0, W, 115, 42, 22)
    _hline(buf, W, H, jy + 1, 0, W, 115, 42, 22)
    _hline(buf, W, H, jy + 2, 0, W, 115, 42, 22)
    _hline(buf, W, H, jy - 1, 0, W, 182, 78, 52)
    _add_noise(buf, W, H, intensity=6, density=0.25, seed=1702)


def draw_ind_low_03(buf, W, H):
    """Saw-tooth industrial, beige brick."""
    base = (192, 176, 135)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    _brick_coursing(buf, W, H, 0, H, max(2, H * 14 // 512), (162, 146, 105), seed=703)
    # Saw-tooth clerestory at top 25%
    st_h = H * 25 // 100
    sw = W * 128 // 512
    for i in range(3):
        sx = W // 8 + i * (W * 140 // 512)
        # Triangle lighter area
        for y in range(st_h):
            frac = y / max(1, st_h)
            lw2 = int(sw * frac)
            x0 = sx + (sw - lw2) // 2
            x1 = x0 + lw2
            for x in range(max(0, x0), min(x1, W)):
                idx = y * W + x
                r, g, b = buf[idx]
                buf[idx] = (_clamp(r + 18), _clamp(g + 19), _clamp(b + 15))
    # Pilasters
    pv = max(2, W * 128 // 512)
    pw = max(1, W * 8 // 512)
    for x in range(0, W, pv):
        _fill_rect(buf, W, x, 0, x + pw, H, 165, 148, 108)
    # Concrete dado lower 15%
    dh = H * 15 // 100
    _fill_rect(buf, W, 0, H - dh, W, H, 165, 162, 158)
    _add_noise(buf, W, H, intensity=5, density=0.2, seed=1703)


def draw_ind_low_04(buf, W, H):
    """Barrel vault shed, dark olive."""
    base = (92, 105, 70)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    _corrugation_lines(buf, W, H, 0, H, max(2, H * 10 // 512), base, amplitude=10)
    # Barrel vault upper 30%
    vt = H * 30 // 100
    for y in range(vt):
        frac = y / max(1, vt)
        dark = int(20 * (1.0 - frac))
        for x in range(W):
            idx = y * W + x
            r, g, b = buf[idx]
            buf[idx] = (_clamp(r - dark), _clamp(g - dark), _clamp(b - dark))
    # Curved shadow lines
    for y in range(0, vt, max(2, H * 20 // 512)):
        _hline(buf, W, H, y, 0, W, 72, 82, 52)
    # Office block right 35%
    ow = W * 35 // 100
    ox = W - ow
    _fill_rect(buf, W, ox, 0, W, H, 105, 118, 82)
    # Office windows 2x3
    ww, wh = W * 35 // 512, H * 40 // 512
    for r in range(3):
        for c in range(2):
            wx = ox + W // 20 + c * (ow // 3)
            wy = H // 5 + r * (H // 4)
            _draw_window(buf, W, H, wx, wy, ww, wh, GLASS_DARK)
    _add_noise(buf, W, H, intensity=5, density=0.2, seed=1704)


def draw_ind_med_01(buf, W, H):
    """Large distribution unit, white steel."""
    base = (225, 228, 225)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    _corrugation_lines(buf, W, H, 0, H, max(2, H * 12 // 512), base, amplitude=8)
    # Loading docks: 3 large dark rectangles
    dw = W * 80 // 512
    dh = H * 100 // 512
    dy = H - dh - H * 5 // 100
    for i in range(3):
        dx = W // 8 + i * (W * 30 // 100)
        _fill_rect(buf, W, dx, dy, dx + dw, dy + dh, 42, 42, 45)
        # Dock canopy
        _fill_rect(buf, W, dx - 4, dy - max(1, H * 15 // 512), dx + dw + 4, dy, 195, 198, 195)
    # Rooftop plant zone top 15%
    ph = H * 15 // 100
    _fill_rect(buf, W, 0, 0, W, ph, 190, 190, 188)
    # Equipment silhouette
    for i in range(4):
        ex = W // 6 + i * (W // 5)
        ew = W * 30 // 512
        eh = max(2, ph * 60 // 100)
        _fill_rect(buf, W, ex, ph - eh, ex + ew, ph, 165, 168, 165)
    _add_noise(buf, W, H, intensity=4, density=0.15, seed=1801)


def draw_ind_med_02(buf, W, H):
    """Gabled factory, yellow/ochre brick."""
    base = (195, 165, 62)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    _brick_coursing(buf, W, H, 0, H, max(2, H * 12 // 512), (168, 138, 42), seed=802)
    # Central gable shadow at top 25%
    gt = H * 25 // 100
    for y in range(gt):
        frac = y / max(1, gt)
        dark = int(18 * (1.0 - frac))
        cx = W // 2
        spread = int(W // 2 * frac)
        for x in range(max(0, cx - spread), min(cx + spread, W)):
            idx = y * W + x
            r, g, b = buf[idx]
            buf[idx] = (_clamp(r - dark), _clamp(g - dark), _clamp(b - dark))
    # Wing sections slightly lighter
    _fill_rect_blend(buf, W, H, 0, 0, W // 4, gt, 208, 178, 75, 0.4)
    _fill_rect_blend(buf, W, H, W * 3 // 4, 0, W, gt, 208, 178, 75, 0.4)
    # Clerestory band at 70%
    cy = H * 70 // 100
    ch = max(2, H * 40 // 512)
    _fill_rect(buf, W, 0, cy, W, cy + ch, 228, 210, 108)
    # Ground floor loading bays
    lb_h = H * 30 // 100
    lb_sp = max(2, W * 80 // 512)
    for x in range(0, W, lb_sp):
        _fill_rect(buf, W, x + 4, H - lb_h, x + lb_sp - 4, H, 52, 42, 18)
    _add_noise(buf, W, H, intensity=5, density=0.2, seed=1802)


def draw_ind_med_03(buf, W, H):
    """Grid-frame structure, dark blue metal."""
    base = (38, 58, 105)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Steel frame grid
    fh = max(2, H * 128 // 512)
    fv = max(2, W * 96 // 512)
    fw = max(1, W * 6 // 512)
    frame_clr = (155, 160, 168)
    for y in range(0, H, fh):
        for dy in range(fw):
            _hline(buf, W, H, y + dy, 0, W, *frame_clr)
    for x in range(0, W, fv):
        for dx in range(fw):
            _vline(buf, W, H, x + dx, 0, H, *frame_clr)
    # Panel variation
    rng = random.Random(1803)
    for y in range(0, H, fh):
        for x in range(0, W, fv):
            d = rng.randint(-8, 8)
            _fill_rect(buf, W, x + fw, y + fw,
                       min(x + fv - fw, W), min(y + fh - fw, H),
                       _clamp(38 + d), _clamp(58 + d), _clamp(105 + d))
    # Clerestory top
    ct = max(2, H * 60 // 512)
    _fill_rect(buf, W, 0, 0, W, ct, 62, 78, 125)
    # Small square windows in clerestory
    sw = max(2, W * 20 // 512)
    for i in range(6):
        sx = W // 10 + i * (W // 7)
        _fill_rect(buf, W, sx, ct // 4, sx + sw, ct * 3 // 4, 120, 135, 165)
    # Anchor plates at frame intersections
    ap = max(1, W * 8 // 512)
    for y in range(0, H, fh):
        for x in range(0, W, fv):
            _fill_rect(buf, W, x - ap // 2, y - ap // 2, x + ap // 2, y + ap // 2, 105, 108, 115)
    _add_noise(buf, W, H, intensity=4, density=0.15, seed=1803)


def draw_ind_med_04(buf, W, H):
    """L-plan unit, light grey precast."""
    base = (182, 185, 190)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Precast panel joints
    pv = max(2, W * 128 // 512)
    ph = max(2, H * 96 // 512)
    lw = max(1, W * 2 // 512)
    for y in range(0, H, ph):
        for dy in range(lw):
            _hline(buf, W, H, y + dy, 0, W, 120, 122, 125)
    for x in range(0, W, pv):
        for dx in range(lw):
            _vline(buf, W, H, x + dx, 0, H, 120, 122, 125)
    # Corner entrance bay
    ew = W * 25 // 100
    ex = (W - ew) // 2
    _fill_rect(buf, W, ex, 0, ex + ew, H, 195, 198, 202)
    # Windows 4x4 upper 70%
    ww, wh = W * 45 // 512, H * 50 // 512
    _draw_window_grid(buf, W, H, 4, 4, ww, wh, GLASS_DARK,
                      start_y=H * 5 // 100,
                      spacing_y=H * 70 // (4 * 100))
    # Ground floor glazing strip
    gh = H * 20 // 100
    _fill_rect(buf, W, 0, H - gh, W, H, 55, 58, 65)
    _add_noise(buf, W, H, intensity=4, density=0.15, seed=1804)


def draw_ind_high_01(buf, W, H):
    """Process plant, bare concrete + pipe."""
    base = (155, 155, 150)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Form-work lines
    sp = max(2, H * 24 // 512)
    for y in range(0, H, sp):
        _hline(buf, W, H, y, 0, W, 138, 138, 132)
    # Pipe run on right 15%
    pw = W * 15 // 100
    px = W - pw
    _fill_rect(buf, W, px, 0, W, H, 200, 100, 35)
    # Flange marks
    fl_sp = max(2, H * 64 // 512)
    fl_r = max(2, pw // 4)
    for y in range(fl_sp // 2, H, fl_sp):
        cx = px + pw // 2
        for dy in range(-fl_r, fl_r):
            for dx in range(-fl_r, fl_r):
                if dx * dx + dy * dy <= fl_r * fl_r:
                    py2 = y + dy
                    px2 = cx + dx
                    if 0 <= py2 < H and 0 <= px2 < W:
                        buf[py2 * W + px2] = (175, 82, 25)
    # Chimney stack on left edge
    cw = max(2, W * 20 // 512)
    _fill_rect(buf, W, 0, 0, cw, H, 88, 85, 82)
    # Windows sparse 3x4
    ww, wh = W * 40 // 512, H * 45 // 512
    _draw_window_grid(buf, W, H, 3, 4, ww, wh, GLASS_DARK,
                      start_x=cw + W // 10)
    _add_noise(buf, W, H, intensity=5, density=0.2, seed=1901)


def draw_ind_high_02(buf, W, H):
    """Large span, dark steel + orange safety stripe."""
    base = (58, 62, 70)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # 3 arched openings in lower 70%
    ah = H * 70 // 100
    aw = W * 28 // 100
    ay = H - ah
    for i in range(3):
        ax = W // 12 + i * (W * 32 // 100)
        _fill_rect(buf, W, ax, ay, ax + aw, H, 25, 25, 28)
        # Arch top
        arc = max(2, aw // 3)
        _fill_rect(buf, W, ax + aw // 6, ay - arc, ax + aw - aw // 6, ay, 25, 25, 28)
    # Safety stripe band at 75%
    sy = H * 25 // 100 - H * 60 // 512
    sh = max(2, H * 60 // 512)
    stripe_w = max(2, W * 20 // 512)
    for y in range(sy, sy + sh):
        for x in range(W):
            stripe_idx = ((x + (y - sy)) // stripe_w) % 2
            if stripe_idx == 0:
                buf[y * W + x] = (210, 105, 25)
            else:
                buf[y * W + x] = (28, 28, 28)
    # Cooling tower silhouette right side
    ct_x = W * 75 // 100
    ct_w = W * 20 // 100
    ct_h = H * 40 // 100
    for y in range(ct_h):
        frac = y / max(1, ct_h)
        r_frac = 0.6 + 0.4 * math.sin(frac * math.pi)
        rw = int(ct_w * r_frac) // 2
        cx = ct_x + ct_w // 2
        for x in range(cx - rw, cx + rw):
            if 0 <= x < W and 0 <= y < H:
                buf[y * W + x] = (82, 85, 92)
    _add_noise(buf, W, H, intensity=4, density=0.15, seed=1902)


def draw_ind_high_03(buf, W, H):
    """Multi-storey process plant, grey/blue."""
    base = (85, 105, 125)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Floor bands
    fh = max(2, H * 80 // 512)
    for y in range(0, H, fh):
        _fill_rect(buf, W, 0, y, W, y + max(1, H * 3 // 512), 55, 70, 88)
    # External staircase zigzag on left 20%
    sw = W * 20 // 100
    stair_clr = (155, 170, 185)
    step = max(2, H * 40 // 512)
    for y in range(0, H, step * 2):
        # Going right
        for dy in range(step):
            sx = int(sw * dy / step)
            if y + dy < H:
                buf[(y + dy) * W + min(sx, W - 1)] = stair_clr
                if sx + 1 < W:
                    buf[(y + dy) * W + sx + 1] = stair_clr
        # Going left
        for dy in range(step):
            sx = int(sw * (1.0 - dy / step))
            if y + step + dy < H:
                buf[(y + step + dy) * W + min(sx, W - 1)] = stair_clr
                if sx + 1 < W:
                    buf[(y + step + dy) * W + sx + 1] = stair_clr
    # External ductwork on right
    dw = max(2, W * 30 // 512)
    dx = W - dw - W // 20
    _fill_rect(buf, W, dx, 0, dx + dw, H, 110, 108, 105)
    # Instrument panels on each floor
    for y in range(fh // 2, H, fh):
        for i in range(3):
            px = W // 4 + i * (W // 5)
            pw = max(2, W * 15 // 512)
            ph2 = max(2, H * 12 // 512)
            _fill_rect(buf, W, px, y, px + pw, y + ph2, 140, 155, 172)
    _add_noise(buf, W, H, intensity=4, density=0.15, seed=1903)


def draw_ind_high_04(buf, W, H):
    """Silo cluster, weathered steel + rust."""
    base = (100, 82, 65)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Rust patches
    rng = random.Random(1904)
    for _ in range(12):
        px = rng.randint(0, W - 1)
        py = rng.randint(0, H - 1)
        pw = rng.randint(W * 30 // 512, W * 60 // 512)
        ph = rng.randint(H * 30 // 512, H * 60 // 512)
        _fill_rect_blend(buf, W, H, px, py, px + pw, py + ph, 145, 68, 28, 0.55)
    # 3 cylindrical silo forms
    silo_w = W * 28 // 100
    for i in range(3):
        sx = W * 5 // 100 + i * (W * 32 // 100)
        # Shading darker on right side
        for y in range(H // 4, H):
            for x in range(sx, min(sx + silo_w, W)):
                frac = (x - sx) / max(1, silo_w)
                dark = int(25 * frac)
                idx = y * W + x
                r, g, b = buf[idx]
                buf[idx] = (_clamp(r - dark), _clamp(g - dark), _clamp(b - dark))
        # Oval arc at top
        cy = H // 4
        for dx in range(silo_w):
            x = sx + dx
            frac = (dx - silo_w // 2) / max(1, silo_w // 2)
            arc_y = cy - int((1.0 - frac * frac) * H * 15 // 512)
            if 0 <= x < W and 0 <= arc_y < H:
                buf[arc_y * W + x] = (72, 55, 40)
    # Ladder on left silo
    lx = W * 5 // 100 + silo_w // 2
    for y in range(H // 4, H):
        if 0 <= lx < W:
            buf[y * W + lx] = (55, 42, 30)
        if y % max(2, H * 20 // 512) < 2:
            for dx in range(-3, 4):
                px2 = lx + dx
                if 0 <= px2 < W:
                    buf[y * W + px2] = (55, 42, 30)
    _add_noise(buf, W, H, intensity=6, density=0.25, seed=1904)


def draw_svc_fire_station(buf, W, H):
    """Red brick civic building."""
    base = (175, 50, 35)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    _brick_coursing(buf, W, H, 0, H, max(2, H * 10 // 512), (142, 38, 25), seed=2001)
    # Bay doors: 3 arched openings lower 55%
    dh = H * 55 // 100
    dy = H - dh
    dw = W * 100 // 512
    for i in range(3):
        dx = W // 8 + i * (W * 28 // 100)
        _fill_rect(buf, W, dx, dy, dx + dw, H, 90, 35, 20)
        # Ribbed panelling inside
        for y in range(dy, H, max(2, H * 15 // 512)):
            _hline(buf, W, H, y, dx + 2, dx + dw - 2, 72, 28, 15)
        # Arch top
        arc = max(2, dw // 3)
        _fill_rect(buf, W, dx + dw // 6, dy - arc, dx + dw - dw // 6, dy, 90, 35, 20)
    # String course above bays
    sc_y = dy - max(2, H * 30 // 512)
    sc_h = max(2, H * 30 // 512)
    _fill_rect(buf, W, 0, sc_y, W, sc_y + sc_h, 200, 180, 135)
    # Upper windows
    ww, wh = W * 40 // 512, H * 55 // 512
    _draw_window_grid(buf, W, H, 5, 1, ww, wh, GLASS_DARK,
                      start_y=H * 60 // 512,
                      frame_rgb=(155, 42, 28), frame_w=max(1, W // 256))
    # Hose tower right 20%
    tw = W * 20 // 100
    tx = W - tw
    _fill_rect(buf, W, tx, 0, W, H, 168, 48, 32)
    _add_noise(buf, W, H, intensity=5, density=0.2, seed=2001)


def draw_svc_police_station(buf, W, H):
    """Civic, dark navy + white."""
    base = (25, 42, 85)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    _brick_coursing(buf, W, H, 0, H, max(2, H * 10 // 512), (18, 32, 68), seed=2002)
    # Columned portico: central 40% width
    pw = W * 40 // 100
    px = (W - pw) // 2
    col_w = max(1, W * 12 // 512)
    for i in range(4):
        cx = px + i * (pw // 3)
        _fill_rect(buf, W, cx, 0, cx + col_w, H, 235, 235, 230)
    # Frieze band top 20%
    fh = H * 20 // 100
    _fill_rect(buf, W, 0, 0, W, fh, 245, 245, 240)
    # Windows 3x3
    ww, wh = W * 45 // 512, H * 60 // 512
    _draw_window_grid(buf, W, H, 3, 3, ww, wh, GLASS_DARK,
                      start_y=fh + H * 20 // 512,
                      frame_rgb=(235, 235, 230), frame_w=max(1, W * 3 // 512))
    # Flag mast
    mx = W // 2
    for y in range(max(1, H * 30 // 512)):
        if 0 <= mx < W:
            buf[y * W + mx] = (235, 235, 230)
    _add_noise(buf, W, H, intensity=4, density=0.15, seed=2002)


def draw_svc_power_plant(buf, W, H):
    """Industrial civic, grey + yellow hazard."""
    base = (140, 142, 140)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Generator hall central 60%
    gw = W * 60 // 100
    gx = (W - gw) // 2
    _fill_rect(buf, W, gx, 0, gx + gw, H, 108, 108, 105)
    # Hazard stripe at 70%
    sy = H * 30 // 100 - H * 80 // 512
    sh = max(2, H * 80 // 512)
    stripe_w = max(2, W * 30 // 512)
    for y in range(max(0, sy), min(sy + sh, H)):
        for x in range(W):
            diag = ((x + (y - sy)) // stripe_w) % 2
            if diag == 0:
                buf[y * W + x] = (210, 192, 35)
            else:
                buf[y * W + x] = (28, 28, 28)
    # High-voltage sign panels: 3 yellow squares on generator hall
    sq = max(2, W * 50 // 512)
    for i in range(3):
        sx = gx + gw // 6 + i * (gw // 3)
        sq_y = H * 55 // 100
        _fill_rect(buf, W, sx, sq_y, sx + sq, sq_y + sq, 220, 200, 35)
        # Lightning bolt symbol approximation
        _fill_rect(buf, W, sx + sq // 3, sq_y + sq // 5,
                   sx + sq * 2 // 3, sq_y + sq * 4 // 5, 28, 28, 28)
    # Louver windows
    for y in range(H * 70 // 100, H, max(2, H * 15 // 512)):
        _hline(buf, W, H, y, gx, gx + gw, 125, 125, 122)
    _add_noise(buf, W, H, intensity=5, density=0.2, seed=2003)


def draw_svc_water_tower(buf, W, H):
    """Weathered steel + pale blue tank."""
    # Lower half: legs
    base = (112, 100, 85)
    _fill_rect(buf, W, 0, H // 2, W, H, *base)
    # Leg structure: 4 diagonal bands
    leg_w = W // 5
    for i in range(4):
        lx = W // 10 + i * (W * 22 // 100)
        for y in range(H // 2, H):
            frac = (y - H // 2) / max(1, H // 2)
            spread = int(leg_w * frac * 0.5)
            cx = lx + leg_w // 2
            for x in range(cx - spread - 2, cx - spread + 2):
                if 0 <= x < W:
                    buf[y * W + x] = (85, 75, 62)
            for x in range(cx + spread - 2, cx + spread + 2):
                if 0 <= x < W:
                    buf[y * W + x] = (85, 75, 62)
    # Tank upper 50%
    _fill_rect(buf, W, 0, 0, W, H // 2, 142, 165, 185)
    # Riveted band lines
    rv_sp = max(2, H * 30 // 512)
    for y in range(0, H // 2, rv_sp):
        _hline(buf, W, H, y, 0, W, 122, 142, 162)
        _hline(buf, W, H, y + 1, 0, W, 122, 142, 162)
    # Tank dome top 10%
    dt = H * 10 // 100
    _fill_rect(buf, W, 0, 0, W, dt, 120, 140, 158)
    # Access ladder
    lx = W // 5
    for y in range(0, H):
        if 0 <= lx < W:
            buf[y * W + lx] = (78, 68, 55)
        if y % max(2, H * 12 // 512) < 2:
            for dx in range(-4, 5):
                px = lx + dx
                if 0 <= px < W:
                    buf[y * W + px] = (78, 68, 55)
    _add_noise(buf, W, H, intensity=5, density=0.2, seed=2004)


def draw_roof_cell(buf, W, H):
    """Shared flat roof material - weathered grey bitumen/felt."""
    base = (162, 158, 148)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Roofing felt strip lines
    sp = max(2, H * 16 // 512)
    for y in range(0, H, sp):
        _hline(buf, W, H, y, 0, W, 148, 145, 135)
    # Darker patches
    rng = random.Random(5001)
    for _ in range(8):
        px = rng.randint(0, W - 1)
        py = rng.randint(0, H - 1)
        pw = rng.randint(W * 20 // 512, W * 60 // 512)
        ph = rng.randint(H * 20 // 512, H * 60 // 512)
        _fill_rect_blend(buf, W, H, px, py, px + pw, py + ph, 140, 135, 125, 0.4)
    # Fine gravel noise
    _add_noise(buf, W, H, intensity=6, density=0.6, seed=5001)


# ---------------------------------------------------------------------------
# Cell draw dispatch table
# ---------------------------------------------------------------------------

CELL_DRAW_FNS = {
    (0, 0): draw_res_low_01,
    (0, 1): draw_res_low_02,
    (0, 2): draw_res_low_03,
    (0, 3): draw_res_low_04,
    (0, 4): draw_res_med_01,
    (0, 5): draw_res_med_02,
    (0, 6): draw_res_med_03,
    (0, 7): draw_res_med_04,
    (1, 0): draw_res_high_01,
    (1, 1): draw_res_high_02,
    (1, 2): draw_res_high_03,
    (1, 3): draw_res_high_04,
    (1, 4): draw_com_low_01,
    (1, 5): draw_com_low_02,
    (1, 6): draw_com_low_03,
    (1, 7): draw_com_low_04,
    (2, 0): draw_com_med_01,
    (2, 1): draw_com_med_02,
    (2, 2): draw_com_med_03,
    (2, 3): draw_com_med_04,
    (2, 4): draw_com_high_01,
    (2, 5): draw_com_high_02,
    (2, 6): draw_com_high_03,
    (2, 7): draw_com_high_04,
    (3, 0): draw_ind_low_01,
    (3, 1): draw_ind_low_02,
    (3, 2): draw_ind_low_03,
    (3, 3): draw_ind_low_04,
    (3, 4): draw_ind_med_01,
    (3, 5): draw_ind_med_02,
    (3, 6): draw_ind_med_03,
    (3, 7): draw_ind_med_04,
    (4, 0): draw_ind_high_01,
    (4, 1): draw_ind_high_02,
    (4, 2): draw_ind_high_03,
    (4, 3): draw_ind_high_04,
    (4, 4): draw_svc_fire_station,
    (4, 5): draw_svc_police_station,
    (4, 6): draw_svc_power_plant,
    (4, 7): draw_svc_water_tower,
    (5, 0): draw_roof_cell,
}

RESERVED_COLOR = (72, 72, 72)


# ---------------------------------------------------------------------------
# Render the full atlas into a flat pixel buffer
# ---------------------------------------------------------------------------

def render_atlas_pixels(atlas_w, atlas_h):
    """
    Render the full atlas into a flat list of (r, g, b) tuples.
    Each cell is rendered at its native resolution using the draw function.
    """
    cell_w = atlas_w // GRID_COLS
    cell_h = atlas_h // GRID_ROWS

    # Initialize to reserved color
    pixels = [RESERVED_COLOR] * (atlas_w * atlas_h)

    for row in range(GRID_ROWS):
        for col in range(GRID_COLS):
            fn = CELL_DRAW_FNS.get((row, col))
            if fn is None:
                # Reserved cell: fill with dark grey
                x0 = col * cell_w
                y0 = row * cell_h
                _fill_rect(pixels, atlas_w, x0, y0, x0 + cell_w, y0 + cell_h, *RESERVED_COLOR)
                continue

            # Render cell into a temporary buffer at cell resolution
            cell_buf = [RESERVED_COLOR] * (cell_w * cell_h)
            fn(cell_buf, cell_w, cell_h)

            # Copy cell buffer into atlas
            x0 = col * cell_w
            y0 = row * cell_h
            for cy in range(cell_h):
                for cx in range(cell_w):
                    pixels[(y0 + cy) * atlas_w + (x0 + cx)] = cell_buf[cy * cell_w + cx]

            print(f"    Cell ({row},{col}): rendered")

    return pixels


def downsample_2x(pixels, w, h):
    """2x2 box-average downsample. Returns (new_pixels, w//2, h//2)."""
    nw = w // 2
    nh = h // 2
    out = [(0, 0, 0)] * (nw * nh)
    for y in range(nh):
        for x in range(nw):
            sx = x * 2
            sy = y * 2
            r0, g0, b0 = pixels[sy * w + sx]
            r1, g1, b1 = pixels[sy * w + sx + 1]
            r2, g2, b2 = pixels[(sy + 1) * w + sx]
            r3, g3, b3 = pixels[(sy + 1) * w + sx + 1]
            out[y * nw + x] = ((r0 + r1 + r2 + r3 + 2) // 4,
                               (g0 + g1 + g2 + g3 + 2) // 4,
                               (b0 + b1 + b2 + b3 + 2) // 4)
    return out, nw, nh


# ---------------------------------------------------------------------------
# DXT1 / BC1 encoder with proper block-colour quantisation
# ---------------------------------------------------------------------------

def rgb_to_565(r, g, b):
    """Pack uint8 RGB into a 16-bit RGB565 value."""
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def rgb565_to_rgb(c565):
    """Unpack RGB565 to (r, g, b) uint8."""
    r = ((c565 >> 11) & 0x1F) << 3
    g = ((c565 >> 5) & 0x3F) << 2
    b = (c565 & 0x1F) << 3
    return r, g, b


def encode_dxt1_block(block_pixels):
    """
    Encode a 4x4 block of (r,g,b) tuples into 8 bytes of DXT1 data.
    Uses min/max colour endpoints with proper 4-palette interpolation.
    block_pixels: list of 16 (r,g,b) tuples in row-major order.
    """
    # Find min and max luminance pixels for good endpoint selection
    min_lum = 999999
    max_lum = -1
    min_rgb = block_pixels[0]
    max_rgb = block_pixels[0]

    for r, g, b in block_pixels:
        lum = r * 2 + g * 5 + b
        if lum < min_lum:
            min_lum = lum
            min_rgb = (r, g, b)
        if lum > max_lum:
            max_lum = lum
            max_rgb = (r, g, b)

    c0_565 = rgb_to_565(*max_rgb)
    c1_565 = rgb_to_565(*min_rgb)

    # Ensure c0 >= c1 for 4-colour mode (opaque)
    if c0_565 < c1_565:
        c0_565, c1_565 = c1_565, c0_565
        max_rgb, min_rgb = min_rgb, max_rgb
    elif c0_565 == c1_565:
        # Uniform block: all indices 0
        return struct.pack('<HH', c0_565, c1_565) + b'\x00\x00\x00\x00'

    # Build 4-colour palette
    r0, g0, b0 = rgb565_to_rgb(c0_565)
    r1, g1, b1 = rgb565_to_rgb(c1_565)

    palette = [
        (r0, g0, b0),
        (r1, g1, b1),
        ((2 * r0 + r1 + 1) // 3, (2 * g0 + g1 + 1) // 3, (2 * b0 + b1 + 1) // 3),
        ((r0 + 2 * r1 + 1) // 3, (g0 + 2 * g1 + 1) // 3, (b0 + 2 * b1 + 1) // 3),
    ]

    # Assign each pixel to the closest palette entry
    indices = 0
    for i, (pr, pg, pb) in enumerate(block_pixels):
        best_dist = 999999
        best_idx = 0
        for pi, (cr, cg, cb) in enumerate(palette):
            dr = pr - cr
            dg = pg - cg
            db = pb - cb
            dist = dr * dr + dg * dg + db * db
            if dist < best_dist:
                best_dist = dist
                best_idx = pi
        indices |= (best_idx << (i * 2))

    return struct.pack('<HHI', c0_565, c1_565, indices)


def encode_dxt1_image_from_pixels(pixels, width, height):
    """
    Encode a full image from pixel buffer to DXT1.
    pixels: list of (r,g,b) tuples, row-major, width*height elements.
    """
    blocks_x = max(1, (width + 3) // 4)
    blocks_y = max(1, (height + 3) // 4)
    out = bytearray()

    for by in range(blocks_y):
        for bx in range(blocks_x):
            block = []
            for py in range(4):
                for px in range(4):
                    x = bx * 4 + px
                    y = by * 4 + py
                    if x < width and y < height:
                        block.append(pixels[y * width + x])
                    else:
                        # Clamp to edge
                        cx = min(x, width - 1)
                        cy = min(y, height - 1)
                        block.append(pixels[cy * width + cx])
            out += encode_dxt1_block(block)

    return bytes(out)


# ---------------------------------------------------------------------------
# DDS header builder (standard 128-byte, no DX10 extension)
# ---------------------------------------------------------------------------

def build_dds_header(width, height, mip_levels):
    """Build a standard 128-byte DDS file header for DXT1/BC1."""
    DDSD_CAPS        = 0x00000001
    DDSD_HEIGHT      = 0x00000002
    DDSD_WIDTH       = 0x00000004
    DDSD_PIXELFORMAT = 0x00001000
    DDSD_MIPMAPCOUNT = 0x00020000
    DDSD_LINEARSIZE  = 0x00080000

    flags = (DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH |
             DDSD_PIXELFORMAT | DDSD_MIPMAPCOUNT | DDSD_LINEARSIZE)

    blocks_x = max(1, (width + 3) // 4)
    blocks_y = max(1, (height + 3) // 4)
    linear_size = blocks_x * blocks_y * 8

    DDPF_FOURCC = 0x00000004
    DDSCAPS_COMPLEX = 0x00000008
    DDSCAPS_TEXTURE = 0x00001000
    DDSCAPS_MIPMAP  = 0x00400000
    caps1 = DDSCAPS_COMPLEX | DDSCAPS_TEXTURE | DDSCAPS_MIPMAP

    magic = b'DDS '
    hdr = struct.pack('<IIIIIII',
        124, flags, height, width, linear_size, 0, mip_levels)
    hdr += bytes(44)  # dwReserved1[11]
    hdr += struct.pack('<II4sIIIII',
        32, DDPF_FOURCC, b'DXT1', 0, 0, 0, 0, 0)
    hdr += struct.pack('<IIII', caps1, 0, 0, 0)
    hdr += struct.pack('<I', 0)  # dwReserved2

    assert len(hdr) == 124
    return magic + hdr


# ---------------------------------------------------------------------------
# DDS generation from pixel buffer with proper mip chain
# ---------------------------------------------------------------------------

def generate_dds_from_pixels(output_path, pixels, width, height, mip_levels):
    """Generate a DXT1 DDS file from a pixel buffer with mip chain."""
    header = build_dds_header(width, height, mip_levels)
    assert len(header) == 128

    all_data = bytearray()
    cur_pixels = pixels
    cur_w = width
    cur_h = height

    for mip in range(mip_levels):
        print(f"    Encoding mip {mip}: {cur_w}x{cur_h}")
        mip_data = encode_dxt1_image_from_pixels(cur_pixels, cur_w, cur_h)
        all_data += mip_data
        if mip < mip_levels - 1:
            cur_pixels, cur_w, cur_h = downsample_2x(cur_pixels, cur_w, cur_h)

    total = len(header) + len(all_data)
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, 'wb') as f:
        f.write(header)
        f.write(all_data)

    return total


# ---------------------------------------------------------------------------
# Source PNG generation
# ---------------------------------------------------------------------------

def generate_source_png(output_path, pixels, width, height):
    """Generate the source PNG authoring file for validate_assets.py Check #28."""
    try:
        from PIL import Image
    except ImportError:
        print("  Skipping PNG generation: Pillow not installed.")
        return

    img = Image.new("RGB", (width, height))
    img.putdata(pixels)
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    img.save(output_path, "PNG")
    print(f"  Saved source PNG: {output_path} ({os.path.getsize(output_path):,} bytes)")


# ---------------------------------------------------------------------------
# Verification
# ---------------------------------------------------------------------------

def verify_expected_sizes(primary_path, fallback_path):
    """Verify the generated files match the spec byte sizes."""
    EXPECTED_PRIMARY  = 11_174_016
    EXPECTED_FALLBACK =  2_785_408

    p_size = os.path.getsize(primary_path)
    f_size = os.path.getsize(fallback_path)

    print(f"\nByte-size verification:")
    print(f"  {primary_path}:  {p_size:>12,} bytes  "
          f"(expected {EXPECTED_PRIMARY:>12,})  "
          f"{'OK' if p_size == EXPECTED_PRIMARY else 'MISMATCH'}")
    print(f"  {fallback_path}: {f_size:>12,} bytes  "
          f"(expected {EXPECTED_FALLBACK:>12,})  "
          f"{'OK' if f_size == EXPECTED_FALLBACK else 'MISMATCH'}")

    if p_size != EXPECTED_PRIMARY:
        raise RuntimeError(
            f"Primary atlas size mismatch: got {p_size}, expected {EXPECTED_PRIMARY}")
    if f_size != EXPECTED_FALLBACK:
        raise RuntimeError(
            f"Fallback atlas size mismatch: got {f_size}, expected {EXPECTED_FALLBACK}")
    print("  All sizes match spec.")


def verify_headers(primary_path, fallback_path):
    """Read back the DDS headers and confirm magic, dimensions, mip count."""
    print("\nHeader verification:")
    for path, expected_w, expected_h, expected_mips in [
        (primary_path, 4096, 4096, 5),
        (fallback_path, 2048, 2048, 4),
    ]:
        with open(path, 'rb') as f:
            magic = f.read(4)
            _size = struct.unpack('<I', f.read(4))[0]
            _flags = struct.unpack('<I', f.read(4))[0]
            height = struct.unpack('<I', f.read(4))[0]
            width = struct.unpack('<I', f.read(4))[0]
            _pitch = struct.unpack('<I', f.read(4))[0]
            _depth = struct.unpack('<I', f.read(4))[0]
            mips = struct.unpack('<I', f.read(4))[0]
            f.seek(84)
            fourcc = f.read(4)

        ok_magic = magic == b'DDS '
        ok_w = width == expected_w
        ok_h = height == expected_h
        ok_mips = mips == expected_mips
        ok_fourcc = fourcc == b'DXT1'
        status = 'OK' if all([ok_magic, ok_w, ok_h, ok_mips, ok_fourcc]) else 'MISMATCH'
        print(f"  {os.path.basename(path)}: magic={magic} {width}x{height} "
              f"mips={mips} fourcc={fourcc}  [{status}]")
        if not all([ok_magic, ok_w, ok_h, ok_mips, ok_fourcc]):
            raise RuntimeError(f"Header verification failed for {path}")
    print("  All headers valid.")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

if __name__ == '__main__':
    script_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.dirname(script_dir)

    primary_path = os.path.join(repo_root, 'assets', 'textures', 'buildings',
                                'buildings_atlas_d.dds')
    fallback_path = os.path.join(repo_root, 'assets', 'textures', 'buildings',
                                 'buildings_atlas_d_2k.dds')
    png_path = os.path.join(repo_root, 'assets', 'textures', 'buildings',
                            'buildings_atlas_d.png')

    # Pre-flight byte-size formula check
    def dxt1_mip_bytes(w, h):
        bx = max(1, (w + 3) // 4)
        by = max(1, (h + 3) // 4)
        return bx * by * 8

    EXPECTED_PRIMARY = 11_174_016
    EXPECTED_FALLBACK = 2_785_408
    primary_data_bytes = sum(dxt1_mip_bytes(max(1, 4096 >> m), max(1, 4096 >> m))
                             for m in range(5))
    fallback_data_bytes = sum(dxt1_mip_bytes(max(1, 2048 >> m), max(1, 2048 >> m))
                              for m in range(4))
    assert 128 + primary_data_bytes == EXPECTED_PRIMARY
    assert 128 + fallback_data_bytes == EXPECTED_FALLBACK

    print("=== AI Town Phase 11e -- Building Atlas DDS Generator ===\n")

    # ----- Step 1: Render at 4096x4096 -----
    print("Rendering 4096x4096 atlas pixels...")
    pixels_4k = render_atlas_pixels(4096, 4096)
    print(f"  Total pixels: {len(pixels_4k):,}")

    # ----- Step 2: Generate primary DDS (4096x4096, 5 mips) -----
    print(f"\nGenerating primary atlas: {primary_path}")
    primary_total = generate_dds_from_pixels(primary_path, pixels_4k, 4096, 4096, 5)

    # ----- Step 3: Render at 2048x2048 for fallback + PNG -----
    print("\nRendering 2048x2048 atlas pixels...")
    pixels_2k = render_atlas_pixels(2048, 2048)
    print(f"  Total pixels: {len(pixels_2k):,}")

    # ----- Step 4: Generate fallback DDS (2048x2048, 4 mips) -----
    print(f"\nGenerating fallback atlas: {fallback_path}")
    fallback_total = generate_dds_from_pixels(fallback_path, pixels_2k, 2048, 2048, 4)

    # ----- Step 5: Source PNG (2048x2048) for Check #28 -----
    print(f"\nGenerating source PNG: {png_path}")
    generate_source_png(png_path, pixels_2k, 2048, 2048)

    # ----- Step 6: Verify -----
    verify_expected_sizes(primary_path, fallback_path)
    verify_headers(primary_path, fallback_path)

    print("\n=== Generation complete ===")
    print(f"  {primary_path}")
    print(f"    {os.path.getsize(primary_path):,} bytes")
    print(f"  {fallback_path}")
    print(f"    {os.path.getsize(fallback_path):,} bytes")
    print(f"  {png_path}")
    print(f"    {os.path.getsize(png_path):,} bytes")
