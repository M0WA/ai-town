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
    """Detached house: warm brick walls, door + 2 windows, utility meter box.

    UV orientation: face_v=0 → top of PNG → FLOOR level of wall.
                    face_v=1 → bottom of PNG → CEILING/ROOFLINE level.
    Layout (top→bottom in PNG = floor→ceiling on wall):
      [door + meter box]   ← floor level
      [2 centred windows]  ← mid-wall
      [parapet cap band]   ← roofline
    """
    base = (168, 100, 65)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    _brick_coursing(buf, W, H, 0, H, max(2, H * 12 // 512), (135, 78, 48), seed=101)
    _add_noise(buf, W, H, intensity=6, density=0.25, seed=1001)

    # --- Door: wide, centred, at TOP of PNG → renders at floor level ---
    # Door is 80px wide (31% of cell = ~1.25 m in a 4 m wall), 155px tall (60%)
    dw = W * 130 // 512   # 65 px — narrower door
    dh = H * 310 // 512   # 155 px
    dx = (W - dw) // 2    # centred: (256-80)//2 = 88
    dy = H * 8 // 512     # small top margin (~4 px)
    _fill_rect(buf, W, dx, dy, dx + dw, dy + dh, 100, 65, 25)
    _fill_rect(buf, W, dx + 2, dy + 2, dx + dw - 2, dy + dh - 2, 75, 45, 15)

    # Utility meter box: small grey panel to the right of door
    mb_x = dx + dw + W * 15 // 512
    mb_y = dy + dh // 4
    mb_w = W * 28 // 512
    mb_h = H * 40 // 512
    _fill_rect(buf, W, mb_x, mb_y, mb_x + mb_w, mb_y + mb_h, 185, 182, 178)
    _fill_rect(buf, W, mb_x + 2, mb_y + 2, mb_x + mb_w - 2, mb_y + mb_h - 2, 160, 158, 155)

    # --- Two windows flanking the door, same floor level, shorter than door ---
    # Each window 40px wide, 100px tall; 10px gap from door edge
    ww = W * 80 // 512           # 40 px wide
    wh = ww * 8 // 6             # compensate for wall 8S wide × 6S tall → square in world space
    gap = W * 35 // 512   # slightly wider gap from door
    wx_left  = dx - gap - ww   # left of door
    wx_right = dx + dw + gap   # right of door
    wy = H * 180 // 512        # ~35% up from floor (PNG y=90 → 35% of 256)
    for wx in (wx_left, wx_right):
        _draw_window(buf, W, H, wx, wy, ww, wh,
                     (45, 48, 55), frame_rgb=(120, 92, 62),
                     frame_w=max(1, W // 256))

    # --- Parapet cap at BOTTOM of PNG → renders at roofline of wall ---
    cap_h = H * 8 // 100
    _fill_rect(buf, W, 0, H - cap_h, W, H, 148, 142, 130)


def draw_res_low_02(buf, W, H):
    """Semi-detached villa: cream/white plaster, dark trim, arched windows (no door).

    UV orientation: face_v=0 → top of PNG → FLOOR level of wall.
                    face_v=1 → bottom of PNG → CEILING/ROOFLINE level.
    Wall geometry: 4S wide × 5S tall (each unit of the semi-detached pair).
    No door in texture: both units share this cell so a centred door would
    appear twice. Entrance is implied at ground level between the two units.
    Single floor only — matches res_low_01 storey count.
    Layout (top→bottom in PNG = floor→ceiling on wall):
      [2 arched windows, single floor, centred vertically]
      [dark trim dado]                ← roofline
    """
    base = (238, 232, 215)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Fine render texture lines
    sp = max(2, H * 28 // 512)
    for y in range(0, H, sp):
        _hline(buf, W, H, y, 0, W, 228, 222, 205)

    # --- Two arched windows, synced with res_low_01 (same ww, aspect for 4S×6S wall) ---
    ww = W * 80 // 512            # same texture-space size as res_low_01
    wh = ww * 2 // 3             # square in world space: ww*(4S/6S)
    sp_x = W * 200 // 512
    wx0  = (W - sp_x) // 2
    wy   = H * 180 // 512        # same mid-wall position as res_low_01

    for wx in (wx0, wx0 + sp_x):
        arch_h = max(2, wh // 5)
        arch_w = ww * 7 // 10
        arch_x = wx + (ww - arch_w) // 2
        _fill_rect(buf, W, arch_x, wy - arch_h, arch_x + arch_w, wy, 38, 40, 48)
        _draw_window(buf, W, H, wx, wy, ww, wh, (42, 45, 55),
                     frame_rgb=(55, 48, 42), frame_w=max(2, W * 4 // 512))
        sill_h = max(2, H * 8 // 512)
        _fill_rect(buf, W, wx, wy + wh + 2, wx + ww, wy + wh + 2 + sill_h, 185, 95, 60)

    # Dark trim base dado at BOTTOM of PNG → roofline
    dado_h = H * 10 // 100
    _fill_rect(buf, W, 0, H - dado_h, W, H, 62, 55, 48)
    _add_noise(buf, W, H, intensity=4, density=0.15, seed=1002)


def draw_res_low_03(buf, W, H):
    """Cottage: warm orange-brown brick, clay-tile roof hint, 2 windows (no door).
    Wall geometry matches res_low_03: 8S wide × 6S tall.
    Used on back/side faces; front face uses draw_res_low_03_door."""
    base = (182, 115, 62)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    _brick_coursing(buf, W, H, 0, H, max(2, H * 14 // 512), (148, 88, 42), seed=1003)
    # Clay-tile roof hint at top 22%
    tile_h = H * 22 // 100
    tile_row_h = max(3, H * 18 // 512)
    for y in range(0, tile_h, tile_row_h):
        row_idx = y // tile_row_h
        base_c = (195, 88, 42) if (row_idx % 2 == 0) else (172, 72, 32)
        _fill_rect(buf, W, 0, y, W, y + tile_row_h, *base_c)
        _hline(buf, W, H, y, 0, W, 138, 55, 25)
        tile_w = max(4, W * 40 // 512)
        offset = (tile_w // 2) if (row_idx % 2 == 1) else 0
        for x in range(offset, W, tile_w):
            _vline(buf, W, H, x, y, min(y + tile_row_h, H), 138, 55, 25)
    # 2 windows — square in world space for 8S wide × 6S tall wall
    ww = W * 80 // 512
    wh = ww * 8 // 6
    sp_x = W * 200 // 512
    wx0 = (W - sp_x) // 2
    wy = H * 180 // 512
    for wx in (wx0, wx0 + sp_x):
        _draw_window(buf, W, H, wx, wy, ww, wh, (42, 45, 52),
                     frame_rgb=(105, 62, 32), frame_w=max(2, W * 3 // 512))
        sill_h = max(2, H * 8 // 512)
        _fill_rect(buf, W, wx, wy + wh + 2, wx + ww, wy + wh + 2 + sill_h, 148, 165, 55)
    _add_noise(buf, W, H, intensity=6, density=0.25, seed=1003)


def draw_res_low_03_door(buf, W, H):
    """Res_low_03 front-door face (6,1): brick wall + centred door + 2 flanking windows.
    Used only on the front face of res_low_03. Wall: 8S wide × 6S tall."""
    base = (182, 115, 62)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    _brick_coursing(buf, W, H, 0, H, max(2, H * 14 // 512), (148, 88, 42), seed=1003)
    # Clay-tile roof hint at top 22%
    tile_h = H * 22 // 100
    tile_row_h = max(3, H * 18 // 512)
    for y in range(0, tile_h, tile_row_h):
        row_idx = y // tile_row_h
        base_c = (195, 88, 42) if (row_idx % 2 == 0) else (172, 72, 32)
        _fill_rect(buf, W, 0, y, W, y + tile_row_h, *base_c)
        _hline(buf, W, H, y, 0, W, 138, 55, 25)
        tile_w = max(4, W * 40 // 512)
        offset = (tile_w // 2) if (row_idx % 2 == 1) else 0
        for x in range(offset, W, tile_w):
            _vline(buf, W, H, x, y, min(y + tile_row_h, H), 138, 55, 25)
    # Door centred at floor level
    dw = W * 100 // 512
    dh = H * 270 // 512
    dx = (W - dw) // 2
    dy = H * 8 // 512
    _fill_rect(buf, W, dx, dy, dx + dw, dy + dh, 80, 48, 20)
    _fill_rect(buf, W, dx + 2, dy + 2, dx + dw - 2, dy + dh - 2, 58, 32, 10)
    # 2 windows flanking door — square in world space for 8S wide × 6S tall wall
    ww = W * 80 // 512
    wh = ww * 8 // 6
    gap = W * 35 // 512
    wx_left = dx - gap - ww
    wx_right = dx + dw + gap
    wy = H * 180 // 512
    for wx in (wx_left, wx_right):
        _draw_window(buf, W, H, wx, wy, ww, wh, (42, 45, 52),
                     frame_rgb=(105, 62, 32), frame_w=max(2, W * 3 // 512))
        sill_h = max(2, H * 8 // 512)
        _fill_rect(buf, W, wx, wy + wh + 2, wx + ww, wy + wh + 2 + sill_h, 148, 165, 55)
    _add_noise(buf, W, H, intensity=6, density=0.25, seed=6003)


def draw_res_low_04(buf, W, H):
    """Red-brick terrace: dark red brick, mortar joints, small punched windows, metal-tile roof hint."""
    base = (135, 42, 38)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    _brick_coursing(buf, W, H, 0, H, max(2, H * 12 // 512), (105, 28, 25), seed=1004)
    _add_noise(buf, W, H, intensity=7, density=0.3, seed=1004)
    # Metal-tile roof hint at top 18% — dark grey with horizontal seam lines
    roof_h = H * 18 // 100
    _fill_rect(buf, W, 0, 0, W, roof_h, 68, 68, 72)
    seam_sp = max(3, H * 22 // 512)
    for y in range(0, roof_h, seam_sp):
        _hline(buf, W, H, y, 0, W, 45, 45, 50)
    # Mortar joint emphasis (lighter thin lines)
    mj_sp = max(2, H * 12 // 512)
    for y in range(roof_h, H, mj_sp):
        _hline(buf, W, H, y, 0, W, 158, 138, 128)
    # Small punched windows: 2 cols x 3 rows
    ww = W * 48 // 512
    wh = ww
    sp_x = W * 160 // 512
    sp_y = H * 130 // 512
    for r in range(3):
        for c in range(2):
            wx = W * 80 // 512 + c * sp_x
            wy = roof_h + H * 18 // 512 + r * sp_y
            _draw_window(buf, W, H, wx, wy, ww, wh, (38, 40, 50),
                         frame_rgb=(88, 25, 22), frame_w=max(2, W * 4 // 512))
    # Recessed entrance with plain surround
    dw = W * 85 // 512
    dh = H * 88 // 512
    dx = (W - dw) // 2
    dy = H - dh - H // 15
    _fill_rect(buf, W, dx - 4, dy - 4, dx + dw + 4, dy + dh + 4, 95, 28, 25)
    _fill_rect(buf, W, dx, dy, dx + dw, dy + dh, 45, 32, 28)


def draw_res_med_01(buf, W, H):
    """2-storey block: flat parapet, external staircase hint, AC condensers, utilitarian."""
    base = (195, 172, 118)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    _brick_coursing(buf, W, H, 0, H, max(2, H * 10 // 512), (165, 142, 90), seed=201)
    # Flat parapet cap
    cap_h = H * 6 // 100
    _fill_rect(buf, W, 0, 0, W, cap_h, 155, 152, 145)
    # Concrete floor band at midpoint
    fb_h = max(2, H * 10 // 512)
    mid_y = H * 50 // 100
    _fill_rect(buf, W, 0, mid_y, W, mid_y + fb_h, 172, 172, 168)
    # Windows 4x4
    ww = W * 48 // 512
    wh = ww
    sp_x = W * 112 // 512
    sp_y = H * 110 // 512
    for r in range(4):
        for c in range(4):
            wx = W * 30 // 512 + c * sp_x
            wy = cap_h + H * 15 // 512 + r * sp_y
            _draw_window(buf, W, H, wx, wy, ww, wh, GLASS_DARK,
                         frame_rgb=(215, 208, 195), frame_w=max(1, W // 256))
    # External staircase hint: diagonal zigzag on right 15%
    st_x = W * 85 // 100
    st_w = W * 12 // 100
    _fill_rect(buf, W, st_x, cap_h, W, H, 178, 155, 105)
    step_h = max(4, H * 30 // 512)
    for y in range(cap_h, H, step_h * 2):
        for dy in range(step_h):
            sx = st_x + int(st_w * dy / max(1, step_h))
            if y + dy < H and sx < W:
                _hline(buf, W, H, y + dy, sx, min(sx + st_w - int(st_w * dy / max(1, step_h)), W), 148, 128, 85)
    # AC condenser boxes on parapet
    for i in range(3):
        ac_x = W * 30 // 512 + i * (W * 155 // 512)
        ac_w = W * 42 // 512
        ac_h = cap_h * 60 // 100
        _fill_rect(buf, W, ac_x, cap_h // 5, ac_x + ac_w, cap_h // 5 + ac_h, 162, 162, 158)
        # Louvre lines
        for ly in range(cap_h // 5, cap_h // 5 + ac_h, max(2, ac_h // 4)):
            _hline(buf, W, H, ly, ac_x + 1, ac_x + ac_w - 1, 135, 135, 132)
    _add_noise(buf, W, H, intensity=5, density=0.2, seed=1201)


def draw_res_med_02(buf, W, H):
    """2-storey villa: seafoam-green rendered render, wrap-around balcony stripe, wrought-iron accents."""
    base = (145, 185, 165)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Fine render texture
    sp = max(2, H * 22 // 512)
    for y in range(0, H, sp):
        _hline(buf, W, H, y, 0, W, 132, 172, 152)
    # White cornice at top 8%
    cor_h = H * 8 // 100
    _fill_rect(buf, W, 0, 0, W, cor_h, 235, 232, 228)
    # Wrap-around balcony stripe: full-width band at 50% height
    bal_y = H * 50 // 100
    bal_h = max(3, H * 28 // 512)
    _fill_rect(buf, W, 0, bal_y, W, bal_y + bal_h, 225, 222, 218)
    # Wrought-iron rail: dark line with small repeat bracket marks
    rail_y = bal_y + bal_h
    _hline(buf, W, H, rail_y, 0, W, 35, 32, 28)
    _hline(buf, W, H, rail_y + 1, 0, W, 35, 32, 28)
    bracket_sp = max(4, W * 32 // 512)
    for bx in range(0, W, bracket_sp):
        for dy in range(max(2, H * 12 // 512)):
            if rail_y + 2 + dy < H:
                buf[(rail_y + 2 + dy) * W + min(bx, W - 1)] = (35, 32, 28)
    # Windows 3x4, seafoam tinted frames
    ww = W * 52 // 512
    wh = ww
    sp_x = W * 148 // 512
    sp_y_step = H * 118 // 512
    for r in range(4):
        for c in range(3):
            wx = W * 50 // 512 + c * sp_x
            wy = cor_h + H * 15 // 512 + r * sp_y_step
            _draw_window(buf, W, H, wx, wy, ww, wh, (38, 42, 48),
                         frame_rgb=(112, 148, 128), frame_w=max(2, W * 3 // 512))
            # Sill planter
            sill_h = max(2, H * 7 // 512)
            _fill_rect(buf, W, wx, wy + wh + 1, wx + ww, wy + wh + 1 + sill_h, 75, 145, 90)
    _add_noise(buf, W, H, intensity=4, density=0.15, seed=1202)


def draw_res_med_03(buf, W, H):
    """2-storey cottage: warm brick, clay-tile hipped roof hint, chimney."""
    base = (178, 108, 62)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    _brick_coursing(buf, W, H, 0, H, max(2, H * 11 // 512), (145, 82, 42), seed=1203)
    # Clay-tile hipped roof hint at top 28%
    roof_h = H * 28 // 100
    tile_row = max(3, H * 16 // 512)
    for y in range(0, roof_h, tile_row):
        row_idx = y // tile_row
        col = (188, 82, 38) if (row_idx % 2 == 0) else (165, 65, 28)
        _fill_rect(buf, W, 0, y, W, y + tile_row, *col)
        _hline(buf, W, H, y, 0, W, 128, 48, 18)
        tw = max(4, W * 42 // 512)
        off = (tw // 2) if (row_idx % 2 == 1) else 0
        for x in range(off, W, tw):
            _vline(buf, W, H, x, y, min(y + tile_row, H), 128, 48, 18)
    # Chimney stack — centred, runs through roof
    ch_x = W * 220 // 512
    ch_w = max(3, W * 30 // 512)
    _fill_rect(buf, W, ch_x, 0, ch_x + ch_w, H * 40 // 100, 145, 72, 42)
    _brick_coursing(buf, W, H, 0, H * 40 // 100, max(2, H * 9 // 512), (115, 52, 28), seed=2203)
    # Chimney pot hint at very top
    _fill_rect(buf, W, ch_x + ch_w // 4, 0, ch_x + ch_w * 3 // 4, max(2, H * 12 // 512), 95, 55, 35)
    # Windows 3x2, warm brick frames
    ww, wh = W * 58 // 512, H * 65 // 512
    sp_x = W * 155 // 512
    sp_y = H * 140 // 512
    for r in range(2):
        for c in range(3):
            wx = W * 28 // 512 + c * sp_x
            wy = roof_h + H * 20 // 512 + r * sp_y
            _draw_window(buf, W, H, wx, wy, ww, wh, (42, 45, 52),
                         frame_rgb=(108, 68, 38), frame_w=max(2, W * 3 // 512))
            # Window box sill planter
            sill_h = max(2, H * 8 // 512)
            pl_clr = (165, 92, 55) if (r == 0) else (85, 148, 55)
            _fill_rect(buf, W, wx, wy + wh + 1, wx + ww, wy + wh + 1 + sill_h, *pl_clr)
    _add_noise(buf, W, H, intensity=6, density=0.25, seed=1203)


def draw_res_med_04(buf, W, H):
    """3-storey red-brick: dark red brick, pitched black metal roof hint, dormers implied."""
    base = (138, 42, 38)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    _brick_coursing(buf, W, H, 0, H, max(2, H * 11 // 512), (108, 28, 25), seed=1204)
    _add_noise(buf, W, H, intensity=7, density=0.28, seed=1204)
    # Pitched black metal roof at top 20%
    roof_h = H * 20 // 100
    _fill_rect(buf, W, 0, 0, W, roof_h, 38, 38, 42)
    # Roof seam lines
    seam_sp = max(3, H * 20 // 512)
    for y in range(0, roof_h, seam_sp):
        _hline(buf, W, H, y, 0, W, 25, 25, 30)
    # Dormer window hints: 2 small bright rectangles in roof band
    dor_w = W * 58 // 512
    dor_h = max(3, roof_h * 55 // 100)
    for di in range(2):
        dox = W * 90 // 512 + di * (W * 230 // 512)
        _fill_rect(buf, W, dox, roof_h // 4, dox + dor_w, roof_h // 4 + dor_h, 52, 52, 58)
        _fill_rect(buf, W, dox + 3, roof_h // 4 + 3, dox + dor_w - 3, roof_h // 4 + dor_h - 3, 38, 42, 52)
    # Windows 3x3 on main facade — small punched, light stone frames
    ww, wh = W * 55 // 512, H * 62 // 512
    sp_x = W * 148 // 512
    sp_y = H * 115 // 512
    for r in range(3):
        for c in range(3):
            wx = W * 45 // 512 + c * sp_x
            wy = roof_h + H * 15 // 512 + r * sp_y
            _draw_window(buf, W, H, wx, wy, ww, wh, (38, 42, 52),
                         frame_rgb=(185, 168, 148), frame_w=max(2, W * 4 // 512))
    # Stone string course at 2/3 height
    sc_y = roof_h + H * 65 // 100 * 2 // 3
    sc_h = max(2, H * 8 // 512)
    _fill_rect(buf, W, 0, sc_y, W, sc_y + sc_h, 192, 172, 148)
    _add_noise(buf, W, H, intensity=6, density=0.2, seed=2204)


def draw_res_high_01(buf, W, H):
    """Flat-roof concrete tower: grey smooth-rendered concrete, punched window grid, AC condensers."""
    base = (155, 155, 150)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Smooth render: subtle horizontal form-work lines
    sp = max(2, H * 18 // 512)
    for y in range(0, H, sp):
        _hline(buf, W, H, y, 0, W, 142, 142, 138)
    _add_noise(buf, W, H, intensity=5, density=0.18, seed=1301)
    # Flat parapet cap
    cap_h = H * 5 // 100
    _fill_rect(buf, W, 0, 0, W, cap_h, 135, 132, 128)
    # AC condenser row on parapet
    ac_w = W * 38 // 512
    ac_h = cap_h * 70 // 100
    for i in range(4):
        acx = W * 20 // 512 + i * (W * 118 // 512)
        _fill_rect(buf, W, acx, cap_h // 6, acx + ac_w, cap_h // 6 + ac_h, 172, 170, 165)
        for ly in range(cap_h // 6, cap_h // 6 + ac_h, max(2, ac_h // 4)):
            _hline(buf, W, H, ly, acx + 1, acx + ac_w - 1, 148, 145, 140)
    # Punched window grid: 5 cols x 8 rows — small square windows deep-set
    ww, wh = W * 52 // 512, H * 52 // 512
    sp_x = W * 92 // 512
    sp_y = H * 84 // 512
    for r in range(8):
        for c in range(5):
            wx = W * 22 // 512 + c * sp_x
            wy = cap_h + H * 12 // 512 + r * sp_y
            # Deep reveal (concrete surround 4px darker)
            _fill_rect(buf, W, wx - 4, wy - 4, wx + ww + 4, wy + wh + 4, 132, 130, 125)
            _draw_window(buf, W, H, wx, wy, ww, wh, (42, 46, 55))


def draw_res_high_02(buf, W, H):
    """Stepped-setback tower: warm beige precast, setback ledges, colonnade hint at base."""
    base = (205, 188, 158)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Precast panel texture: vertical joints at 80px, horizontal at 96px
    pv = max(2, W * 80 // 512)
    ph = max(2, H * 96 // 512)
    jw = max(1, W * 3 // 512)
    for x in range(0, W, pv):
        for dx in range(jw):
            _vline(buf, W, H, x + dx, 0, H, 115, 98, 72)
    for y in range(0, H, ph):
        for dy in range(jw):
            _hline(buf, W, H, y + dy, 0, W, 115, 98, 72)
    # Setback ledges at 33% and 66% height — wider section below each ledge
    for ledge_y in [H * 33 // 100, H * 66 // 100]:
        ledge_h = max(3, H * 14 // 512)
        _fill_rect(buf, W, 0, ledge_y, W, ledge_y + ledge_h, 188, 172, 142)
        # Shadow line above ledge
        _hline(buf, W, H, ledge_y - 1, 0, W, 145, 128, 100)
        _hline(buf, W, H, ledge_y + ledge_h, 0, W, 225, 210, 182)
    # Colonnade hint at base: repeated vertical column marks
    col_h = H * 15 // 100
    col_w = max(2, W * 12 // 512)
    col_sp = max(4, W * 68 // 512)
    for cx in range(0, W, col_sp):
        _fill_rect(buf, W, cx, H - col_h, cx + col_w, H, 178, 162, 132)
    # Windows 3x8 — set in precast
    ww, wh = W * 60 // 512, H * 52 // 512
    _draw_window_grid(buf, W, H, 3, 8, ww, wh, GLASS_DARK,
                      frame_rgb=(162, 148, 120), frame_w=max(1, W * 4 // 512))
    _add_noise(buf, W, H, intensity=4, density=0.18, seed=1302)


def draw_res_high_03(buf, W, H):
    """Curtain-wall residential tower: modern glass curtain wall, balcony slab bands."""
    base = (125, 148, 172)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Curtain-wall grid — floor height 56px, bay width 72px
    fh = max(2, H * 56 // 512)
    cv = max(2, W * 72 // 512)
    lw = max(1, W * 2 // 512)
    for y in range(0, H, fh):
        for dy in range(lw):
            _hline(buf, W, H, y + dy, 0, W, 55, 72, 92)
    for x in range(0, W, cv):
        for dx in range(lw):
            _vline(buf, W, H, x + dx, 0, H, 55, 72, 92)
    # Glass fill — slightly varying blue-grey tones per bay
    for y in range(0, H, fh):
        for x in range(0, W, cv):
            # Alternate warmer/cooler glass tones
            tone = 8 if ((y // fh + x // cv) % 2 == 0) else -5
            _fill_rect(buf, W, x + lw + 1, y + lw + 1,
                       min(x + cv - lw - 1, W), min(y + fh - lw - 1, H),
                       _clamp(138 + tone), _clamp(162 + tone), _clamp(188 + tone))
    # Balcony slab bands at every 3rd floor — full-width concrete strip
    slab_h = max(3, H * 12 // 512)
    for y in range(fh * 3, H, fh * 3):
        _fill_rect(buf, W, 0, y - slab_h, W, y, 188, 185, 180)
        _hline(buf, W, H, y - slab_h, 0, W, 145, 142, 138)
        _hline(buf, W, H, y, 0, W, 222, 218, 215)
    # Mechanical top 6%
    mh = H * 6 // 100
    _fill_rect(buf, W, 0, 0, W, mh, 105, 118, 135)
    _add_noise(buf, W, H, intensity=3, density=0.1, seed=1303)


def draw_res_high_04(buf, W, H):
    """Flat-fronted concrete tower: board-form concrete, horizontal spandrel bands, retail ground floor."""
    base = (148, 142, 135)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Board-form concrete: closely spaced horizontal plank lines
    plank_h = max(2, H * 8 // 512)
    for y in range(0, H, plank_h):
        offset = 3 if ((y // plank_h) % 3 == 0) else 0
        _hline(buf, W, H, y, offset, W, 128, 122, 115)
    _add_noise(buf, W, H, intensity=5, density=0.2, seed=1304)
    # Horizontal spandrel bands at each floor level (every 64px)
    sp_floor = max(2, H * 64 // 512)
    sp_band = max(2, H * 12 // 512)
    for y in range(sp_floor, H * 80 // 100, sp_floor):
        _fill_rect(buf, W, 0, y, W, y + sp_band, 168, 162, 155)
        _hline(buf, W, H, y, 0, W, 105, 100, 95)
        _hline(buf, W, H, y + sp_band, 0, W, 185, 178, 170)
    # Recessed loggia pockets: 3x2 deep recesses
    log_w = W * 70 // 512
    log_h = H * 48 // 512
    log_sp_x = W * 158 // 512
    log_sp_y = H * 130 // 512
    for r in range(2):
        for c in range(3):
            lx = W * 28 // 512 + c * log_sp_x
            ly = H * 5 // 100 + r * log_sp_y
            _fill_rect(buf, W, lx, ly, lx + log_w, ly + log_h, 105, 100, 95)
            # Window inside loggia
            _draw_window(buf, W, H, lx + 4, ly + 4, log_w - 8, log_h - 8,
                         (38, 42, 50))
    # Retail strip ground floor bottom 15% — glazed band
    ret_h = H * 15 // 100
    _fill_rect(buf, W, 0, H - ret_h, W, H, 115, 112, 108)
    # Glazed bays
    bay_w = W * 85 // 512
    for i in range(4):
        bx = W * 15 // 512 + i * (W * 120 // 512)
        _fill_rect(buf, W, bx, H - ret_h + 4, bx + bay_w, H - 4, 38, 42, 52)
        _fill_rect(buf, W, bx, H - ret_h, bx + bay_w, H - ret_h + 4, 88, 85, 80)


def draw_com_low_01(buf, W, H):
    """Convenience store: bright signage band at top, glazed shopfront, clean commercial finish."""
    base = (228, 228, 222)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    _add_noise(buf, W, H, intensity=3, density=0.1, seed=1401)
    # Bright signage band — top 22%, vivid orange-red
    sign_h = H * 22 // 100
    _fill_rect(buf, W, 0, 0, W, sign_h, 210, 62, 28)
    # White sign panel inset
    sp_w = W * 75 // 100
    sp_h = sign_h * 55 // 100
    _fill_rect(buf, W, (W - sp_w) // 2, sign_h * 20 // 100,
               (W + sp_w) // 2, sign_h * 20 // 100 + sp_h, 252, 248, 240)
    # Mid section: smooth white/cream render
    # Upper windows: 4 small punch-out windows
    ww, wh = W * 52 // 512, H * 48 // 512
    wy_upper = sign_h + H * 18 // 512
    for c in range(4):
        wx = W * 18 // 512 + c * (W * 118 // 512)
        _draw_window(buf, W, H, wx, wy_upper, ww, wh, GLASS_DARK,
                     frame_rgb=(195, 192, 185), frame_w=max(1, W // 256))
    # Glazed shopfront bottom 35%
    sf_y = H * 65 // 100
    _fill_rect(buf, W, 0, sf_y, W, H, 205, 202, 198)
    # Fascia band below sign
    fas_h = max(2, H * 10 // 512)
    _fill_rect(buf, W, 0, sf_y, W, sf_y + fas_h, 178, 175, 170)
    # Large glazed panel
    gw = W * 78 // 100
    gh = H - sf_y - fas_h - H * 5 // 100
    gx = (W - gw) // 2
    gy = sf_y + fas_h + 2
    _fill_rect(buf, W, gx, gy, gx + gw, gy + gh, 38, 42, 50)
    # Door opening
    dw = W * 55 // 512
    _fill_rect(buf, W, (W - dw) // 2, gy, (W + dw) // 2, H - 2, 28, 32, 40)


def draw_com_low_02(buf, W, H):
    """Café: warm brick/render, canvas awning colour band, wide window."""
    base = (188, 155, 105)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    _brick_coursing(buf, W, H, 0, H * 70 // 100, max(2, H * 11 // 512), (155, 122, 75), seed=1402)
    _add_noise(buf, W, H, intensity=5, density=0.2, seed=1402)
    # Canvas awning band — warm olive/ochre (distinct from com_low_01 orange-red)
    awn_y = H * 62 // 100
    awn_h = max(4, H * 32 // 512)
    _fill_rect(buf, W, 0, awn_y, W, awn_y + awn_h, 148, 128, 48)
    # Awning stripe pattern (diagonal lines)
    stripe_w = max(3, W * 20 // 512)
    for y in range(awn_y, awn_y + awn_h):
        for x in range(W):
            if ((x + (y - awn_y) * 2) // stripe_w) % 2 == 1:
                buf[y * W + x] = (168, 148, 65)
    # Wide café window (ground floor) — two large panes
    gf_y = awn_y + awn_h
    gf_h = H - gf_y - H * 4 // 100
    _fill_rect(buf, W, 0, gf_y, W, H, 172, 142, 92)
    pane_w = W * 42 // 100
    for i in range(2):
        px = W * 5 // 100 + i * (W * 50 // 100)
        _fill_rect(buf, W, px, gf_y + 4, px + pane_w, gf_y + gf_h, 35, 40, 52)
        # Window divider bar
        bar_x = px + pane_w // 2
        _vline(buf, W, H, bar_x, gf_y + 4, gf_y + gf_h, 135, 110, 75)
    # Upper windows: 2x2 with warm brick frames
    ww, wh = W * 62 // 512, H * 68 // 512
    for r in range(2):
        for c in range(2):
            wx = W * 60 // 512 + c * (W * 230 // 512)
            wy = H * 35 // 512 + r * (H * 130 // 512)
            _draw_window(buf, W, H, wx, wy, ww, wh, GLASS_DARK,
                         frame_rgb=(148, 115, 72), frame_w=max(2, W * 3 // 512))


def draw_com_low_03(buf, W, H):
    """Auto garage: corrugated metal walls, roll-up shutter doors, industrial character."""
    base = (132, 128, 122)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    _corrugation_lines(buf, W, H, 0, H, max(2, H * 10 // 512), base, amplitude=18)
    _add_noise(buf, W, H, intensity=6, density=0.25, seed=1403)
    # Gable shadow at top 15%
    gable_h = H * 15 // 100
    for y in range(gable_h):
        frac = y / max(1, gable_h)
        dark = int(28 * (1.0 - frac))
        for x in range(W):
            idx = y * W + x
            r, g, b = buf[idx]
            buf[idx] = (_clamp(r - dark), _clamp(g - dark), _clamp(b - dark))
    # Company name fascia band below gable — mid-grey
    fas_y = gable_h
    fas_h = max(3, H * 28 // 512)
    _fill_rect(buf, W, 0, fas_y, W, fas_y + fas_h, 88, 85, 80)
    # Roll-up shutter doors: 2 wide dark openings with horizontal ribs
    door_w = W * 38 // 100
    door_h = H * 52 // 100
    door_y = H - door_h - H * 3 // 100
    for di in range(2):
        dx = W * 4 // 100 + di * (W * 52 // 100)
        _fill_rect(buf, W, dx, door_y, dx + door_w, door_y + door_h, 45, 45, 48)
        # Shutter horizontal rib lines
        rib_sp = max(3, H * 18 // 512)
        for ry in range(door_y, door_y + door_h, rib_sp):
            _hline(buf, W, H, ry, dx + 2, dx + door_w - 2, 62, 62, 65)
        # Guide channel marks on sides
        _vline(buf, W, H, dx, door_y, door_y + door_h, 72, 70, 68)
        _vline(buf, W, H, dx + door_w - 1, door_y, door_y + door_h, 72, 70, 68)
    # Small office window above right shutter
    _draw_window(buf, W, H, W * 62 // 100, fas_y + fas_h + H * 10 // 512,
                 W * 60 // 512, H * 48 // 512, GLASS_DARK,
                 frame_rgb=(105, 102, 98), frame_w=max(1, W // 256))


def draw_com_low_04(buf, W, H):
    """Supermarket: large glazed shopfront, wide covered walkway canopy, white/light grey cladding."""
    base = (235, 235, 230)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    _add_noise(buf, W, H, intensity=3, density=0.1, seed=1404)
    # Light grey cladding panel joints
    ph = max(2, H * 96 // 512)
    pv = max(2, W * 128 // 512)
    for y in range(0, H, ph):
        _hline(buf, W, H, y, 0, W, 212, 210, 206)
    for x in range(0, W, pv):
        _vline(buf, W, H, x, 0, H, 212, 210, 206)
    # Blue corporate trim band at very top 8%
    trim_h = H * 8 // 100
    _fill_rect(buf, W, 0, 0, W, trim_h, 42, 82, 152)
    # Upper section small transom windows: 5 x 1
    ww, wh = W * 72 // 512, H * 42 // 512
    wy_u = trim_h + H * 12 // 512
    for c in range(5):
        wx = W * 12 // 512 + c * (W * 96 // 512)
        _draw_window(buf, W, H, wx, wy_u, ww, wh, GLASS_DARK,
                     frame_rgb=(200, 198, 195), frame_w=max(1, W // 256))
    # Wide covered walkway canopy at 60% height — full width overhang
    can_y = H * 58 // 100
    can_h = max(4, H * 22 // 512)
    _fill_rect(buf, W, 0, can_y, W, can_y + can_h, 188, 185, 180)
    # Canopy soffit shadow
    _hline(buf, W, H, can_y + can_h, 0, W, 155, 152, 148)
    _hline(buf, W, H, can_y + can_h + 1, 0, W, 168, 165, 160)
    # Support posts
    post_sp = max(4, W * 96 // 512)
    post_w = max(2, W * 8 // 512)
    for px in range(post_sp // 2, W, post_sp):
        _fill_rect(buf, W, px - post_w // 2, can_y + can_h, px + post_w // 2, H, 172, 168, 165)
    # Large glazed shopfront below canopy
    sf_y = can_y + can_h + 2
    gw = W - 2
    gh = H - sf_y - 2
    _fill_rect(buf, W, 1, sf_y, gw, sf_y + gh, 38, 42, 52)
    # Mullion dividers in shopfront
    mull_sp = max(4, W * 80 // 512)
    for mx in range(mull_sp, W, mull_sp):
        _vline(buf, W, H, mx, sf_y, sf_y + gh, 185, 182, 178)


def draw_com_med_01(buf, W, H):
    """Strip mall: plain commercial cladding, multiple fascia sign panels, large glazed shopfronts."""
    base = (210, 208, 202)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    _add_noise(buf, W, H, intensity=4, density=0.15, seed=1501)
    # Render horizontal bond lines
    sp = max(2, H * 38 // 512)
    for y in range(0, H, sp):
        _hline(buf, W, H, y, 0, W, 195, 192, 188)
    # Fascia sign panel band — top 28%, 4 distinct coloured sign bays
    fas_h = H * 28 // 100
    sign_colors = [(188, 32, 28), (28, 82, 168), (28, 128, 62), (188, 135, 22)]
    bay_w = W // 4
    for i, sc in enumerate(sign_colors):
        bx = i * bay_w
        _fill_rect(buf, W, bx, 0, bx + bay_w, fas_h, *sc)
        # White sign lettering band inset
        sb_h = max(2, fas_h * 35 // 100)
        _fill_rect(buf, W, bx + 4, fas_h * 30 // 100, bx + bay_w - 4,
                   fas_h * 30 // 100 + sb_h, 248, 245, 240)
        # Bay divider
        _vline(buf, W, H, bx, 0, fas_h, 165, 162, 158)
    # Separation band between fascia and shopfront
    sep_h = max(2, H * 12 // 512)
    _fill_rect(buf, W, 0, fas_h, W, fas_h + sep_h, 188, 185, 180)
    # Glazed shopfronts in 4 bays
    sf_y = fas_h + sep_h
    sf_h = H - sf_y - H * 5 // 100
    for i in range(4):
        bx = i * bay_w + 2
        _fill_rect(buf, W, bx, sf_y + 2, bx + bay_w - 4, sf_y + sf_h, 38, 42, 52)
        # Door in centre of each bay
        dw = bay_w * 22 // 100
        dh = sf_h * 70 // 100
        _fill_rect(buf, W, bx + (bay_w - dw) // 2, sf_y + sf_h - dh,
                   bx + (bay_w + dw) // 2, sf_y + sf_h, 28, 32, 42)


def draw_com_med_02(buf, W, H):
    """Boutique hotel: warm ochre render, juliet balcony railing bands, ornamental brackets."""
    base = (205, 165, 75)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Fine render texture
    sp = max(2, H * 25 // 512)
    for y in range(0, H, sp):
        _hline(buf, W, H, y, 0, W, 192, 152, 65)
    _add_noise(buf, W, H, intensity=5, density=0.2, seed=1502)
    # White/cream cornice top 10%
    cor_h = H * 10 // 100
    _fill_rect(buf, W, 0, 0, W, cor_h, 238, 232, 218)
    # Juliet balcony railing bands — full-width dark forged-iron stripe at each floor
    floor_h = H * 96 // 512
    rail_h = max(2, H * 10 // 512)
    for fy in range(cor_h + floor_h, H * 85 // 100, floor_h):
        # Railing band — dark wrought iron
        _fill_rect(buf, W, 0, fy, W, fy + rail_h, 35, 30, 25)
        # Rail posts (repeating narrow vertical marks)
        post_sp = max(3, W * 28 // 512)
        for px in range(0, W, post_sp):
            _vline(buf, W, H, px, fy - max(2, H * 15 // 512), fy, 35, 30, 25)
    # Ornamental bracket marks at corners of each floor (small L-shapes)
    brk_h = max(3, H * 14 // 512)
    brk_w = max(3, W * 12 // 512)
    for fy in range(cor_h + floor_h, H * 85 // 100, floor_h):
        for bx in [W * 8 // 512, W - W * 20 // 512]:
            _fill_rect(buf, W, bx, fy - brk_h, bx + brk_w, fy, 88, 72, 38)
            _fill_rect(buf, W, bx, fy - brk_h, bx + brk_w, fy - brk_h + 2, 108, 88, 48)
    # Windows 3x5, warm frames
    ww, wh = W * 58 // 512, H * 65 // 512
    _draw_window_grid(buf, W, H, 3, 5, ww, wh, GLASS_DARK,
                      start_y=cor_h + H * 12 // 512,
                      spacing_y=floor_h,
                      frame_rgb=(158, 122, 48), frame_w=max(2, W * 3 // 512))
    # Rusticated base: lower 12% slightly darker
    _fill_rect_blend(buf, W, H, 0, H * 88 // 100, W, H, 155, 118, 45, 0.3)


def draw_com_med_03(buf, W, H):
    """Corner bank: stone/limestone facade, pilaster rhythm, arched windows, cornice band."""
    base = (212, 202, 175)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    _add_noise(buf, W, H, intensity=4, density=0.15, seed=1503)
    # Limestone block coursing — horizontal joint lines
    course_h = max(2, H * 28 // 512)
    for y in range(0, H, course_h):
        _hline(buf, W, H, y, 0, W, 185, 175, 148)
    # Pilaster rhythm — 4 raised vertical strips
    pv = max(2, W * 110 // 512)
    pw = max(3, W * 14 // 512)
    for px in range(0, W, pv):
        _fill_rect(buf, W, px, 0, px + pw, H, 228, 218, 192)
        # Capital detail at top
        cap_h = max(2, H * 20 // 512)
        _fill_rect(buf, W, px - 2, 0, px + pw + 2, cap_h, 235, 225, 200)
    # Cornice band at 85%
    cy = H * 85 // 100
    ch = max(3, H * 22 // 512)
    _fill_rect(buf, W, 0, cy, W, cy + ch, 230, 220, 195)
    _hline(buf, W, H, cy, 0, W, 182, 172, 145)
    _hline(buf, W, H, cy + ch, 0, W, 182, 172, 145)
    # Arched windows: 3 cols x 4 rows — arch top above each window
    ww, wh = W * 62 // 512, H * 68 // 512
    sp_x = W * 158 // 512
    sp_y = H * 118 // 512
    for r in range(4):
        for c in range(3):
            wx = W * 42 // 512 + c * sp_x
            wy = H * 18 // 512 + r * sp_y
            arch_h = max(2, ww // 4)
            arch_w = ww * 7 // 10
            ax = wx + (ww - arch_w) // 2
            _fill_rect(buf, W, ax, wy - arch_h, ax + arch_w, wy, 38, 40, 52)
            _draw_window(buf, W, H, wx, wy, ww, wh, GLASS_DARK,
                         frame_rgb=(198, 188, 162), frame_w=max(2, W * 4 // 512))
    # Rusticated base lower 20%
    rb_h = H * 20 // 100
    _fill_rect(buf, W, 0, H - rb_h, W, H, 195, 185, 158)
    rsp = max(2, H * 28 // 512)
    for y in range(H - rb_h, H, rsp):
        _hline(buf, W, H, y, 0, W, 162, 152, 125)
        for vx in range(0, W, max(2, W * 80 // 512)):
            _vline(buf, W, H, vx, y, min(y + rsp, H), 162, 152, 125)


def draw_com_med_04(buf, W, H):
    """Office block: glass curtain-wall facade, flat roof with louvred parapet."""
    base = (105, 128, 155)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Curtain-wall grid — floor 64px, bay 80px
    fh = max(2, H * 64 // 512)
    cv = max(2, W * 80 // 512)
    lw = max(1, W * 2 // 512)
    for y in range(0, H, fh):
        for dy in range(lw):
            _hline(buf, W, H, y + dy, 0, W, 55, 72, 92)
    for x in range(0, W, cv):
        for dx in range(lw):
            _vline(buf, W, H, x + dx, 0, H, 55, 72, 92)
    # Glass fill — cool blue-grey
    for y in range(0, H, fh):
        for x in range(0, W, cv):
            _fill_rect(buf, W, x + lw + 1, y + lw + 1,
                       min(x + cv - lw - 1, W), min(y + fh - lw - 1, H),
                       120, 148, 178)
    # Spandrel band per floor — slightly opaque grey
    sp_h = max(1, H * 14 // 512)
    for y in range(fh - sp_h, H, fh):
        _fill_rect(buf, W, 0, y, W, y + sp_h, 82, 98, 118)
    # Louvred parapet at top 8% — alternating grey/light louvre slats
    par_h = H * 8 // 100
    _fill_rect(buf, W, 0, 0, W, par_h, 92, 105, 120)
    louv_sp = max(3, H * 8 // 512)
    for y in range(0, par_h, louv_sp):
        _hline(buf, W, H, y, 0, W, 135, 148, 165)
        _hline(buf, W, H, y + 1, 0, W, 68, 82, 100)
    _add_noise(buf, W, H, intensity=3, density=0.1, seed=1504)


def draw_com_high_01(buf, W, H):
    """Narrow glass tower with spire: silver steel + clear glass, slim vertical proportions."""
    base = (192, 198, 208)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Steel structure: vertical columns every 64px, floor bands every 48px
    fh = max(2, H * 48 // 512)
    cv = max(2, W * 64 // 512)
    col_w = max(2, W * 6 // 512)
    fb_h = max(1, H * 8 // 512)
    # Column strips — silver-grey
    for cx in range(0, W, cv):
        _fill_rect(buf, W, cx, 0, cx + col_w, H, 145, 150, 158)
    # Floor bands
    for fy in range(0, H, fh):
        _fill_rect(buf, W, 0, fy, W, fy + fb_h, 162, 168, 178)
    # Clear glass fill between columns
    for fy in range(0, H, fh):
        for cx in range(col_w, W, cv):
            _fill_rect(buf, W, cx, fy + fb_h,
                       min(cx + cv - col_w, W), min(fy + fh, H),
                       175, 192, 218)
    # Slim tower: narrow the facade at 75% height (setback implied — lighter zone)
    _fill_rect_blend(buf, W, H, 0, 0, W * 12 // 100, H * 75 // 100, 155, 162, 175, 0.5)
    _fill_rect_blend(buf, W, H, W * 88 // 100, 0, W, H * 75 // 100, 155, 162, 175, 0.5)
    # Spire feature at top 12% — narrow silver shaft
    sp_h = H * 12 // 100
    sp_w = max(3, W * 12 // 512)
    sp_x = (W - sp_w) // 2
    _fill_rect(buf, W, sp_x, 0, sp_x + sp_w, sp_h, 175, 180, 190)
    # Antenna tip
    tip_w = max(1, sp_w // 3)
    _fill_rect(buf, W, (W - tip_w) // 2, 0, (W + tip_w) // 2, sp_h // 3, 135, 140, 150)
    # Mechanical floor at sp_h
    _fill_rect(buf, W, 0, sp_h, W, sp_h + max(2, H * 16 // 512), 138, 142, 152)
    _add_noise(buf, W, H, intensity=3, density=0.1, seed=1601)


def draw_com_high_02(buf, W, H):
    """Wide slab tower: broader base steps back, antenna cluster at crown."""
    base = (165, 172, 185)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Deep blue glass curtain wall grid
    fh = max(2, H * 52 // 512)
    cv = max(2, W * 82 // 512)
    lw = max(1, W * 3 // 512)
    for y in range(0, H, fh):
        for dy in range(lw):
            _hline(buf, W, H, y + dy, 0, W, 72, 85, 108)
    for x in range(0, W, cv):
        for dx in range(lw):
            _vline(buf, W, H, x + dx, 0, H, 72, 85, 108)
    # Glass fill — dark navy-blue glass
    for y in range(0, H, fh):
        for x in range(0, W, cv):
            _fill_rect(buf, W, x + lw + 1, y + lw + 1,
                       min(x + cv - lw - 1, W), min(y + fh - lw - 1, H),
                       38, 62, 105)
    # Setback at 65% height — full-width ledge, narrower tower above
    setback_y = H * 65 // 100
    ledge_h = max(4, H * 18 // 512)
    _fill_rect(buf, W, 0, setback_y, W, setback_y + ledge_h, 148, 155, 168)
    _hline(buf, W, H, setback_y, 0, W, 105, 112, 128)
    _hline(buf, W, H, setback_y + ledge_h, 0, W, 195, 200, 215)
    # Upper section slightly inset (lighter sides)
    inset_w = W * 10 // 100
    _fill_rect_blend(buf, W, H, 0, 0, inset_w, setback_y, 125, 132, 148, 0.6)
    _fill_rect_blend(buf, W, H, W - inset_w, 0, W, setback_y, 125, 132, 148, 0.6)
    # Antenna cluster at crown — 3 thin vertical rods
    mh = H * 8 // 100
    _fill_rect(buf, W, 0, 0, W, mh, 115, 120, 132)
    ant_positions = [W * 30 // 100, W * 50 // 100, W * 68 // 100]
    ant_heights = [mh, mh * 70 // 100, mh * 50 // 100]
    for ax, ah in zip(ant_positions, ant_heights):
        ant_w = max(1, W * 3 // 512)
        _fill_rect(buf, W, ax - ant_w // 2, 0, ax + ant_w // 2, ah, 88, 92, 102)
    _add_noise(buf, W, H, intensity=3, density=0.1, seed=1602)


def draw_com_high_03(buf, W, H):
    """Tapered pyramid tower: floor plate reduces base to crown, chamfered corners."""
    base = (128, 155, 175)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Light steel-blue glass curtain wall grid
    fh = max(2, H * 52 // 512)
    cv = max(2, W * 72 // 512)
    lw = max(1, W * 2 // 512)
    for y in range(0, H, fh):
        for dy in range(lw):
            _hline(buf, W, H, y + dy, 0, W, 72, 92, 112)
    for x in range(0, W, cv):
        for dx in range(lw):
            _vline(buf, W, H, x + dx, 0, H, 72, 92, 112)
    # Glass fill
    for y in range(0, H, fh):
        for x in range(0, W, cv):
            _fill_rect(buf, W, x + lw + 1, y + lw + 1,
                       min(x + cv - lw - 1, W), min(y + fh - lw - 1, H),
                       148, 178, 205)
    # Pyramid taper: mask corners with progressively larger triangular darker zones
    # At each floor row from top, chamfer corners by 1 unit per floor
    taper_rate = max(1, W // (2 * max(1, H // fh)))
    for row in range(H // fh):
        y0 = row * fh
        y1 = min(y0 + fh, H)
        margin = row * taper_rate
        # Left and right chamfer darkening
        if margin > 0:
            _fill_rect_blend(buf, W, H, 0, y0, min(margin, W), y1, 88, 108, 128, 0.6)
            _fill_rect_blend(buf, W, H, max(0, W - margin), y0, W, y1, 88, 108, 128, 0.6)
    # Chamfered corner diagonal edge marks
    for y in range(H):
        margin = (y // max(1, fh)) * taper_rate
        if margin > 0 and margin < W // 2:
            if 0 <= margin < W:
                buf[y * W + margin] = (62, 80, 98)
            if W - margin - 1 >= 0:
                buf[y * W + W - margin - 1] = (62, 80, 98)
    # Crown point top 5%
    crown_h = H * 5 // 100
    crown_w = max(4, W * 15 // 100)
    _fill_rect(buf, W, (W - crown_w) // 2, 0, (W + crown_w) // 2, crown_h, 108, 128, 148)
    _add_noise(buf, W, H, intensity=3, density=0.1, seed=1603)


def draw_com_high_04(buf, W, H):
    """Stepped ziggurat tower: horizontal ledges at every setback step, 4+ step levels."""
    base = (155, 148, 138)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Concrete/stone cladding texture
    sp = max(2, H * 20 // 512)
    for y in range(0, H, sp):
        _hline(buf, W, H, y, 0, W, 138, 132, 122)
    _add_noise(buf, W, H, intensity=4, density=0.15, seed=1604)
    # 5 setback steps from base to crown
    n_steps = 5
    step_h = H // n_steps
    step_inset = W // (n_steps * 2)  # horizontal inset per step
    for step in range(n_steps):
        y0 = step * step_h
        y1 = min(y0 + step_h, H)
        inset = step * step_inset
        # Full ledge at the top of each step section
        ledge_h = max(4, H * 16 // 512)
        ledge_y = y1 - ledge_h
        if ledge_y >= 0 and ledge_y < H:
            _fill_rect(buf, W, inset, ledge_y, W - inset, ledge_y + ledge_h, 175, 168, 158)
            _hline(buf, W, H, ledge_y, inset, W - inset, 118, 112, 102)
            _hline(buf, W, H, ledge_y + ledge_h - 1, inset, W - inset, 205, 198, 188)
        # Window grid in this step band
        step_win_h = H * 45 // 512
        step_win_w = W * 48 // 512
        band_inner = y0 + H * 10 // 512
        band_outer = ledge_y - H * 8 // 512 if ledge_y > 0 else y1
        if band_outer > band_inner and (W - 2 * inset) > step_win_w:
            usable_w = W - 2 * inset
            n_wins = max(1, usable_w // (step_win_w + W * 12 // 512))
            win_sp = usable_w // n_wins
            for c in range(n_wins):
                wx = inset + c * win_sp + (win_sp - step_win_w) // 2
                wy = band_inner
                _draw_window(buf, W, H, wx, wy, step_win_w, step_win_h,
                             GLASS_DARK, frame_rgb=(125, 118, 108),
                             frame_w=max(1, W // 256))


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
    """Brick workshop: plain dark brick, flat parapet, roller-shutter entrance."""
    base = (88, 68, 58)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    _brick_coursing(buf, W, H, 0, H, max(2, H * 12 // 512), (68, 50, 42), seed=1702)
    _add_noise(buf, W, H, intensity=7, density=0.3, seed=1702)
    # Flat concrete parapet cap
    cap_h = H * 7 // 100
    _fill_rect(buf, W, 0, 0, W, cap_h, 118, 115, 110)
    # Roller-shutter entrance — centred, bottom 55%
    rs_w = W * 62 // 100
    rs_h = H * 55 // 100
    rs_x = (W - rs_w) // 2
    rs_y = H - rs_h
    _fill_rect(buf, W, rs_x, rs_y, rs_x + rs_w, H, 42, 40, 38)
    # Shutter horizontal rib lines
    rib_sp = max(3, H * 16 // 512)
    for ry in range(rs_y, H, rib_sp):
        _hline(buf, W, H, ry, rs_x + 2, rs_x + rs_w - 2, 58, 56, 54)
    # Guide channel on sides
    gc_w = max(2, W * 8 // 512)
    _fill_rect(buf, W, rs_x - gc_w, rs_y, rs_x, H, 72, 68, 65)
    _fill_rect(buf, W, rs_x + rs_w, rs_y, rs_x + rs_w + gc_w, H, 72, 68, 65)
    # Small windows either side of shutter
    ww, wh = W * 52 // 512, H * 45 // 512
    win_y = cap_h + H * 15 // 512
    for wx in [W * 15 // 512, W - W * 67 // 512]:
        _draw_window(buf, W, H, wx, win_y, ww, wh, (38, 40, 48),
                     frame_rgb=(62, 48, 40), frame_w=max(1, W // 256))


def draw_ind_low_03(buf, W, H):
    """Sawtooth factory: ochre/yellow brick, sawtooth roof shadow bands, chimney stack."""
    base = (195, 172, 92)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    _brick_coursing(buf, W, H, 0, H, max(2, H * 13 // 512), (165, 142, 68), seed=1703)
    _add_noise(buf, W, H, intensity=5, density=0.2, seed=1703)
    # Sawtooth roof shadow bands at top 28% — alternating bright/shadow triangles
    st_h = H * 28 // 100
    n_teeth = 3
    tooth_w = W // n_teeth
    for i in range(n_teeth):
        tx = i * tooth_w
        # Shadow slope on right side of each tooth
        for y in range(st_h):
            frac = y / max(1, st_h)
            shadow_start = tx + int(tooth_w * frac)
            shadow_end = tx + tooth_w
            for x in range(max(tx, shadow_start), min(shadow_end, W)):
                idx = y * W + x
                r, g, b = buf[idx]
                buf[idx] = (_clamp(r - 28), _clamp(g - 25), _clamp(b - 18))
        # Sawtooth ridge line
        for y in range(st_h):
            frac = y / max(1, st_h)
            ridge_x = tx + int(tooth_w * frac)
            if 0 <= ridge_x < W:
                buf[y * W + ridge_x] = (155, 130, 55)
    # Chimney stack — right side, runs from top
    ch_x = W * 72 // 100
    ch_w = max(3, W * 26 // 512)
    _fill_rect(buf, W, ch_x, 0, ch_x + ch_w, H * 45 // 100, 152, 132, 68)
    _brick_coursing(buf, W, H, 0, H * 45 // 100, max(2, H * 10 // 512), (122, 102, 48), seed=2703)
    # Chimney top cap
    cap_h = max(2, H * 12 // 512)
    _fill_rect(buf, W, ch_x - 2, 0, ch_x + ch_w + 2, cap_h, 105, 98, 55)
    # Pilasters on facade
    pv = max(2, W * 128 // 512)
    pw = max(1, W * 8 // 512)
    for x in range(0, W, pv):
        _fill_rect(buf, W, x, st_h, x + pw, H, 172, 148, 78)
    # Concrete dado lower 12%
    dh = H * 12 // 100
    _fill_rect(buf, W, 0, H - dh, W, H, 165, 162, 155)


def draw_ind_low_04(buf, W, H):
    """Storage yard gatehouse: plain concrete block finish, security fence hint."""
    base = (162, 158, 148)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Concrete block coursing: horizontal joints every 32px, vertical at 48px
    bh = max(2, H * 32 // 512)
    bv = max(2, W * 48 // 512)
    joint_clr = (138, 134, 124)
    for y in range(0, H, bh):
        _hline(buf, W, H, y, 0, W, *joint_clr)
        row_idx = y // bh
        offset = (bv // 2) if (row_idx % 2 == 1) else 0
        for x in range(offset, W, bv):
            _vline(buf, W, H, x, y, min(y + bh, H), *joint_clr)
    _add_noise(buf, W, H, intensity=5, density=0.2, seed=1704)
    # Flat parapet cap
    cap_h = H * 8 // 100
    _fill_rect(buf, W, 0, 0, W, cap_h, 145, 140, 132)
    # Security fence hint: chain-link pattern strip at top 15% (above cap)
    # Represented as regular diagonal cross-hatch
    fence_h = H * 15 // 100
    _fill_rect(buf, W, 0, 0, W, fence_h, 118, 115, 108)
    diag_sp = max(3, W * 18 // 512)
    for y in range(0, fence_h):
        for x in range(W):
            if (x + y) % diag_sp == 0 or (x - y) % diag_sp == 0:
                buf[y * W + x] = (88, 85, 80)
    # Small windows: 2x2 punched openings
    ww, wh = W * 55 // 512, H * 50 // 512
    sp_x = W * 180 // 512
    for r in range(2):
        for c in range(2):
            wx = W * 55 // 512 + c * sp_x
            wy = fence_h + H * 18 // 512 + r * (H * 130 // 512)
            _draw_window(buf, W, H, wx, wy, ww, wh, (38, 40, 48),
                         frame_rgb=(128, 124, 115), frame_w=max(1, W // 256))
    # Plain entrance door
    dw, dh = W * 75 // 512, H * 90 // 512
    dx = (W - dw) // 2
    dy = H - dh - H * 4 // 100
    _fill_rect(buf, W, dx, dy, dx + dw, dy + dh, 88, 85, 80)
    _fill_rect(buf, W, dx + 2, dy + 2, dx + dw - 2, dy + dh - 2, 68, 65, 62)


def draw_ind_med_01(buf, W, H):
    """Flat-roof factory: concrete with horizontal loading bay openings, two chimney stacks."""
    base = (158, 155, 148)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Smooth concrete form-work lines
    sp = max(2, H * 16 // 512)
    for y in range(0, H, sp):
        _hline(buf, W, H, y, 0, W, 142, 138, 132)
    _add_noise(buf, W, H, intensity=5, density=0.2, seed=1801)
    # Flat parapet cap
    cap_h = H * 6 // 100
    _fill_rect(buf, W, 0, 0, W, cap_h, 138, 135, 128)
    # Two chimney stacks rising above cap
    for ch_cx in [W * 25 // 100, W * 72 // 100]:
        ch_w = max(4, W * 28 // 512)
        _fill_rect(buf, W, ch_cx - ch_w // 2, 0, ch_cx + ch_w // 2, H * 35 // 100, 118, 115, 108)
        # Stack flue cap
        _fill_rect(buf, W, ch_cx - ch_w // 2 - 2, H * 3 // 100,
                   ch_cx + ch_w // 2 + 2, H * 3 // 100 + max(2, H * 8 // 512), 95, 92, 88)
    # Horizontal loading bay strip — bottom 35%, 3 openings
    bay_y = H * 65 // 100
    bay_h = H * 30 // 100
    _fill_rect(buf, W, 0, bay_y, W, H, 140, 138, 132)
    bay_w = W * 26 // 100
    for i in range(3):
        bx = W * 5 // 100 + i * (W * 32 // 100)
        _fill_rect(buf, W, bx, bay_y + H * 8 // 512, bx + bay_w, H - H * 4 // 100, 42, 40, 38)
        # Canopy above each opening
        _fill_rect(buf, W, bx - 2, bay_y, bx + bay_w + 2, bay_y + H * 8 // 512, 120, 118, 112)
    # Windows row above bays: 5 small punched
    ww, wh = W * 45 // 512, H * 42 // 512
    wy = cap_h + H * 18 // 512
    for c in range(5):
        wx = W * 15 // 512 + c * (W * 95 // 512)
        _draw_window(buf, W, H, wx, wy, ww, wh, GLASS_DARK,
                     frame_rgb=(125, 122, 115), frame_w=max(1, W // 256))


def draw_ind_med_02(buf, W, H):
    """Steel-frame warehouse: exposed structural steel corner columns, wide corrugated cladding."""
    base = (178, 175, 168)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    _corrugation_lines(buf, W, H, 0, H, max(2, H * 14 // 512), base, amplitude=12)
    _add_noise(buf, W, H, intensity=4, density=0.15, seed=1802)
    # Exposed structural steel columns — 4 vertical strips
    col_w = max(4, W * 18 // 512)
    col_positions = [0, W * 32 // 100 - col_w // 2, W * 66 // 100 - col_w // 2, W - col_w]
    for cx in col_positions:
        _fill_rect(buf, W, cx, 0, cx + col_w, H, 125, 122, 118)
        # Column bolt detail lines every floor
        fb_sp = max(3, H * 64 // 512)
        bolt_h = max(2, H * 8 // 512)
        for fy in range(0, H, fb_sp):
            _fill_rect(buf, W, cx - 2, fy, cx + col_w + 2, fy + bolt_h, 108, 105, 100)
    # Steel floor tie beams: horizontal flat bands at each floor
    beam_h = max(2, H * 10 // 512)
    fb_sp = max(2, H * 96 // 512)
    for fy in range(fb_sp, H, fb_sp):
        _fill_rect(buf, W, 0, fy, W, fy + beam_h, 138, 135, 128)
    # Wide loading door — two bays, bottom 45%
    door_h = H * 45 // 100
    for i in range(2):
        dx = col_positions[i + 1] + col_w
        dw = col_positions[i + 2] - dx
        _fill_rect(buf, W, dx, H - door_h, dx + dw, H, 42, 40, 38)
        rib_sp = max(3, H * 14 // 512)
        for ry in range(H - door_h, H, rib_sp):
            _hline(buf, W, H, ry, dx + 2, dx + dw - 2, 58, 56, 54)
    # Eave gutter line
    gut_y = H * 8 // 100
    _fill_rect(buf, W, 0, gut_y, W, gut_y + max(2, H * 10 // 512), 145, 142, 135)


def draw_ind_med_03(buf, W, H):
    """Brick mill: dark red brick, multi-pane industrial windows, arched lintels."""
    base = (105, 45, 38)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    _brick_coursing(buf, W, H, 0, H, max(2, H * 11 // 512), (82, 32, 28), seed=1803)
    _add_noise(buf, W, H, intensity=7, density=0.28, seed=1803)
    # Stone quoin corners: lighter columns at left and right edges
    qw = max(3, W * 20 // 512)
    _fill_rect(buf, W, 0, 0, qw, H, 148, 128, 108)
    _fill_rect(buf, W, W - qw, 0, W, H, 148, 128, 108)
    # Stone string course at 35% and 70% height
    for sc_y in [H * 35 // 100, H * 70 // 100]:
        sc_h = max(2, H * 12 // 512)
        _fill_rect(buf, W, 0, sc_y, W, sc_y + sc_h, 165, 145, 118)
        _hline(buf, W, H, sc_y, 0, W, 128, 108, 88)
        _hline(buf, W, H, sc_y + sc_h, 0, W, 188, 168, 142)
    # Multi-pane industrial windows: 3 cols x 3 rows, arched lintel above each
    ww, wh = W * 68 // 512, H * 72 // 512
    sp_x = W * 158 // 512
    sp_y = H * 120 // 512
    for r in range(3):
        for c in range(3):
            wx = qw + W * 18 // 512 + c * sp_x
            wy = H * 18 // 512 + r * sp_y
            # Arched lintel (stone arch above)
            arch_h = max(2, ww // 4)
            arch_w = ww
            _fill_rect(buf, W, wx, wy - arch_h, wx + arch_w, wy, 148, 128, 105)
            # Inner arch cutout
            inner_arch_w = arch_w * 7 // 10
            _fill_rect(buf, W, wx + (arch_w - inner_arch_w) // 2, wy - arch_h + 2,
                       wx + (arch_w + inner_arch_w) // 2, wy, 78, 35, 28)
            # Multi-pane window: 3 panes across, 2 rows
            pane_w = (ww - 4) // 3
            pane_h = (wh - 2) // 2
            _fill_rect(buf, W, wx, wy, wx + ww, wy + wh, 62, 28, 22)
            for pr in range(2):
                for pc in range(3):
                    px = wx + 1 + pc * (pane_w + 1)
                    py = wy + 1 + pr * (pane_h + 1)
                    _fill_rect(buf, W, px, py, px + pane_w, py + pane_h, 38, 42, 52)
    # Parapet cap
    cap_h = H * 6 // 100
    _fill_rect(buf, W, 0, 0, W, cap_h, 128, 108, 88)


def draw_ind_med_04(buf, W, H):
    """Distribution centre: white steel cladding, loading dock canopy bands, dock openings."""
    base = (225, 225, 220)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    _corrugation_lines(buf, W, H, 0, H, max(2, H * 10 // 512), base, amplitude=6)
    _add_noise(buf, W, H, intensity=3, density=0.1, seed=1804)
    # Horizontal steel cladding rail lines (wide rivet-band style)
    band_sp = max(3, H * 56 // 512)
    band_h = max(2, H * 8 // 512)
    for fy in range(band_sp, H, band_sp):
        _fill_rect(buf, W, 0, fy, W, fy + band_h, 195, 192, 188)
        _hline(buf, W, H, fy, 0, W, 175, 172, 168)
        _hline(buf, W, H, fy + band_h, 0, W, 235, 232, 228)
    # Loading dock section: bottom 40% — yellow dock-canopy band + dock openings
    dock_y = H * 60 // 100
    can_h = max(3, H * 18 // 512)
    # Dock canopy stripe: safety yellow-green
    _fill_rect(buf, W, 0, dock_y, W, dock_y + can_h, 188, 185, 52)
    _hline(buf, W, H, dock_y, 0, W, 155, 152, 35)
    _hline(buf, W, H, dock_y + can_h, 0, W, 215, 210, 80)
    # 3 dock opening bays
    dock_bay_w = W * 27 // 100
    dock_bay_h = H - dock_y - can_h - H * 3 // 100
    for i in range(3):
        dbx = W * 3 // 100 + i * (W * 32 // 100)
        dby = dock_y + can_h
        _fill_rect(buf, W, dbx, dby, dbx + dock_bay_w, dby + dock_bay_h, 42, 40, 38)
        # Dock leveler plate at base
        lev_h = max(2, H * 12 // 512)
        _fill_rect(buf, W, dbx, dby + dock_bay_h - lev_h,
                   dbx + dock_bay_w, dby + dock_bay_h, 88, 85, 80)
    # Upper windows: 5 small punch-outs
    ww, wh = W * 52 // 512, H * 42 // 512
    wy = H * 8 // 100
    for c in range(5):
        wx = W * 15 // 512 + c * (W * 96 // 512)
        _draw_window(buf, W, H, wx, wy, ww, wh, GLASS_DARK,
                     frame_rgb=(195, 192, 185), frame_w=max(1, W // 256))


def draw_ind_high_01(buf, W, H):
    """Concrete tower: board-form concrete, small punched windows, safety stripes at base."""
    base = (145, 142, 135)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Board-form concrete: dense horizontal plank lines
    plank_h = max(2, H * 10 // 512)
    for y in range(0, H, plank_h):
        _hline(buf, W, H, y, 0, W, 125, 122, 115)
    _add_noise(buf, W, H, intensity=5, density=0.2, seed=1901)
    # Tie-hole pattern: regular grid of small dark spots
    tie_sp_x = max(4, W * 48 // 512)
    tie_sp_y = max(4, H * 32 // 512)
    tie_r = max(1, W * 3 // 512)
    for ty in range(tie_sp_y // 2, H * 85 // 100, tie_sp_y):
        for tx in range(tie_sp_x // 2, W, tie_sp_x):
            for dy in range(-tie_r, tie_r + 1):
                for dx in range(-tie_r, tie_r + 1):
                    if dx * dx + dy * dy <= tie_r * tie_r:
                        py2 = ty + dy
                        px2 = tx + dx
                        if 0 <= py2 < H and 0 <= px2 < W:
                            buf[py2 * W + px2] = (105, 102, 95)
    # Small punched windows: 3 cols x 5 rows, very deep reveals
    ww, wh = W * 45 // 512, H * 45 // 512
    sp_x = W * 148 // 512
    sp_y = H * 108 // 512
    for r in range(5):
        for c in range(3):
            wx = W * 38 // 512 + c * sp_x
            wy = H * 5 // 100 + r * sp_y
            _fill_rect(buf, W, wx - 5, wy - 5, wx + ww + 5, wy + wh + 5, 112, 108, 102)
            _draw_window(buf, W, H, wx, wy, ww, wh, (38, 40, 48))
    # Safety stripes at base (bottom 12%): diagonal hazard band
    base_y = H * 88 // 100
    stripe_w = max(3, W * 22 // 512)
    for y in range(base_y, H):
        for x in range(W):
            si = ((x + (y - base_y) * 2) // stripe_w) % 2
            buf[y * W + x] = (210, 188, 25) if si == 0 else (35, 35, 35)
    # Chimney stacks: 2 narrow stacks rising from top
    for ch_cx in [W * 20 // 100, W * 78 // 100]:
        ch_w = max(3, W * 18 // 512)
        _fill_rect(buf, W, ch_cx - ch_w // 2, 0, ch_cx + ch_w // 2, H * 20 // 100, 112, 108, 102)


def draw_ind_high_02(buf, W, H):
    """Exposed steel-frame refinery: structural steel grid, pipe run accents, spherical vessel hint."""
    base = (62, 68, 78)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Exposed steel structural grid — bold silver-grey frame
    fh = max(2, H * 80 // 512)
    fv = max(2, W * 96 // 512)
    fw = max(3, W * 8 // 512)
    frame_clr = (148, 152, 158)
    for y in range(0, H, fh):
        for dy in range(fw):
            _hline(buf, W, H, y + dy, 0, W, *frame_clr)
    for x in range(0, W, fv):
        for dx in range(fw):
            _vline(buf, W, H, x + dx, 0, H, *frame_clr)
    # Panel fill between frame — varied dark metal tones
    rng = random.Random(1902)
    for y in range(0, H, fh):
        for x in range(0, W, fv):
            d = rng.randint(-10, 10)
            _fill_rect(buf, W, x + fw, y + fw,
                       min(x + fv - fw, W), min(y + fh - fw, H),
                       _clamp(62 + d), _clamp(68 + d), _clamp(78 + d))
    # Pipe run accents: 2 horizontal pipe runs crossing mid-facade
    pipe_positions = [H * 33 // 100, H * 66 // 100]
    pipe_h = max(4, H * 18 // 512)
    for pipe_y in pipe_positions:
        _fill_rect(buf, W, 0, pipe_y, W, pipe_y + pipe_h, 165, 115, 62)
        # Pipe flange marks
        fl_sp = max(4, W * 62 // 512)
        fl_h = max(3, pipe_h + 4)
        for fx in range(fl_sp // 2, W, fl_sp):
            _fill_rect(buf, W, fx - 2, pipe_y - 2, fx + 3, pipe_y + pipe_h + 2, 142, 98, 48)
    # Spherical pressure vessel hint: right-centre zone
    v_cx = W * 72 // 100
    v_cy = H * 50 // 100
    v_r = max(8, W * 28 // 512)
    for dy in range(-v_r, v_r + 1):
        for dx in range(-v_r, v_r + 1):
            if dx * dx + dy * dy <= v_r * v_r:
                px2 = v_cx + dx
                py2 = v_cy + dy
                if 0 <= px2 < W and 0 <= py2 < H:
                    # Lighting: lighter on upper-left
                    light = int(25 * (1.0 - (dx + dy) / (2 * v_r)))
                    buf[py2 * W + px2] = (_clamp(118 + light), _clamp(122 + light), _clamp(128 + light))
    _add_noise(buf, W, H, intensity=4, density=0.15, seed=1902)


def draw_ind_high_03(buf, W, H):
    """Cylindrical silo cluster: 3 round corrugated silos with curved shading and access ladder."""
    base = (108, 95, 80)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    _add_noise(buf, W, H, intensity=4, density=0.15, seed=1903)
    # 3 cylindrical silo forms side by side
    n_silos = 3
    silo_w = W * 28 // 100
    silo_gap = (W - n_silos * silo_w) // (n_silos + 1)
    for i in range(n_silos):
        sx = silo_gap + i * (silo_w + silo_gap)
        # Base colour varies slightly between silos
        silo_base = [
            (112, 98, 82),
            (102, 90, 75),
            (118, 102, 85),
        ][i]
        _fill_rect(buf, W, sx, H * 15 // 100, sx + silo_w, H, *silo_base)
        # Horizontal corrugation rings
        ring_sp = max(3, H * 20 // 512)
        for ry in range(H * 15 // 100, H, ring_sp):
            _hline(buf, W, H, ry, sx, sx + silo_w, _clamp(silo_base[0] - 18),
                   _clamp(silo_base[1] - 18), _clamp(silo_base[2] - 18))
        # Cylindrical shading: darker on right half
        for y in range(H * 15 // 100, H):
            for x in range(sx, min(sx + silo_w, W)):
                frac = (x - sx) / max(1, silo_w)
                dark = int(32 * frac)
                idx = y * W + x
                r2, g2, b2 = buf[idx]
                buf[idx] = (_clamp(r2 - dark), _clamp(g2 - dark), _clamp(b2 - dark))
        # Dome top
        dome_h = H * 12 // 100
        dome_y = H * 15 // 100 - dome_h
        if dome_y >= 0:
            _fill_rect(buf, W, sx, dome_y, sx + silo_w, H * 15 // 100, _clamp(silo_base[0] + 12),
                       _clamp(silo_base[1] + 10), _clamp(silo_base[2] + 8))
            # Dome arc silhouette
            for dx2 in range(silo_w):
                x = sx + dx2
                frac = (dx2 - silo_w // 2) / max(1, silo_w // 2)
                arc_y = H * 15 // 100 - dome_h + int(dome_h * frac * frac)
                if 0 <= x < W and 0 <= arc_y < H:
                    buf[arc_y * W + x] = (_clamp(silo_base[0] - 22), _clamp(silo_base[1] - 22),
                                          _clamp(silo_base[2] - 22))
        # Access ladder on first silo
        if i == 0:
            lx = sx + silo_w // 3
            for y in range(H * 12 // 100, H):
                if 0 <= lx < W:
                    buf[y * W + lx] = (55, 48, 38)
                rung_sp = max(2, H * 18 // 512)
                if y % rung_sp < 2:
                    for ddx in range(-3, 4):
                        if 0 <= lx + ddx < W:
                            buf[y * W + lx + ddx] = (55, 48, 38)


def draw_ind_high_04(buf, W, H):
    """Refinery tower: grating/deck bands, hazard stripe posts, louvre panels, flare stack hint."""
    base = (72, 70, 65)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    _add_noise(buf, W, H, intensity=4, density=0.15, seed=1904)
    # Grating/deck platform bands — every ~80px, full width
    deck_sp = max(3, H * 80 // 512)
    deck_h = max(4, H * 16 // 512)
    for dy in range(deck_h, H, deck_sp):
        _fill_rect(buf, W, 0, dy, W, dy + deck_h, 92, 88, 82)
        # Grating texture: diagonal hatch lines
        grate_sp = max(2, W * 12 // 512)
        for y in range(dy, dy + deck_h):
            for x in range(W):
                if (x + (y - dy) * 2) % grate_sp == 0:
                    buf[y * W + x] = (62, 60, 56)
        # Deck edge handrail top line
        _hline(buf, W, H, dy, 0, W, 115, 110, 105)
        _hline(buf, W, H, dy + deck_h - 1, 0, W, 62, 60, 56)
    # Louvre panel sections: left 30% — vertical dark slat bands
    louv_w = W * 30 // 100
    louv_slat = max(3, W * 10 // 512)
    for x in range(0, louv_w, louv_slat * 2):
        _fill_rect(buf, W, x, 0, x + louv_slat, H, 48, 46, 42)
    # Hazard stripe posts: 3 vertical striped pillars
    post_positions = [W * 38 // 100, W * 58 // 100, W * 78 // 100]
    post_w = max(4, W * 14 // 512)
    for px2 in post_positions:
        for y in range(H):
            stripe = (y // max(3, H * 20 // 512)) % 2
            clr = (210, 188, 28) if stripe == 0 else (35, 35, 35)
            for dx2 in range(post_w):
                if 0 <= px2 + dx2 < W:
                    buf[y * W + px2 + dx2] = clr
    # Flare stack hint: narrow right-edge column with glow at top
    flare_x = W - max(4, W * 12 // 512) - W * 4 // 100
    flare_w = max(4, W * 12 // 512)
    _fill_rect(buf, W, flare_x, 0, flare_x + flare_w, H, 88, 85, 80)
    # Glow zone at flare tip
    glow_h = max(4, H * 25 // 512)
    _fill_rect(buf, W, flare_x - 4, 0, flare_x + flare_w + 4, glow_h, 210, 138, 38)
    _fill_rect(buf, W, flare_x - 2, 0, flare_x + flare_w + 2, glow_h // 2, 235, 172, 65)
    # Pipe runs: 2 horizontal orange pipes
    for pipe_y in [H * 30 // 100, H * 62 // 100]:
        ph2 = max(3, H * 12 // 512)
        _fill_rect(buf, W, louv_w, pipe_y, flare_x, pipe_y + ph2, 175, 108, 45)
        fl_sp = max(4, W * 55 // 512)
        for fx in range(louv_w + fl_sp // 2, flare_x, fl_sp):
            _fill_rect(buf, W, fx - 2, pipe_y - 2, fx + 3, pipe_y + ph2 + 2, 145, 85, 32)


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


def draw_ground_garden(buf, W, H):
    """Ground feature cell (5,1): mid-green grass/garden patch."""
    base = (80, 130, 60)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Subtle grass-blade noise (vertical streaks)
    _add_noise(buf, W, H, intensity=12, density=0.4, seed=5101)
    # Darker horizontal grass-blade lines for texture
    rng = random.Random(5102)
    for _ in range(W * H // 20):
        bx = rng.randint(0, W - 1)
        by = rng.randint(0, H - 1)
        blade_h = rng.randint(2, 6)
        dark = rng.randint(40, 70)
        for dy in range(blade_h):
            if by + dy < H:
                r, g, b = buf[(by + dy) * W + bx]
                buf[(by + dy) * W + bx] = (_clamp(r - dark // 3), _clamp(g - dark // 4), _clamp(b - dark // 2))


def draw_ground_pool(buf, W, H):
    """Ground feature cell (5,2): pool-blue water."""
    base = (70, 160, 200)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Faint tile-grid lines (lighter lines every ~32 px)
    grid_spacing = max(16, W // 16)
    line_rgb = (90, 180, 220)
    for x in range(0, W, grid_spacing):
        _vline(buf, W, H, x, 0, H, *line_rgb)
    for y in range(0, H, grid_spacing):
        _hline(buf, W, H, y, 0, W, *line_rgb)
    # Subtle ripple noise
    _add_noise(buf, W, H, intensity=8, density=0.2, seed=5201)


def draw_ground_paving(buf, W, H):
    """Ground feature cell (5,3): light-grey concrete forecourt."""
    base = (190, 185, 178)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Faint paving joint lines (darker lines every ~64 px)
    joint_spacing = max(32, W // 8)
    joint_rgb = (155, 150, 144)
    for x in range(0, W, joint_spacing):
        _vline(buf, W, H, x, 0, H, *joint_rgb)
    for y in range(0, H, joint_spacing):
        _hline(buf, W, H, y, 0, W, *joint_rgb)
    _add_noise(buf, W, H, intensity=6, density=0.25, seed=5301)


def draw_ground_tarmac(buf, W, H):
    """Ground feature cell (5,4): dark asphalt tarmac."""
    base = (55, 55, 58)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Subtle aggregate texture (random lighter/darker specks)
    _add_noise(buf, W, H, intensity=10, density=0.5, seed=5401)
    # Occasional coarse aggregate specks
    rng = random.Random(5402)
    for _ in range(W * H // 80):
        bx = rng.randint(0, W - 1)
        by = rng.randint(0, H - 1)
        speck = rng.randint(15, 30)
        r, g, b = buf[by * W + bx]
        buf[by * W + bx] = (_clamp(r + speck), _clamp(g + speck), _clamp(b + speck))


def draw_ground_gravel(buf, W, H):
    """Ground feature cell (5,5): beige/tan gravel."""
    base = (180, 165, 130)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    # Irregular gravel speckle noise
    _add_noise(buf, W, H, intensity=20, density=0.6, seed=5501)
    # Coarse gravel chunks (small rectangles)
    rng = random.Random(5502)
    for _ in range(W * H // 50):
        bx = rng.randint(0, W - 3)
        by = rng.randint(0, H - 3)
        cw = rng.randint(2, 5)
        ch = rng.randint(2, 4)
        dr = rng.randint(-25, 25)
        for dy in range(ch):
            for dx in range(cw):
                if by + dy < H and bx + dx < W:
                    r, g, b = buf[(by + dy) * W + (bx + dx)]
                    buf[(by + dy) * W + (bx + dx)] = (_clamp(r + dr), _clamp(g + dr * 9 // 10), _clamp(b + dr * 7 // 10))


def draw_solid_wall_brick(buf, W, H):
    """Solid wall brick cell (5,6): plain warm terracotta brick with NO windows or door.
    Used for gable-end triangles on gabled buildings so the triangular face shows
    plain brick coursing rather than clipped window geometry from a wall cell.
    Base colour matches draw_res_low_01 (RGB 168, 100, 65)."""
    base = (168, 100, 65)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    _brick_coursing(buf, W, H, 0, H, max(2, H * 12 // 512), (135, 78, 48), seed=5601)
    _add_noise(buf, W, H, intensity=6, density=0.25, seed=5602)


def draw_res_low_02_door(buf, W, H):
    """Res_low_02 front-door face (6,0): cream wall + centred door + 2 flanking arched windows.
    Used only for the FRONT face of the left unit of the semi-detached pair so the door
    appears on exactly one wall. All other faces use draw_res_low_02 (no door).
    Wall geometry matches res_low_02: 4S wide × 5S tall."""
    base = (238, 232, 215)
    _fill_rect(buf, W, 0, 0, W, H, *base)
    sp = max(2, H * 28 // 512)
    for y in range(0, H, sp):
        _hline(buf, W, H, y, 0, W, 228, 222, 205)

    # Door centred at floor level (top of PNG)
    dw = W * 100 // 512
    dh = H * 270 // 512
    dx = (W - dw) // 2
    dy = H * 8 // 512
    _fill_rect(buf, W, dx, dy, dx + dw, dy + dh, 100, 65, 25)
    _fill_rect(buf, W, dx + 2, dy + 2, dx + dw - 2, dy + dh - 2, 75, 45, 15)

    # 2 arched windows flanking door, synced with res_low_01 (4S×6S wall)
    ww = W * 80 // 512
    wh = ww * 2 // 3
    gap = W * 28 // 512
    wx_left  = dx - gap - ww
    wx_right = dx + dw + gap
    wy = H * 180 // 512   # mid-wall, matching plain cell
    for wx in (wx_left, wx_right):
        arch_h = max(2, wh // 5)
        arch_w = ww * 7 // 10
        arch_x = wx + (ww - arch_w) // 2
        _fill_rect(buf, W, arch_x, wy - arch_h, arch_x + arch_w, wy, 38, 40, 48)
        _draw_window(buf, W, H, wx, wy, ww, wh, (42, 45, 55),
                     frame_rgb=(55, 48, 42), frame_w=max(2, W * 4 // 512))
        sill_h = max(2, H * 8 // 512)
        _fill_rect(buf, W, wx, wy + wh + 2, wx + ww, wy + wh + 2 + sill_h, 185, 95, 60)

    dado_h = H * 10 // 100
    _fill_rect(buf, W, 0, H - dado_h, W, H, 62, 55, 48)
    _add_noise(buf, W, H, intensity=4, density=0.15, seed=6001)


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
    (5, 1): draw_ground_garden,
    (5, 2): draw_ground_pool,
    (5, 3): draw_ground_paving,
    (5, 4): draw_ground_tarmac,
    (5, 5): draw_ground_gravel,
    (5, 6): draw_solid_wall_brick,
    (6, 0): draw_res_low_02_door,
    (6, 1): draw_res_low_03_door,
}

RESERVED_COLOR = (72, 72, 72)

# ---------------------------------------------------------------------------
# Cell-to-asset-name mapping (for PLY-first detection)
# ---------------------------------------------------------------------------
# Maps atlas (row, col) → building asset name so the generator can detect
# which cells have a PLY lod0 file committed and should be preserved from the
# converter-written atlas PNG rather than regenerated procedurally.
CELL_ASSET_NAMES = {
    (0, 0): 'res_low_01',    (0, 1): 'res_low_02',    (0, 2): 'res_low_03',    (0, 3): 'res_low_04',
    (0, 4): 'res_med_01',    (0, 5): 'res_med_02',    (0, 6): 'res_med_03',    (0, 7): 'res_med_04',
    (1, 0): 'res_high_01',   (1, 1): 'res_high_02',   (1, 2): 'res_high_03',   (1, 3): 'res_high_04',
    (1, 4): 'com_low_01',    (1, 5): 'com_low_02',    (1, 6): 'com_low_03',    (1, 7): 'com_low_04',
    (2, 0): 'com_med_01',    (2, 1): 'com_med_02',    (2, 2): 'com_med_03',    (2, 3): 'com_med_04',
    (2, 4): 'com_high_01',   (2, 5): 'com_high_02',   (2, 6): 'com_high_03',   (2, 7): 'com_high_04',
    (3, 0): 'ind_low_01',    (3, 1): 'ind_low_02',    (3, 2): 'ind_low_03',    (3, 3): 'ind_low_04',
    (3, 4): 'ind_med_01',    (3, 5): 'ind_med_02',    (3, 6): 'ind_med_03',    (3, 7): 'ind_med_04',
    (4, 0): 'ind_high_01',   (4, 1): 'ind_high_02',   (4, 2): 'ind_high_03',   (4, 3): 'ind_high_04',
    (4, 4): 'svc_fire_station',  (4, 5): 'svc_police_station',
    (4, 6): 'svc_power_plant',   (4, 7): 'svc_water_tower',
}


def _load_atlas_png_4k(png_path):
    """Load the 4096×4096 atlas PNG as a flat list of (r,g,b) tuples.

    The converter (convert_tripo3d_to_ply.py) bakes Tripo3D textures in Blender's
    Y-up array at (7-R)*512, but Blender flips on save, so row R lands at PNG
    y=R*512 (Y-normal).  Irrlicht loads PNG without flipping (V=0 = top-left of
    the image), so PLY UV V=[R/8,(R+1)/8] samples PNG y=[R*512,(R+1)*512].
    Returns None if the file does not exist or cannot be loaded.
    """
    if not os.path.exists(png_path):
        return None
    try:
        from PIL import Image
        img = Image.open(png_path).convert('RGB')
        if img.size != (4096, 4096):
            print(f"  WARNING: atlas PNG is {img.size}, expected (4096,4096) — skipping PLY cell preservation")
            return None
        return list(img.getdata())
    except Exception as exc:
        print(f"  WARNING: could not load atlas PNG for PLY cell preservation: {exc}")
        return None


# ---------------------------------------------------------------------------
# Render the full atlas into a flat pixel buffer
# ---------------------------------------------------------------------------

def render_atlas_pixels(atlas_w, atlas_h, png_4k_pixels=None, ply_cells=None):
    """
    Render the full atlas into a flat list of (r, g, b) tuples.

    For cells in ``ply_cells`` where ``png_4k_pixels`` is available the pixels
    are copied from the existing 4096×4096 atlas PNG (written by the PLY
    converter) rather than generated procedurally.  The converter stores row R
    at PNG y=(GRID_ROWS-1-R)*cell_size (Y-flipped), so this function un-flips
    when copying into the generator's Y-normal pixel buffer.

    For all other cells the draw function from CELL_DRAW_FNS is used.
    """
    cell_w = atlas_w // GRID_COLS
    cell_h = atlas_h // GRID_ROWS
    PNG_CELL = 4096 // GRID_COLS   # 512 — native cell size in converter PNG

    # Initialize to reserved color
    pixels = [RESERVED_COLOR] * (atlas_w * atlas_h)

    for row in range(GRID_ROWS):
        for col in range(GRID_COLS):
            # Atlas Y convention: Y-normal — row R is stored at y=R*cell_h in the PNG.
            # The converter bakes at Blender array y=(7-R)*512 but Blender flips on
            # save, placing row R at PNG y=R*512.  Irrlicht loads PNG such that UV
            # V=0 maps to the top-left of the image (PNG y=0), so PLY UV
            # V=[R/8,(R+1)/8] correctly samples PNG y=[R*cell_h,(R+1)*cell_h).
            x0 = col * cell_w
            y0 = row * cell_h

            # --- PLY cell: copy real Tripo3D texture from converter PNG ---
            if ply_cells and (row, col) in ply_cells and png_4k_pixels is not None:
                # Converter stores row R at PNG y = row*PNG_CELL (Y-normal after
                # Blender's flip-on-save).  Read from the same Y-normal position.
                src_y0 = row * PNG_CELL
                src_x0 = col * PNG_CELL
                for cy in range(cell_h):
                    src_cy = int(cy * PNG_CELL / cell_h)   # scale if atlas_w != 4096
                    for cx in range(cell_w):
                        src_cx = int(cx * PNG_CELL / cell_w)
                        src_idx = (src_y0 + src_cy) * 4096 + (src_x0 + src_cx)
                        pixels[(y0 + cy) * atlas_w + (x0 + cx)] = png_4k_pixels[src_idx]
                print(f"    Cell ({row},{col}): copied from atlas PNG (PLY asset: {CELL_ASSET_NAMES.get((row,col))})")
                continue

            fn = CELL_DRAW_FNS.get((row, col))
            if fn is None:
                # Reserved cell: fill with dark grey
                _fill_rect(pixels, atlas_w, x0, y0, x0 + cell_w, y0 + cell_h, *RESERVED_COLOR)
                continue

            # Render cell into a temporary buffer at cell resolution
            cell_buf = [RESERVED_COLOR] * (cell_w * cell_h)
            fn(cell_buf, cell_w, cell_h)

            # Copy cell buffer into atlas at Y-normal position
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


def build_dds_header_dx10_bc1_srgb(width, height, mip_levels):
    """Build a 148-byte DDS header with DX10 extension for BC1_UNORM_SRGB.

    Layout: 4-byte magic + 124-byte DDS_HEADER + 20-byte DDS_HEADER_DXT10 = 148 bytes.
    DXGI_FORMAT_BC1_UNORM_SRGB = 72.
    """
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
    # FourCC = 'DX10' to signal DX10 extended header
    hdr += struct.pack('<II4sIIIII',
        32, DDPF_FOURCC, b'DX10', 0, 0, 0, 0, 0)
    hdr += struct.pack('<IIII', caps1, 0, 0, 0)
    hdr += struct.pack('<I', 0)  # dwReserved2

    assert len(hdr) == 124

    # DDS_HEADER_DXT10 (20 bytes):
    #   dxgiFormat        = 72  (DXGI_FORMAT_BC1_UNORM_SRGB)
    #   resourceDimension = 3   (D3D10_RESOURCE_DIMENSION_TEXTURE2D)
    #   miscFlag          = 0
    #   arraySize         = 1
    #   miscFlags2        = 0
    dx10 = struct.pack('<IIIII', 72, 3, 0, 1, 0)
    assert len(dx10) == 20

    return magic + hdr + dx10


# ---------------------------------------------------------------------------
# DDS generation from pixel buffer with proper mip chain
# ---------------------------------------------------------------------------

def generate_dds_dx10_bc1_srgb_from_pixels(output_path, pixels, width, height, mip_levels):
    """Generate a DX10/BC1_UNORM_SRGB DDS file from a pixel buffer with mip chain.

    Produces a 148-byte header (DDS_HEADER + DX10 extension) followed by BC1 pixel data.
    DXGI_FORMAT = 72 (BC1_UNORM_SRGB).  Use for sRGB diffuse atlases.
    """
    header = build_dds_header_dx10_bc1_srgb(width, height, mip_levels)
    assert len(header) == 148

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
# Vehicle Atlas Generator
#
# Reads vehicle assignments from vehicle_atlas_registry.json, extracts
# basecolor textures from Tripo3D ZIPs, and produces:
#   assets/textures/vehicles/vehicles_diffuse_atlas_d.dds  (2048x2048, 4 mips, DX10/BC1_UNORM_SRGB)
#   assets/textures/vehicles/vehicles_diffuse_atlas_d.png  (source PNG)
#
# Y-normal convention: row R is stored at PNG y = R * cell_size.
# Irrlicht samples V=0 = top of PNG, matching remap_uvs_to_atlas's V-flip.
# ---------------------------------------------------------------------------

VEHICLE_ATLAS_GRID = 4   # 4×4 grid
VEHICLE_CELL_SIZE  = 512  # px per cell at 2048×2048
VEHICLE_ATLAS_SIZE = 2048


def generate_vehicle_atlas(repo_root):
    """Generate vehicles_diffuse_atlas_d.dds and .png from Tripo3D ZIPs."""
    import json
    import zipfile
    import io

    vehicles_dir  = os.path.join(repo_root, 'assets', 'textures', 'vehicles')
    zip_dir       = os.path.join(repo_root, 'assets', 'tripo3d', 'medium_poly', 'vehicles')
    vehicles_3d   = os.path.join(repo_root, 'assets', '3d', 'vehicles')
    registry_path = os.path.join(repo_root, 'tools', 'vehicle_atlas_registry.json')
    out_dds       = os.path.join(vehicles_dir, 'vehicles_diffuse_atlas_d.dds')
    out_png       = os.path.join(vehicles_dir, 'vehicles_diffuse_atlas_d.png')

    print("\n=== Vehicle Diffuse Atlas DDS Generator ===\n")

    # Load assignments from registry
    with open(registry_path) as f:
        registry = json.load(f)
    assignments = registry.get('assignments', [])

    # Build atlas pixels (RGBA kept as RGB list)
    atlas_w = atlas_h = VEHICLE_ATLAS_SIZE
    cell = VEHICLE_CELL_SIZE
    pixels = [RESERVED_COLOR] * (atlas_w * atlas_h)

    ok, skipped = [], []
    for entry in assignments:
        vid  = entry['vehicle_id']
        row  = entry['row']
        col  = entry['col']
        ply  = os.path.join(vehicles_3d, f'{vid}_lod0.ply')
        zpath = os.path.join(zip_dir, f'{vid}.zip')

        if not os.path.exists(ply):
            skipped.append(f'{vid} (no PLY)')
            continue

        if not os.path.exists(zpath):
            skipped.append(f'{vid} (no ZIP)')
            continue

        try:
            with zipfile.ZipFile(zpath) as zf:
                bc_name = next((n for n in zf.namelist()
                                if 'basecolor' in n.lower()
                                and n.lower().endswith(('.jpg', '.jpeg', '.png'))), None)
                if bc_name is None:
                    skipped.append(f'{vid} (no basecolor in ZIP)')
                    continue
                bc_data = zf.read(bc_name)

            from PIL import Image as PILImage
            cell_img = PILImage.open(io.BytesIO(bc_data)).convert('RGB')
            cell_img = cell_img.resize((cell, cell), PILImage.LANCZOS)
            cell_pixels = list(cell_img.getdata())

            # Y-normal: row R at atlas y = R * cell
            x0 = col * cell
            y0 = row * cell
            for cy in range(cell):
                for cx in range(cell):
                    pixels[(y0 + cy) * atlas_w + (x0 + cx)] = cell_pixels[cy * cell + cx]

            print(f"  ({row},{col}) {vid}: baked from ZIP")
            ok.append(vid)
        except Exception as exc:
            skipped.append(f'{vid} (error: {exc})')

    print(f"\n  Baked: {len(ok)}, Skipped: {len(skipped)}")
    if skipped:
        for s in skipped:
            print(f"    SKIP: {s}")

    # Generate DDS (2048×2048, 4 mip levels, DX10/BC1_UNORM_SRGB — required by check_25)
    print(f"\nGenerating {out_dds}")
    generate_dds_dx10_bc1_srgb_from_pixels(out_dds, pixels, atlas_w, atlas_h, 4)
    print(f"  {os.path.getsize(out_dds):,} bytes")

    # Save source PNG
    print(f"Generating {out_png}")
    generate_source_png(out_png, pixels, atlas_w, atlas_h)
    print(f"  {os.path.getsize(out_png):,} bytes")

    print("\n=== Vehicle atlas generation complete ===")


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

    # ----- Detect PLY cells and load existing atlas PNG -----
    # Cells with a committed PLY lod0 file carry real Tripo3D textures baked by
    # convert_tripo3d_to_ply.py.  Preserve those cells instead of overwriting
    # them with procedural draw functions.
    buildings_3d = os.path.join(repo_root, 'assets', '3d', 'buildings')
    ply_cells = set()
    for (r, c), name in CELL_ASSET_NAMES.items():
        if os.path.exists(os.path.join(buildings_3d, f'{name}_lod0.ply')):
            ply_cells.add((r, c))
    if ply_cells:
        print(f"PLY cells detected ({len(ply_cells)}): {sorted(ply_cells)}")
    else:
        print("No PLY cells detected — all cells will be generated procedurally.")

    png_4k_pixels = None
    if ply_cells:
        png_4k_pixels = _load_atlas_png_4k(png_path)
        if png_4k_pixels is None:
            print("  WARNING: atlas PNG not available — PLY cells will fall back to procedural.")
            ply_cells = set()

    # ----- Step 1: Render at 4096x4096 -----
    print("\nRendering 4096x4096 atlas pixels...")
    pixels_4k = render_atlas_pixels(4096, 4096, png_4k_pixels=png_4k_pixels, ply_cells=ply_cells)
    print(f"  Total pixels: {len(pixels_4k):,}")

    # ----- Step 2: Generate primary DDS (4096x4096, 5 mips) -----
    print(f"\nGenerating primary atlas: {primary_path}")
    primary_total = generate_dds_from_pixels(primary_path, pixels_4k, 4096, 4096, 5)

    # ----- Step 3: Render at 2048x2048 for fallback -----
    print("\nRendering 2048x2048 atlas pixels...")
    pixels_2k = render_atlas_pixels(2048, 2048, png_4k_pixels=png_4k_pixels, ply_cells=ply_cells)
    print(f"  Total pixels: {len(pixels_2k):,}")

    # ----- Step 4: Generate fallback DDS (2048x2048, 4 mips) -----
    print(f"\nGenerating fallback atlas: {fallback_path}")
    fallback_total = generate_dds_from_pixels(fallback_path, pixels_2k, 2048, 2048, 4)

    # ----- Step 5: Source PNG (4096x4096) for Check #28 and converter compatibility -----
    # Saved at 4096x4096 (512x512 per cell) so convert_tripo3d_to_ply.py can
    # write baked textures into it correctly (cell_px = 4096 // ATLAS_GRID = 512).
    print(f"\nGenerating source PNG: {png_path}")
    generate_source_png(png_path, pixels_4k, 4096, 4096)

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

    # ----- Vehicle atlas -----
    print()
    generate_vehicle_atlas(repo_root)
