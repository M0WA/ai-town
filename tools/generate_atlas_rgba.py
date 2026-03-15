#!/usr/bin/env python3
"""
generate_atlas_rgba.py — Regenerate buildings_atlas_d.dds as uncompressed RGBA8.

The DXT1 atlas produced by generate_production_textures.py fails to load on
devcontainer hardware that lacks GL_EXT_texture_compression_s3tc.  This script
produces an identical 2048x2048 atlas with the same procedural cell content but
writes it as an uncompressed RGBA8 DDS that Irrlicht always supports via its
standard IVideoDriver::getTexture() path.

No ImageMagick subprocess is required — pixel data is written directly with
Python's struct module.

Run from workspace root:
    python tools/generate_atlas_rgba.py

Overwrites:
    assets/textures/buildings/buildings_atlas_d.dds
"""

import os
import struct
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
ROOT = Path(__file__).resolve().parent.parent
OUT_PATH = ROOT / "assets" / "textures" / "buildings" / "buildings_atlas_d.dds"

# ---------------------------------------------------------------------------
# Uncompressed RGBA8 DDS writer
#
# Format layout (all little-endian):
#   "DDS " magic (4 bytes)
#   DDS_HEADER (124 bytes):
#     dwSize            = 124
#     dwFlags           = DDSD_CAPS|DDSD_HEIGHT|DDSD_WIDTH|DDSD_PITCH|DDSD_PIXELFORMAT = 0x1007
#     dwHeight, dwWidth = 2048, 2048
#     dwPitchOrLinearSize = width * 4 = 8192  (bytes per row for uncompressed)
#     dwDepth           = 0
#     dwMipMapCount     = 1
#     dwReserved1[11]   = 0 * 11  (44 bytes)
#     DDS_PIXELFORMAT (32 bytes):
#       dwSize    = 32
#       dwFlags   = DDPF_ALPHAPIXELS|DDPF_RGB = 0x41
#       dwFourCC  = 0  (not compressed)
#       dwRGBBitCount = 32
#       dwRBitMask  = 0x000000FF  (R in byte 0)
#       dwGBitMask  = 0x0000FF00  (G in byte 1)
#       dwBBitMask  = 0x00FF0000  (B in byte 2)
#       dwABitMask  = 0xFF000000  (A in byte 3)
#     dwCaps    = DDSCAPS_TEXTURE = 0x1000
#     dwCaps2,3,4 = 0
#     dwReserved2 = 0
#   Pixel data: width * height * 4 bytes, RGBA, top-to-bottom
# ---------------------------------------------------------------------------

def write_rgba8_dds(rgba_image: Image.Image, out_path: Path) -> None:
    """Write a PIL RGBA image as an uncompressed RGBA8 DDS file."""
    width, height = rgba_image.size
    assert rgba_image.mode == "RGBA", f"Expected RGBA image, got {rgba_image.mode}"

    DDSD_CAPS        = 0x1
    DDSD_HEIGHT      = 0x2
    DDSD_WIDTH       = 0x4
    DDSD_PITCH       = 0x8
    DDSD_PIXELFORMAT = 0x1000
    dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT

    pitch = width * 4  # bytes per row for 32-bit uncompressed

    DDPF_ALPHAPIXELS = 0x1
    DDPF_RGB         = 0x40
    pf_flags = DDPF_ALPHAPIXELS | DDPF_RGB

    DDSCAPS_TEXTURE = 0x1000

    header = struct.pack(
        "<4sIIIIIII",
        b"DDS ",       # magic
        124,           # dwSize (DDS_HEADER size)
        dwFlags,
        height,
        width,
        pitch,         # dwPitchOrLinearSize
        0,             # dwDepth
        1,             # dwMipMapCount
    )
    # dwReserved1[11] — 44 bytes of zeros
    header += b"\x00" * 44

    # DDS_PIXELFORMAT (32 bytes)
    pixelformat = struct.pack(
        "<IIIIIII",
        32,            # dwSize
        pf_flags,      # dwFlags
        0,             # dwFourCC (0 = not compressed)
        32,            # dwRGBBitCount
        0x000000FF,    # dwRBitMask  — R at byte 0
        0x0000FF00,    # dwGBitMask  — G at byte 1
        0x00FF0000,    # dwBBitMask  — B at byte 2
    )
    # dwABitMask is the 8th field; struct only has 7 above, append separately
    pixelformat += struct.pack("<I", 0xFF000000)  # dwABitMask — A at byte 3
    assert len(pixelformat) == 32, f"pixelformat size mismatch: {len(pixelformat)}"

    # Caps (5 × uint32 = 20 bytes)
    caps = struct.pack("<IIIII",
        DDSCAPS_TEXTURE,  # dwCaps
        0,                # dwCaps2
        0,                # dwCaps3
        0,                # dwCaps4
        0,                # dwReserved2
    )

    full_header = header + pixelformat + caps
    # DDS magic (4) + DDS_HEADER (124) = 128 bytes total
    assert len(full_header) == 128, f"full_header length mismatch: {len(full_header)}"

    pixel_data = rgba_image.tobytes()  # RGBA, top-to-bottom row order
    assert len(pixel_data) == width * height * 4

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(full_header)
        f.write(pixel_data)


# ---------------------------------------------------------------------------
# Cell painter helpers (ported from generate_production_textures.py)
# ---------------------------------------------------------------------------

CELL   = 512
BORDER = 8
USABLE = CELL - 2 * BORDER   # 496


def _add_noise(arr: np.ndarray, amount: int = 8) -> np.ndarray:
    rng = np.random.default_rng(seed=42)
    noise = rng.integers(-amount, amount + 1, size=arr.shape[:2] + (3,), dtype=np.int16)
    result = arr.copy()
    result[..., :3] = np.clip(arr[..., :3].astype(np.int16) + noise, 0, 255).astype(np.uint8)
    return result


def _base_cell(bg: tuple) -> Image.Image:
    arr = np.full((CELL, CELL, 4), (*bg, 255), dtype=np.uint8)
    arr = _add_noise(arr, 6)
    return Image.fromarray(arr, "RGBA")


def _draw_windows(draw, cell_x, cell_y, rows, cols, win_w, win_h,
                  frame_color, glass_color, margin_top=40, margin_bottom=20):
    x0 = cell_x + BORDER
    y0 = cell_y + BORDER
    usable_w = USABLE
    usable_h = USABLE - margin_top - margin_bottom
    col_step = usable_w // cols
    row_step = usable_h // rows
    for r in range(rows):
        for c in range(cols):
            wx = x0 + c * col_step + (col_step - win_w) // 2
            wy = y0 + margin_top + r * row_step + (row_step - win_h) // 2
            draw.rectangle([wx - 3, wy - 3, wx + win_w + 2, wy + win_h + 2],
                           fill=frame_color)
            draw.rectangle([wx, wy, wx + win_w, wy + win_h], fill=glass_color)


def _draw_brick_rows(img, cell_x, cell_y, base_color,
                     mortar_color=(180, 168, 150), brick_h=24, mortar_h=3):
    draw = ImageDraw.Draw(img)
    rng = np.random.default_rng(seed=7)
    brick_w = 64
    y = cell_y + BORDER
    row = 0
    while y < cell_y + CELL - BORDER:
        draw.rectangle([cell_x + BORDER, y,
                        cell_x + CELL - BORDER - 1, y + mortar_h - 1],
                       fill=mortar_color)
        y += mortar_h
        if y >= cell_y + CELL - BORDER:
            break
        offset = (brick_w // 2) if (row % 2 == 1) else 0
        x = cell_x + BORDER - offset
        while x < cell_x + CELL - BORDER:
            br = int(np.clip(base_color[0] + rng.integers(-10, 11), 0, 255))
            bg = int(np.clip(base_color[1] + rng.integers(-8,  9),  0, 255))
            bb = int(np.clip(base_color[2] + rng.integers(-6,  7),  0, 255))
            bx0 = max(x, cell_x + BORDER)
            bx1 = min(x + brick_w - 2, cell_x + CELL - BORDER - 1)
            draw.rectangle([bx0, y, bx1, y + brick_h - 1], fill=(br, bg, bb))
            x += brick_w
        y += brick_h
        row += 1


def _draw_corrugated(img, cell_x, cell_y, base_color, line_spacing=12):
    draw = ImageDraw.Draw(img)
    light = tuple(min(c + 20, 255) for c in base_color)
    dark  = tuple(max(c - 20, 0) for c in base_color)
    y = cell_y + BORDER
    toggle = False
    while y < cell_y + CELL - BORDER:
        color = light if toggle else dark
        draw.line([(cell_x + BORDER, y),
                   (cell_x + CELL - BORDER - 1, y)],
                  fill=color, width=2)
        y += line_spacing
        toggle = not toggle


def _draw_curtain_wall(img, cell_x, cell_y, base_color,
                       mullion_w=4, bay_w=62, floor_h=80):
    draw = ImageDraw.Draw(img)
    spandrel   = tuple(max(c - 30, 0) for c in base_color)
    glass_light = tuple(min(c + 40, 255) for c in base_color)
    y = cell_y + BORDER
    while y < cell_y + CELL - BORDER:
        draw.rectangle([cell_x + BORDER, y,
                        cell_x + CELL - BORDER - 1, y + 8],
                       fill=spandrel)
        y_glass_end = min(y + floor_h - 1, cell_y + CELL - BORDER - 1)
        draw.rectangle([cell_x + BORDER, y + 9,
                        cell_x + CELL - BORDER - 1, y_glass_end],
                       fill=base_color)
        draw.rectangle([cell_x + BORDER, y + 9,
                        cell_x + CELL - BORDER - 1, y + 14],
                       fill=glass_light)
        y += floor_h
    x = cell_x + BORDER
    mullion_color = tuple(max(c - 40, 0) for c in base_color)
    while x < cell_x + CELL - BORDER:
        draw.rectangle([x, cell_y + BORDER,
                        x + mullion_w - 1, cell_y + CELL - BORDER - 1],
                       fill=mullion_color)
        x += bay_w


# ---------------------------------------------------------------------------
# Cell painters (one per atlas cell, matching generate_production_textures.py)
# ---------------------------------------------------------------------------

def paint_cell_00_res_low(atlas, cx, cy):
    BG = (232, 213, 176)
    _draw_brick_rows(atlas, cx, cy, base_color=BG,
                     mortar_color=(180, 168, 150), brick_h=20, mortar_h=3)
    draw = ImageDraw.Draw(atlas)
    border_fill = (232, 213, 176)
    for side in [
        (cx, cy, cx + CELL - 1, cy + BORDER - 1),
        (cx, cy + CELL - BORDER, cx + CELL - 1, cy + CELL - 1),
        (cx, cy, cx + BORDER - 1, cy + CELL - 1),
        (cx + CELL - BORDER, cy, cx + CELL - 1, cy + CELL - 1),
    ]:
        draw.rectangle(side, fill=border_fill)
    _draw_brick_rows(atlas, cx, cy, base_color=BG)
    _draw_windows(draw, cx, cy, rows=2, cols=3,
                  win_w=50, win_h=48,
                  frame_color=(200, 185, 155),
                  glass_color=(140, 160, 180),
                  margin_top=60, margin_bottom=30)


def paint_cell_01_com_low(atlas, cx, cy):
    BG = (184, 200, 216)
    img_arr = np.full((CELL, CELL, 4), (*BG, 255), dtype=np.uint8)
    img_arr = _add_noise(img_arr, 5)
    cell = Image.fromarray(img_arr, "RGBA")
    atlas.paste(cell, (cx, cy))
    draw = ImageDraw.Draw(atlas)
    fascia_h = int(CELL * 0.18)
    fascia_color = (100, 120, 145)
    draw.rectangle([cx + BORDER, cy + BORDER,
                    cx + CELL - BORDER - 1, cy + BORDER + fascia_h],
                   fill=fascia_color)
    sign_color = (220, 230, 240)
    draw.rectangle([cx + BORDER + 20, cy + BORDER + fascia_h // 3,
                    cx + CELL - BORDER - 20, cy + BORDER + fascia_h * 2 // 3],
                   fill=sign_color)
    win_y = cy + BORDER + int(USABLE * 0.58)
    win_x = cx + BORDER + int(USABLE * 0.1)
    win_w = int(USABLE * 0.8)
    win_h = int(USABLE * 0.38)
    draw.rectangle([win_x - 4, win_y - 4, win_x + win_w + 3, win_y + win_h + 3],
                   fill=(160, 175, 190))
    pane_w = win_w // 3
    for i in range(3):
        px = win_x + i * pane_w + 2
        draw.rectangle([px, win_y, px + pane_w - 4, win_y + win_h],
                       fill=(180, 210, 230))
        draw.rectangle([px, win_y, px + 6, win_y + win_h // 2],
                       fill=(210, 230, 245))
    door_x = cx + CELL // 2 - 20
    draw.rectangle([door_x, win_y, door_x + 38, win_y + win_h],
                   fill=(100, 130, 160))


def paint_cell_02_ind_low(atlas, cx, cy):
    BG = (160, 160, 160)
    cell = _base_cell(BG)
    atlas.paste(cell, (cx, cy))
    _draw_corrugated(atlas, cx, cy, base_color=BG, line_spacing=10)
    draw = ImageDraw.Draw(atlas)
    dock_y = cy + BORDER + int(USABLE * 0.65)
    dock_x = cx + BORDER + USABLE // 4
    dock_w = USABLE // 2
    dock_h = int(USABLE * 0.30)
    draw.rectangle([dock_x - 6, dock_y - 6,
                    dock_x + dock_w + 5, dock_y + dock_h + 5],
                   fill=(100, 100, 100))
    rib_h = 16
    y = dock_y
    toggle = True
    while y < dock_y + dock_h:
        rib_color = (80, 80, 80) if toggle else (95, 95, 95)
        draw.rectangle([dock_x, y, dock_x + dock_w - 1,
                        min(y + rib_h - 1, dock_y + dock_h - 1)],
                       fill=rib_color)
        y += rib_h
        toggle = not toggle
    win_y = cy + BORDER + 30
    win_x = cx + BORDER + 20
    draw.rectangle([win_x, win_y, win_x + 60, win_y + 36], fill=(90, 90, 90))
    draw.rectangle([win_x + 3, win_y + 3, win_x + 57, win_y + 33],
                   fill=(140, 155, 160))


def paint_cell_03_base_low(atlas, cx, cy):
    BG = (200, 184, 154)
    cell = _base_cell(BG)
    atlas.paste(cell, (cx, cy))
    draw = ImageDraw.Draw(atlas)
    band_h = int(USABLE * 0.12)
    draw.rectangle([cx + BORDER, cy + BORDER,
                    cx + CELL - BORDER - 1, cy + BORDER + band_h],
                   fill=(170, 158, 130))
    for i in range(1, 4):
        ly = cy + BORDER + band_h + i * (USABLE - band_h) // 4
        draw.line([(cx + BORDER, ly), (cx + CELL - BORDER - 1, ly)],
                  fill=(180, 168, 138), width=1)
    for xoff in [80, 280]:
        wx = cx + BORDER + xoff
        wy = cy + BORDER + band_h + 40
        draw.rectangle([wx, wy, wx + 80, wy + 60], fill=(155, 145, 120))
        draw.rectangle([wx + 4, wy + 4, wx + 76, wy + 56], fill=(120, 135, 150))


def paint_cell_10_res_med(atlas, cx, cy):
    BG = (212, 192, 144)
    cell = _base_cell(BG)
    atlas.paste(cell, (cx, cy))
    draw = ImageDraw.Draw(atlas)
    floor_h = USABLE // 4
    for f in range(4):
        fy = cy + BORDER + f * floor_h
        draw.rectangle([cx + BORDER, fy + floor_h - 5,
                        cx + CELL - BORDER - 1, fy + floor_h - 1],
                       fill=(185, 165, 120))
        ww, wh = 70, int(floor_h * 0.5)
        wy = fy + (floor_h - wh) // 2 - 5
        for c in range(3):
            wx = cx + BORDER + c * (USABLE // 3) + (USABLE // 3 - ww) // 2
            draw.rectangle([wx - 3, wy - 3, wx + ww + 2, wy + wh + 2],
                           fill=(185, 165, 120))
            draw.rectangle([wx, wy, wx + ww, wy + wh], fill=(210, 200, 170))
            draw.rectangle([wx + 4, wy + 4, wx + ww - 4, wy + wh - 4],
                           fill=(160, 185, 205))
            draw.rectangle([wx - 6, wy + wh - 1, wx + ww + 5, wy + wh + 5],
                           fill=(175, 155, 110))


def paint_cell_11_com_med(atlas, cx, cy):
    BG = (128, 144, 168)
    cell = _base_cell(BG)
    atlas.paste(cell, (cx, cy))
    _draw_curtain_wall(atlas, cx, cy, base_color=BG, mullion_w=5, bay_w=70, floor_h=70)


def paint_cell_12_ind_med(atlas, cx, cy):
    BG = (120, 120, 120)
    cell = _base_cell(BG)
    atlas.paste(cell, (cx, cy))
    draw = ImageDraw.Draw(atlas)
    _draw_corrugated(atlas, cx, cy, base_color=BG, line_spacing=16)
    panel_w = 80
    joint_color = (90, 90, 90)
    x = cx + BORDER
    while x < cx + CELL - BORDER:
        draw.line([(x, cy + BORDER), (x, cy + CELL - BORDER - 1)],
                  fill=joint_color, width=2)
        for y_dot in range(cy + BORDER + 20, cy + CELL - BORDER, 40):
            draw.ellipse([x - 3, y_dot - 3, x + 3, y_dot + 3],
                         fill=(100, 100, 100))
        x += panel_w
    win_y = cy + BORDER + 20
    win_h = int(USABLE * 0.20)
    for wc in range(3):
        wx = cx + BORDER + wc * (USABLE // 3) + 15
        ww = USABLE // 3 - 30
        draw.rectangle([wx - 3, win_y - 3, wx + ww + 2, win_y + win_h + 2],
                       fill=(80, 80, 80))
        draw.rectangle([wx, win_y, wx + ww, win_y + win_h],
                       fill=(135, 155, 165))


def paint_cell_13_base_med(atlas, cx, cy):
    BG = (96, 96, 96)
    cell = _base_cell(BG)
    atlas.paste(cell, (cx, cy))
    draw = ImageDraw.Draw(atlas)
    entry_y = cy + BORDER + int(USABLE * 0.65)
    draw.rectangle([cx + BORDER, entry_y,
                    cx + CELL - BORDER - 1, cy + CELL - BORDER - 1],
                   fill=(70, 90, 110))
    pane_w = USABLE // 4
    for p in range(4):
        px = cx + BORDER + p * pane_w + 4
        draw.rectangle([px, entry_y + 8, px + pane_w - 8, cy + CELL - BORDER - 8],
                       fill=(100, 135, 165))
        draw.rectangle([px - 3, entry_y, px - 1, cy + CELL - BORDER - 1],
                       fill=(50, 60, 70))
    sign_y = cy + BORDER
    sign_h = int(USABLE * 0.12)
    draw.rectangle([cx + BORDER, sign_y,
                    cx + CELL - BORDER - 1, sign_y + sign_h],
                   fill=(50, 50, 55))
    text_y = sign_y + sign_h // 4
    text_x = cx + BORDER + 30
    for seg_w in [80, 50, 90, 45, 70]:
        draw.rectangle([text_x, text_y, text_x + seg_w, text_y + sign_h // 2],
                       fill=(200, 210, 220))
        text_x += seg_w + 12
    for i in range(1, 4):
        ly = sign_y + sign_h + i * int(USABLE * 0.53) // 4
        draw.line([(cx + BORDER, ly), (cx + CELL - BORDER - 1, ly)],
                  fill=(80, 80, 80), width=2)


def paint_cell_20_res_high(atlas, cx, cy):
    BG = (224, 221, 213)
    arr = np.full((CELL, CELL, 4), (*BG, 255), dtype=np.uint8)
    arr = _add_noise(arr, 4)
    atlas.paste(Image.fromarray(arr, "RGBA"), (cx, cy))
    draw = ImageDraw.Draw(atlas)
    floors = 6
    floor_h = USABLE // floors
    cols = 4
    ww, wh = 55, int(floor_h * 0.52)
    rng = np.random.default_rng(42)
    for f in range(floors):
        band_tint = (215, 212, 204) if f % 2 == 0 else BG
        fy = cy + BORDER + f * floor_h
        draw.rectangle([cx + BORDER, fy,
                        cx + CELL - BORDER - 1, fy + floor_h - 1],
                       fill=band_tint)
        draw.rectangle([cx + BORDER, fy,
                        cx + CELL - BORDER - 1, fy + 3],
                       fill=(190, 188, 180))
        for c in range(cols):
            wx = cx + BORDER + c * (USABLE // cols) + (USABLE // cols - ww) // 2
            wy = fy + (floor_h - wh) // 2
            draw.rectangle([wx - 2, wy - 2, wx + ww + 1, wy + wh + 1],
                           fill=(200, 198, 190))
            gr = int(rng.integers(140, 175))
            gg = int(rng.integers(165, 200))
            gb = int(rng.integers(200, 225))
            draw.rectangle([wx, wy, wx + ww, wy + wh], fill=(gr, gg, gb))


def paint_cell_21_com_high(atlas, cx, cy):
    BG = (112, 128, 144)
    cell = _base_cell(BG)
    atlas.paste(cell, (cx, cy))
    _draw_curtain_wall(atlas, cx, cy, base_color=BG, mullion_w=4, bay_w=56, floor_h=60)
    draw = ImageDraw.Draw(atlas)
    for stripe in range(0, CELL, 90):
        x0 = cx + BORDER + stripe
        draw.line([(x0, cy + BORDER), (x0 + 40, cy + CELL - BORDER)],
                  fill=(140, 160, 178), width=3)


def paint_cell_22_ind_high(atlas, cx, cy):
    BG = (80, 80, 88)
    cell = _base_cell(BG)
    atlas.paste(cell, (cx, cy))
    draw = ImageDraw.Draw(atlas)
    panel_w = 100
    panel_h = 80
    panel_light = (95, 95, 105)
    panel_dark  = (60, 60, 66)
    panel_joint = (45, 45, 50)
    for py in range(0, CELL, panel_h):
        for px_off in range(0, CELL, panel_w):
            px = cx + px_off
            py_abs = cy + py
            tint = panel_light if ((py // panel_h + px_off // panel_w) % 2 == 0) else panel_dark
            draw.rectangle([px + 2, py_abs + 2,
                            px + panel_w - 3, py_abs + panel_h - 3],
                           fill=tint)
        draw.rectangle([cx, cy + py, cx + CELL - 1, cy + py + 2], fill=panel_joint)
    for px_off in range(0, CELL, panel_w):
        draw.rectangle([cx + px_off, cy, cx + px_off + 2, cy + CELL - 1],
                       fill=panel_joint)
    for r in range(2):
        for c in range(2):
            wx = cx + BORDER + c * (USABLE // 2) + 30
            wy = cy + BORDER + r * (USABLE // 2) + 25
            draw.rectangle([wx - 3, wy - 3, wx + 75, wy + 43], fill=(55, 55, 60))
            draw.rectangle([wx, wy, wx + 72, wy + 40], fill=(110, 125, 135))
            for bar in range(1, 3):
                draw.line([(wx + bar * 24, wy), (wx + bar * 24, wy + 40)],
                          fill=(70, 70, 75), width=2)


def paint_cell_23_roof_shared(atlas, cx, cy):
    BG = (72, 72, 72)
    cell = _base_cell(BG)
    atlas.paste(cell, (cx, cy))
    draw = ImageDraw.Draw(atlas)
    seam_color = (60, 60, 60)
    for x in range(cx + BORDER, cx + CELL - BORDER, 40):
        draw.line([(x, cy + BORDER), (x, cy + CELL - BORDER)],
                  fill=seam_color, width=1)
    draw.rectangle([cx + BORDER, cy + BORDER,
                    cx + CELL - BORDER - 1, cy + BORDER + 10],
                   fill=(55, 55, 58))
    ux, uy = cx + BORDER + 30, cy + BORDER + 40
    draw.rectangle([ux, uy, ux + 120, uy + 80], fill=(85, 88, 88))
    draw.rectangle([ux + 4, uy + 4, ux + 116, uy + 20], fill=(95, 98, 98))
    for vx in range(ux + 10, ux + 120, 12):
        draw.line([(vx, uy + 25), (vx, uy + 75)], fill=(60, 63, 63), width=2)
    ux2, uy2 = cx + BORDER + 220, cy + BORDER + 50
    draw.rectangle([ux2, uy2, ux2 + 70, uy2 + 55], fill=(80, 83, 83))
    draw.rectangle([ux2 + 3, uy2 + 3, ux2 + 67, uy2 + 15], fill=(90, 93, 93))
    ux3, uy3 = cx + BORDER + 340, cy + BORDER + 60
    draw.rectangle([ux3, uy3, ux3 + 80, uy3 + 80], fill=(78, 81, 81))
    draw.ellipse([ux3 + 10, uy3 + 10, ux3 + 70, uy3 + 70], fill=(65, 68, 68))
    draw.ellipse([ux3 + 25, uy3 + 25, ux3 + 55, uy3 + 55], fill=(78, 81, 81))
    for gx in range(cx + BORDER + 80, cx + CELL - BORDER, 100):
        draw.rectangle([gx - 4, cy + CELL - BORDER - 15,
                        gx + 4, cy + CELL - BORDER - 1],
                       fill=(50, 50, 55))


def paint_cell_30_facade_balcony(atlas, cx, cy):
    BG = (144, 144, 144)
    cell = _base_cell(BG)
    atlas.paste(cell, (cx, cy))
    draw = ImageDraw.Draw(atlas)
    cols, rows = 3, 4
    mod_w = USABLE // cols
    mod_h = USABLE // rows
    shadow_depth = 8
    rail_color = (110, 110, 112)
    for r in range(rows):
        for c in range(cols):
            bx = cx + BORDER + c * mod_w
            by = cy + BORDER + r * mod_h
            draw.rectangle([bx + 4, by + 4,
                            bx + mod_w - 4, by + shadow_depth + 4],
                           fill=(100, 100, 102))
            draw.rectangle([bx + 4, by + mod_h - 12,
                            bx + mod_w - 4, by + mod_h - 4],
                           fill=(120, 120, 122))
            for post in range(4):
                px = bx + 10 + post * ((mod_w - 16) // 3)
                draw.line([(px, by + shadow_depth + 5), (px, by + mod_h - 12)],
                          fill=rail_color, width=3)
            draw.line([(bx + 8, by + shadow_depth + 5 + 15),
                       (bx + mod_w - 8, by + shadow_depth + 5 + 15)],
                      fill=rail_color, width=2)
            draw.rectangle([bx + 18, by + shadow_depth + 24,
                            bx + mod_w - 18, by + mod_h - 20],
                           fill=(160, 170, 185))


def paint_cell_31_facade_pilaster(atlas, cx, cy):
    BG = (176, 168, 144)
    cell = _base_cell(BG)
    atlas.paste(cell, (cx, cy))
    draw = ImageDraw.Draw(atlas)
    pilaster_w = 40
    pilaster_spacing = USABLE // 3
    shadow_c = (145, 138, 118)
    light_c  = (200, 195, 175)
    cap_c    = (160, 153, 130)
    for p in range(3):
        px = cx + BORDER + p * pilaster_spacing + (pilaster_spacing - pilaster_w) // 2
        draw.rectangle([px, cy + BORDER, px + pilaster_w, cy + CELL - BORDER - 1],
                       fill=BG)
        draw.rectangle([px, cy + BORDER, px + 6, cy + CELL - BORDER - 1],
                       fill=shadow_c)
        draw.rectangle([px + pilaster_w - 6, cy + BORDER,
                        px + pilaster_w, cy + CELL - BORDER - 1],
                       fill=light_c)
        draw.rectangle([px - 8, cy + BORDER,
                        px + pilaster_w + 8, cy + BORDER + 28],
                       fill=cap_c)
        draw.rectangle([px - 4, cy + BORDER + 28,
                        px + pilaster_w + 4, cy + BORDER + 36],
                       fill=shadow_c)
        draw.rectangle([px - 8, cy + CELL - BORDER - 28,
                        px + pilaster_w + 8, cy + CELL - BORDER - 1],
                       fill=cap_c)
    draw.rectangle([cx + BORDER, cy + BORDER,
                    cx + CELL - BORDER - 1, cy + BORDER + 16],
                   fill=(155, 148, 126))
    for dx in range(cx + BORDER + 10, cx + CELL - BORDER, 18):
        draw.rectangle([dx, cy + BORDER + 4, dx + 10, cy + BORDER + 16],
                       fill=light_c)


def paint_cell_32_service(atlas, cx, cy):
    draw = ImageDraw.Draw(atlas)
    conc_h = int(USABLE * 0.30)
    conc_base = (144, 144, 136)
    arr_conc = np.full((conc_h, USABLE, 4), (*conc_base, 255), dtype=np.uint8)
    arr_conc = _add_noise(arr_conc, 6)
    conc_img = Image.fromarray(arr_conc, "RGBA")
    atlas.paste(conc_img, (cx + BORDER, cy + BORDER))
    for i in range(0, conc_h, 24):
        draw.line([(cx + BORDER, cy + BORDER + i),
                   (cx + CELL - BORDER - 1, cy + BORDER + i)],
                  fill=(120, 120, 112), width=1)
    draw.line([(cx + BORDER + 180, cy + BORDER + 10),
               (cx + BORDER + 175, cy + BORDER + conc_h - 10)],
              fill=(115, 115, 108), width=1)
    glass_y = cy + BORDER + conc_h
    glass_h = int(USABLE * 0.40)
    glass_base = (96, 128, 160)
    draw.rectangle([cx + BORDER, glass_y,
                    cx + CELL - BORDER - 1, glass_y + glass_h - 1],
                   fill=glass_base)
    bay_w = 55
    for bx in range(cx + BORDER, cx + CELL - BORDER, bay_w):
        draw.rectangle([bx, glass_y, bx + bay_w - 3, glass_y + glass_h - 1],
                       fill=(100, 135, 168))
        draw.rectangle([bx, glass_y, bx + 8, glass_y + glass_h // 2],
                       fill=(130, 165, 195))
        draw.rectangle([bx + bay_w - 3, glass_y,
                        bx + bay_w - 1, glass_y + glass_h - 1],
                       fill=(65, 90, 115))
    draw.rectangle([cx + BORDER, glass_y,
                    cx + CELL - BORDER - 1, glass_y + 8],
                   fill=(70, 100, 130))
    util_y = glass_y + glass_h
    util_h = USABLE - conc_h - glass_h
    util_base = (112, 112, 112)
    draw.rectangle([cx + BORDER, util_y,
                    cx + CELL - BORDER - 1, cy + CELL - BORDER - 1],
                   fill=util_base)
    panel_w = 64
    panel_h_u = 40
    for pr in range(0, util_h, panel_h_u):
        for pc in range(0, USABLE, panel_w):
            draw.rectangle([cx + BORDER + pc + 2, util_y + pr + 2,
                            cx + BORDER + pc + panel_w - 3, util_y + pr + panel_h_u - 3],
                           fill=(118, 118, 118))
    stripe_y = cy + CELL - BORDER - 14
    stripe_colors = [(200, 165, 0), (30, 30, 30)]
    stripe_w = 20
    for si, sx in enumerate(range(cx + BORDER, cx + CELL - BORDER, stripe_w)):
        draw.rectangle([sx, stripe_y, sx + stripe_w - 1, cy + CELL - BORDER - 1],
                       fill=stripe_colors[si % 2])


def paint_cell_33_reserved(atlas, cx, cy):
    draw = ImageDraw.Draw(atlas)
    sq = 16
    for row in range(CELL // sq):
        for col in range(CELL // sq):
            color = (255, 0, 255) if (row + col) % 2 == 0 else (0, 0, 0)
            draw.rectangle([cx + col * sq, cy + row * sq,
                            cx + col * sq + sq - 1, cy + row * sq + sq - 1],
                           fill=color)


# ---------------------------------------------------------------------------
# Cell painter dispatch table
# ---------------------------------------------------------------------------
CELL_PAINTERS = {
    (0, 0): paint_cell_00_res_low,
    (0, 1): paint_cell_01_com_low,
    (0, 2): paint_cell_02_ind_low,
    (0, 3): paint_cell_03_base_low,
    (1, 0): paint_cell_10_res_med,
    (1, 1): paint_cell_11_com_med,
    (1, 2): paint_cell_12_ind_med,
    (1, 3): paint_cell_13_base_med,
    (2, 0): paint_cell_20_res_high,
    (2, 1): paint_cell_21_com_high,
    (2, 2): paint_cell_22_ind_high,
    (2, 3): paint_cell_23_roof_shared,
    (3, 0): paint_cell_30_facade_balcony,
    (3, 1): paint_cell_31_facade_pilaster,
    (3, 2): paint_cell_32_service,
    (3, 3): paint_cell_33_reserved,
}


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def main() -> None:
    print("Generating 2048x2048 RGBA8 building atlas...")
    atlas = Image.new("RGBA", (2048, 2048), (0, 0, 0, 255))

    for (row, col), painter in CELL_PAINTERS.items():
        cx = col * CELL
        cy = row * CELL
        painter(atlas, cx, cy)
        print(f"  Painted cell ({row},{col})")

    print(f"Writing uncompressed RGBA8 DDS to: {OUT_PATH}")
    write_rgba8_dds(atlas, OUT_PATH)
    size = OUT_PATH.stat().st_size
    print(f"Done. File size: {size:,} bytes (expected {128 + 2048*2048*4:,} bytes)")
    assert size == 128 + 2048 * 2048 * 4, \
        f"Size mismatch: got {size}, expected {128 + 2048*2048*4}"
    print("Size check passed.")


if __name__ == "__main__":
    main()
