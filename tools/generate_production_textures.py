#!/usr/bin/env python3
"""Generate production-quality DDS texture files for AI Town V1 building and vehicle assets.

Unlike generate_dds_stubs.py (which produces structurally-correct but visually blank
placeholders), this script paints real visual content into every texture:

  - buildings_atlas_d.dds      2048×2048 DXT1  sRGB   4 mips  — 4×4 cell building facade atlas
  - buildings_atlas_d_n.dds    [not generated here — normal maps stay as _n.dds stubs]
  - wall_*_n.dds (9 files)     512×512  DXT5   linear 4 mips  — DXT5nm flat normals
  - wall_*_s.dds (9 files)     512×512  DXT1   linear 4 mips  — zone-appropriate specular
  - vehicles_diffuse_atlas_d.dds  2048×2048 DXT1 sRGB  4 mips — vehicle body palette
  - vehicles_sprite_atlas_d.dds   256×256  DXT5  linear 1 mip  — roof color swatches (binary)
  - *_lm.dds (18 files)        512×512  DXT5   linear 1 mip  — AO gradient lightmaps
  - *_billboard.dds (12 files) 1024×128 DX10   sRGB   4 mips  — building elevation strips

All DXT compression is performed via ImageMagick `convert`; pixel art is painted with
PIL/Pillow + numpy for noise variation.

Format compliance
-----------------
- Building atlas: DXT1 FourCC (sRGB upload path handled by TextureCache::loadSRGB)
- Billboard atlas: DX10 extended header, DXGI_FORMAT=78 (BC3_UNORM_SRGB) — check_13
- Normal maps: DXT5nm packing: RGBA(0, Y, 0, X) = RGBA(0,128,0,128) for flat normals
- Specular maps: DXT1 grayscale, zone-appropriate intensity
- Lightmaps: DXT5, single mip level (GL_TEXTURE_MAX_LEVEL=0 lightmap exemption)
- Vehicle sprite atlas: DXT5 binary write (solid-color DXT5 blocks per cell), 1 mip

Mip chain generation
--------------------
ImageMagick -define dds:mipmaps=N generates N extra mip levels beyond level 0.
For 4-level chains we pass mipmaps=3 (levels 1-3 auto-generated from level 0).
"""

import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parent.parent

# ---------------------------------------------------------------------------
# Utility: run ImageMagick convert
# ---------------------------------------------------------------------------

def _im_convert(args: list[str]) -> None:
    """Run ImageMagick convert with the given argument list, raising on failure."""
    cmd = ["convert"] + args
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(
            f"ImageMagick failed: {' '.join(cmd)}\n"
            f"stderr: {result.stderr}"
        )


def _ensure_dir(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


# ---------------------------------------------------------------------------
# Core DDS header writer (for billboard DX10 + sprite atlas binary path)
# ---------------------------------------------------------------------------

def _rgb_to_rgb565(r: int, g: int, b: int) -> int:
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def _make_dxt5_solid_block(r: int, g: int, b: int, a: int = 255) -> bytes:
    """16-byte DXT5 block encoding a solid RGBA colour."""
    c = _rgb_to_rgb565(r, g, b)
    alpha_part = struct.pack('<BB', a, a) + b'\x00' * 6
    color_part = struct.pack('<HHI', c, c, 0x00000000)
    return alpha_part + color_part


def _write_dds_header(width: int, height: int, fourcc: bytes,
                      mip_levels: int = 1,
                      is_dx10: bool = False,
                      dxgi_format: int = 0) -> bytes:
    """Return DDS magic + DDS_HEADER (+ DX10 header if requested)."""
    DDSD_CAPS        = 0x1
    DDSD_HEIGHT      = 0x2
    DDSD_WIDTH       = 0x4
    DDSD_PIXELFORMAT = 0x1000
    DDSD_LINEARSIZE  = 0x80000
    DDSD_MIPMAPCOUNT = 0x20000

    flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_LINEARSIZE
    if mip_levels > 1:
        flags |= DDSD_MIPMAPCOUNT

    bw = max(1, (width  + 3) // 4)
    bh = max(1, (height + 3) // 4)
    if fourcc == b'DXT1' or (is_dx10 and dxgi_format in (71, 72)):
        linear_size = bw * bh * 8
    else:
        linear_size = bw * bh * 16

    data = b'DDS '
    data += struct.pack('<I', 124)
    data += struct.pack('<I', flags)
    data += struct.pack('<I', height)
    data += struct.pack('<I', width)
    data += struct.pack('<I', linear_size)
    data += struct.pack('<I', 0)
    data += struct.pack('<I', mip_levels)
    data += b'\x00' * 44

    if is_dx10:
        data += struct.pack('<I', 32)
        data += struct.pack('<I', 4)
        data += b'DX10'
        data += b'\x00' * 20
    else:
        data += struct.pack('<I', 32)
        data += struct.pack('<I', 4)
        data += fourcc
        data += b'\x00' * 20

    DDSCAPS_TEXTURE = 0x1000
    DDSCAPS_MIPMAP  = 0x400000 if mip_levels > 1 else 0
    DDSCAPS_COMPLEX = 0x8      if mip_levels > 1 else 0
    data += struct.pack('<I', DDSCAPS_TEXTURE | DDSCAPS_MIPMAP | DDSCAPS_COMPLEX)
    data += struct.pack('<I', 0)
    data += struct.pack('<I', 0)
    data += struct.pack('<I', 0)
    data += struct.pack('<I', 0)

    if is_dx10:
        data += struct.pack('<I', dxgi_format)
        data += struct.pack('<I', 3)   # D3D10_RESOURCE_DIMENSION_TEXTURE2D
        data += struct.pack('<I', 0)
        data += struct.pack('<I', 1)
        data += struct.pack('<I', 0)

    return data


# ---------------------------------------------------------------------------
# Noise helper
# ---------------------------------------------------------------------------

def _add_noise(arr: np.ndarray, amount: int = 8) -> np.ndarray:
    """Add uniform noise ±amount to RGB channels, clamping to [0, 255]."""
    rng = np.random.default_rng(seed=42)
    noise = rng.integers(-amount, amount + 1, size=arr.shape[:2] + (3,), dtype=np.int16)
    result = arr.copy()
    result[..., :3] = np.clip(arr[..., :3].astype(np.int16) + noise, 0, 255).astype(np.uint8)
    return result


# ===========================================================================
# Cell painters — each returns a PIL RGBA Image of cell_size × cell_size
# ===========================================================================

CELL = 512
BORDER = 8
USABLE = CELL - 2 * BORDER   # 496


def _base_cell(bg: tuple[int, int, int]) -> Image.Image:
    """Create a cell filled with bg colour plus slight noise."""
    arr = np.full((CELL, CELL, 4), (*bg, 255), dtype=np.uint8)
    arr = _add_noise(arr, 6)
    return Image.fromarray(arr, 'RGBA')


def _draw_windows(draw: ImageDraw.ImageDraw,
                  cell_x: int, cell_y: int,
                  rows: int, cols: int,
                  win_w: int, win_h: int,
                  frame_color: tuple[int, int, int],
                  glass_color: tuple[int, int, int],
                  margin_top: int = 40, margin_bottom: int = 20) -> None:
    """Draw a grid of windows within the usable area of a cell."""
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
            # Frame
            draw.rectangle([wx - 3, wy - 3, wx + win_w + 2, wy + win_h + 2],
                           fill=frame_color)
            # Glass
            draw.rectangle([wx, wy, wx + win_w, wy + win_h], fill=glass_color)


def _draw_brick_rows(img: Image.Image,
                     cell_x: int, cell_y: int,
                     base_color: tuple[int, int, int],
                     mortar_color: tuple[int, int, int] = (180, 168, 150),
                     brick_h: int = 24, mortar_h: int = 3) -> None:
    """Paint horizontal brick courses with offset alternating rows."""
    draw = ImageDraw.Draw(img)
    rng = np.random.default_rng(seed=7)
    brick_w = 64
    y = cell_y + BORDER
    row = 0
    while y < cell_y + CELL - BORDER:
        # mortar joint
        draw.rectangle([cell_x + BORDER, y, cell_x + CELL - BORDER - 1, y + mortar_h - 1],
                       fill=mortar_color)
        y += mortar_h
        if y >= cell_y + CELL - BORDER:
            break
        # brick row
        offset = (brick_w // 2) if (row % 2 == 1) else 0
        x = cell_x + BORDER - offset
        while x < cell_x + CELL - BORDER:
            # vary brick tint slightly
            br = int(np.clip(base_color[0] + rng.integers(-10, 11), 0, 255))
            bg = int(np.clip(base_color[1] + rng.integers(-8,  9),  0, 255))
            bb = int(np.clip(base_color[2] + rng.integers(-6,  7),  0, 255))
            bx0 = max(x, cell_x + BORDER)
            bx1 = min(x + brick_w - 2, cell_x + CELL - BORDER - 1)
            draw.rectangle([bx0, y, bx1, y + brick_h - 1], fill=(br, bg, bb))
            x += brick_w
        y += brick_h
        row += 1


def _draw_corrugated(img: Image.Image,
                     cell_x: int, cell_y: int,
                     base_color: tuple[int, int, int],
                     line_spacing: int = 12) -> None:
    """Draw horizontal corrugated metal lines."""
    draw = ImageDraw.Draw(img)
    light = tuple(min(c + 20, 255) for c in base_color)
    dark  = tuple(max(c - 20, 0) for c in base_color)
    y = cell_y + BORDER
    toggle = False
    while y < cell_y + CELL - BORDER:
        color = light if toggle else dark
        draw.line([(cell_x + BORDER, y), (cell_x + CELL - BORDER - 1, y)],
                  fill=color, width=2)
        y += line_spacing
        toggle = not toggle


def _draw_curtain_wall(img: Image.Image,
                       cell_x: int, cell_y: int,
                       base_color: tuple[int, int, int],
                       mullion_w: int = 4, bay_w: int = 62,
                       floor_h: int = 80) -> None:
    """Draw reflective curtain-wall facade with vertical mullions and spandrel bands."""
    draw = ImageDraw.Draw(img)
    # Spandrel bands (floor lines)
    spandrel = tuple(max(c - 30, 0) for c in base_color)
    glass_light = tuple(min(c + 40, 255) for c in base_color)
    y = cell_y + BORDER
    while y < cell_y + CELL - BORDER:
        # Spandrel strip
        draw.rectangle([cell_x + BORDER, y, cell_x + CELL - BORDER - 1, y + 8],
                       fill=spandrel)
        # Glass band
        y_glass_end = min(y + floor_h - 1, cell_y + CELL - BORDER - 1)
        draw.rectangle([cell_x + BORDER, y + 9, cell_x + CELL - BORDER - 1, y_glass_end],
                       fill=base_color)
        # Highlight strip at top of glass
        draw.rectangle([cell_x + BORDER, y + 9, cell_x + CELL - BORDER - 1, y + 14],
                       fill=glass_light)
        y += floor_h

    # Vertical mullions
    x = cell_x + BORDER
    mullion_color = tuple(max(c - 40, 0) for c in base_color)
    while x < cell_x + CELL - BORDER:
        draw.rectangle([x, cell_y + BORDER, x + mullion_w - 1, cell_y + CELL - BORDER - 1],
                       fill=mullion_color)
        x += bay_w


def paint_cell_00_res_low(atlas: Image.Image, cx: int, cy: int) -> None:
    """Residential low-rise: warm cream brick with small square windows."""
    BG = (232, 213, 176)
    _draw_brick_rows(atlas, cx, cy, base_color=BG,
                     mortar_color=(180, 168, 150), brick_h=20, mortar_h=3)
    draw = ImageDraw.Draw(atlas)
    # Border fill with base colour
    border_fill = (232, 213, 176)
    for side in [
        (cx, cy, cx + CELL - 1, cy + BORDER - 1),
        (cx, cy + CELL - BORDER, cx + CELL - 1, cy + CELL - 1),
        (cx, cy, cx + BORDER - 1, cy + CELL - 1),
        (cx + CELL - BORDER, cy, cx + CELL - 1, cy + CELL - 1),
    ]:
        draw.rectangle(side, fill=border_fill)
    # Re-draw brick inside usable area only
    _draw_brick_rows(atlas, cx, cy, base_color=BG)
    # Windows: 3 columns × 2 rows, small squares
    _draw_windows(draw, cx, cy, rows=2, cols=3,
                  win_w=50, win_h=48,
                  frame_color=(200, 185, 155),
                  glass_color=(140, 160, 180),
                  margin_top=60, margin_bottom=30)


def paint_cell_01_com_low(atlas: Image.Image, cx: int, cy: int) -> None:
    """Commercial low-rise: pale gray-blue, large storefront window, fascia band."""
    BG = (184, 200, 216)
    img_arr = np.full((CELL, CELL, 4), (*BG, 255), dtype=np.uint8)
    img_arr = _add_noise(img_arr, 5)
    # Paste into atlas
    cell = Image.fromarray(img_arr, 'RGBA')
    atlas.paste(cell, (cx, cy))
    draw = ImageDraw.Draw(atlas)

    # Fascia band (upper 18%)
    fascia_h = int(CELL * 0.18)
    fascia_color = (100, 120, 145)
    draw.rectangle([cx + BORDER, cy + BORDER, cx + CELL - BORDER - 1, cy + BORDER + fascia_h],
                   fill=fascia_color)
    # Fascia sign stripe
    sign_color = (220, 230, 240)
    draw.rectangle([cx + BORDER + 20, cy + BORDER + fascia_h // 3,
                    cx + CELL - BORDER - 20, cy + BORDER + fascia_h * 2 // 3],
                   fill=sign_color)

    # Large storefront window (lower 40% of usable area, 80% width)
    win_y = cy + BORDER + int(USABLE * 0.58)
    win_x = cx + BORDER + int(USABLE * 0.1)
    win_w = int(USABLE * 0.8)
    win_h = int(USABLE * 0.38)
    draw.rectangle([win_x - 4, win_y - 4, win_x + win_w + 3, win_y + win_h + 3],
                   fill=(160, 175, 190))  # frame
    # Glass panes
    pane_w = win_w // 3
    for i in range(3):
        px = win_x + i * pane_w + 2
        draw.rectangle([px, win_y, px + pane_w - 4, win_y + win_h],
                       fill=(180, 210, 230))
        # Reflection highlight
        draw.rectangle([px, win_y, px + 6, win_y + win_h // 2],
                       fill=(210, 230, 245))

    # Door
    door_x = cx + cx + CELL // 2 - 20 - cx  # center
    door_x = cx + CELL // 2 - 20
    draw.rectangle([door_x, win_y, door_x + 38, win_y + win_h],
                   fill=(100, 130, 160))


def paint_cell_02_ind_low(atlas: Image.Image, cx: int, cy: int) -> None:
    """Industrial low-rise: gray corrugated metal, loading dock."""
    BG = (160, 160, 160)
    cell = _base_cell(BG)
    atlas.paste(cell, (cx, cy))
    _draw_corrugated(atlas, cx, cy, base_color=BG, line_spacing=10)
    draw = ImageDraw.Draw(atlas)

    # Loading dock rectangle (lower 30%, centered)
    dock_y = cy + BORDER + int(USABLE * 0.65)
    dock_x = cx + BORDER + USABLE // 4
    dock_w = USABLE // 2
    dock_h = int(USABLE * 0.30)
    draw.rectangle([dock_x - 6, dock_y - 6, dock_x + dock_w + 5, dock_y + dock_h + 5],
                   fill=(100, 100, 100))  # frame
    # Dock door panels (ribbed)
    rib_h = 16
    y = dock_y
    toggle = True
    while y < dock_y + dock_h:
        rib_color = (80, 80, 80) if toggle else (95, 95, 95)
        draw.rectangle([dock_x, y, dock_x + dock_w - 1, min(y + rib_h - 1, dock_y + dock_h - 1)],
                       fill=rib_color)
        y += rib_h
        toggle = not toggle

    # Small utility window
    win_y = cy + BORDER + 30
    win_x = cx + BORDER + 20
    draw.rectangle([win_x, win_y, win_x + 60, win_y + 36],
                   fill=(90, 90, 90))
    draw.rectangle([win_x + 3, win_y + 3, win_x + 57, win_y + 33],
                   fill=(140, 155, 160))


def paint_cell_03_base_low(atlas: Image.Image, cx: int, cy: int) -> None:
    """Shared base module (low-density ground floor): taupe concrete, minimal windows."""
    BG = (200, 184, 154)
    cell = _base_cell(BG)
    atlas.paste(cell, (cx, cy))
    draw = ImageDraw.Draw(atlas)

    # Concrete band at top (10%)
    band_h = int(USABLE * 0.12)
    draw.rectangle([cx + BORDER, cy + BORDER, cx + CELL - BORDER - 1, cy + BORDER + band_h],
                   fill=(170, 158, 130))

    # Horizontal scoring lines
    for i in range(1, 4):
        ly = cy + BORDER + band_h + i * (USABLE - band_h) // 4
        draw.line([(cx + BORDER, ly), (cx + CELL - BORDER - 1, ly)],
                  fill=(180, 168, 138), width=1)

    # Two small fenestration openings
    for xoff in [80, 280]:
        wx = cx + BORDER + xoff
        wy = cy + BORDER + band_h + 40
        draw.rectangle([wx, wy, wx + 80, wy + 60], fill=(155, 145, 120))
        draw.rectangle([wx + 4, wy + 4, wx + 76, wy + 56], fill=(120, 135, 150))


def paint_cell_10_res_med(atlas: Image.Image, cx: int, cy: int) -> None:
    """Residential medium-rise: sandy yellow, horizontal window bands, balcony lines."""
    BG = (212, 192, 144)
    cell = _base_cell(BG)
    atlas.paste(cell, (cx, cy))
    draw = ImageDraw.Draw(atlas)

    # 4 floor bands
    floor_h = USABLE // 4
    for f in range(4):
        fy = cy + BORDER + f * floor_h
        # Balcony line
        draw.rectangle([cx + BORDER, fy + floor_h - 5, cx + CELL - BORDER - 1, fy + floor_h - 1],
                       fill=(185, 165, 120))
        # Window row: 3 windows per floor
        ww, wh = 70, int(floor_h * 0.5)
        wy = fy + (floor_h - wh) // 2 - 5
        for c in range(3):
            wx = cx + BORDER + c * (USABLE // 3) + (USABLE // 3 - ww) // 2
            draw.rectangle([wx - 3, wy - 3, wx + ww + 2, wy + wh + 2],
                           fill=(185, 165, 120))
            # Frame
            draw.rectangle([wx, wy, wx + ww, wy + wh], fill=(210, 200, 170))
            # Glass
            draw.rectangle([wx + 4, wy + 4, wx + ww - 4, wy + wh - 4],
                           fill=(160, 185, 205))
            # Balcony slab (bottom of window)
            draw.rectangle([wx - 6, wy + wh - 1, wx + ww + 5, wy + wh + 5],
                           fill=(175, 155, 110))


def paint_cell_11_com_med(atlas: Image.Image, cx: int, cy: int) -> None:
    """Commercial medium-rise: steel blue-gray reflective curtain wall."""
    BG = (128, 144, 168)
    cell = _base_cell(BG)
    atlas.paste(cell, (cx, cy))
    _draw_curtain_wall(atlas, cx, cy, base_color=BG, mullion_w=5, bay_w=70, floor_h=70)


def paint_cell_12_ind_med(atlas: Image.Image, cx: int, cy: int) -> None:
    """Industrial medium-rise: dark gray, warehouse windows, riveted panels."""
    BG = (120, 120, 120)
    cell = _base_cell(BG)
    atlas.paste(cell, (cx, cy))
    draw = ImageDraw.Draw(atlas)
    _draw_corrugated(atlas, cx, cy, base_color=BG, line_spacing=16)

    # Panel joint lines (vertical, simulating riveted cladding)
    panel_w = 80
    joint_color = (90, 90, 90)
    x = cx + BORDER
    while x < cx + CELL - BORDER:
        draw.line([(x, cy + BORDER), (x, cy + CELL - BORDER - 1)],
                  fill=joint_color, width=2)
        # Rivet dots
        for y_dot in range(cy + BORDER + 20, cy + CELL - BORDER, 40):
            draw.ellipse([x - 3, y_dot - 3, x + 3, y_dot + 3], fill=(100, 100, 100))
        x += panel_w

    # High warehouse windows (upper 25%)
    win_y = cy + BORDER + 20
    win_h = int(USABLE * 0.20)
    for wc in range(3):
        wx = cx + BORDER + wc * (USABLE // 3) + 15
        ww = USABLE // 3 - 30
        draw.rectangle([wx - 3, win_y - 3, wx + ww + 2, win_y + win_h + 2],
                       fill=(80, 80, 80))
        draw.rectangle([wx, win_y, wx + ww, win_y + win_h],
                       fill=(135, 155, 165))


def paint_cell_13_base_med(atlas: Image.Image, cx: int, cy: int) -> None:
    """Shared base module (med/high lobby): dark concrete, glazed entry, signage."""
    BG = (96, 96, 96)
    cell = _base_cell(BG)
    atlas.paste(cell, (cx, cy))
    draw = ImageDraw.Draw(atlas)

    # Glazed entry zone (bottom 30%)
    entry_y = cy + BORDER + int(USABLE * 0.65)
    draw.rectangle([cx + BORDER, entry_y, cx + CELL - BORDER - 1, cy + CELL - BORDER - 1],
                   fill=(70, 90, 110))
    # Entry glass panes
    pane_w = USABLE // 4
    for p in range(4):
        px = cx + BORDER + p * pane_w + 4
        draw.rectangle([px, entry_y + 8, px + pane_w - 8, cy + CELL - BORDER - 8],
                       fill=(100, 135, 165))
        # Vertical mullion
        draw.rectangle([px - 3, entry_y, px - 1, cy + CELL - BORDER - 1],
                       fill=(50, 60, 70))

    # Signage band (upper 15%)
    sign_y = cy + BORDER
    sign_h = int(USABLE * 0.12)
    draw.rectangle([cx + BORDER, sign_y, cx + CELL - BORDER - 1, sign_y + sign_h],
                   fill=(50, 50, 55))
    # Sign text simulation (rectangles of varying width = letters)
    text_y = sign_y + sign_h // 4
    text_x = cx + BORDER + 30
    for seg_w in [80, 50, 90, 45, 70]:
        draw.rectangle([text_x, text_y, text_x + seg_w, text_y + sign_h // 2],
                       fill=(200, 210, 220))
        text_x += seg_w + 12

    # Horizontal concrete band lines
    for i in range(1, 4):
        ly = sign_y + sign_h + i * int(USABLE * 0.53) // 4
        draw.line([(cx + BORDER, ly), (cx + CELL - BORDER - 1, ly)],
                  fill=(80, 80, 80), width=2)


def paint_cell_20_res_high(atlas: Image.Image, cx: int, cy: int) -> None:
    """Residential high-rise: off-white, repetitive window grid, floor band variation."""
    BG = (224, 221, 213)
    arr = np.full((CELL, CELL, 4), (*BG, 255), dtype=np.uint8)
    arr = _add_noise(arr, 4)
    atlas.paste(Image.fromarray(arr, 'RGBA'), (cx, cy))
    draw = ImageDraw.Draw(atlas)

    # 6 floors of windows
    floors = 6
    floor_h = USABLE // floors
    cols = 4
    ww, wh = 55, int(floor_h * 0.52)
    rng = np.random.default_rng(42)
    for f in range(floors):
        # Subtle band tint per every 2 floors
        if f % 2 == 0:
            band_tint = (215, 212, 204)
        else:
            band_tint = BG
        fy = cy + BORDER + f * floor_h
        draw.rectangle([cx + BORDER, fy, cx + CELL - BORDER - 1, fy + floor_h - 1],
                       fill=band_tint)
        # Balcony ledge
        draw.rectangle([cx + BORDER, fy, cx + CELL - BORDER - 1, fy + 3],
                       fill=(190, 188, 180))
        # Windows
        for c in range(cols):
            wx = cx + BORDER + c * (USABLE // cols) + (USABLE // cols - ww) // 2
            wy = fy + (floor_h - wh) // 2
            # Window frame
            draw.rectangle([wx - 2, wy - 2, wx + ww + 1, wy + wh + 1],
                           fill=(200, 198, 190))
            # Glass with slight variation
            gr = int(rng.integers(140, 175))
            gg = int(rng.integers(165, 200))
            gb = int(rng.integers(200, 225))
            draw.rectangle([wx, wy, wx + ww, wy + wh], fill=(gr, gg, gb))


def paint_cell_21_com_high(atlas: Image.Image, cx: int, cy: int) -> None:
    """Commercial high-rise: tinted glass blue-green full curtain wall."""
    BG = (112, 128, 144)
    cell = _base_cell(BG)
    atlas.paste(cell, (cx, cy))
    _draw_curtain_wall(atlas, cx, cy, base_color=BG, mullion_w=4, bay_w=56, floor_h=60)
    draw = ImageDraw.Draw(atlas)
    # Additional diagonal reflection highlights
    for stripe in range(0, CELL, 90):
        x0 = cx + BORDER + stripe
        draw.line([(x0, cy + BORDER), (x0 + 40, cy + CELL - BORDER)],
                  fill=(140, 160, 178), width=3)


def paint_cell_22_ind_high(atlas: Image.Image, cx: int, cy: int) -> None:
    """Industrial high-rise: gunmetal gray, heavy cladding panels, sparse windows."""
    BG = (80, 80, 88)
    cell = _base_cell(BG)
    atlas.paste(cell, (cx, cy))
    draw = ImageDraw.Draw(atlas)

    # Heavy cladding panel grid
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
            draw.rectangle([px + 2, py_abs + 2, px + panel_w - 3, py_abs + panel_h - 3],
                           fill=tint)
        # Horizontal joint
        draw.rectangle([cx, cy + py, cx + CELL - 1, cy + py + 2], fill=panel_joint)

    for px_off in range(0, CELL, panel_w):
        draw.rectangle([cx + px_off, cy, cx + px_off + 2, cy + CELL - 1], fill=panel_joint)

    # Sparse industrial windows (2 rows × 2 cols)
    for r in range(2):
        for c in range(2):
            wx = cx + BORDER + c * (USABLE // 2) + 30
            wy = cy + BORDER + r * (USABLE // 2) + 25
            draw.rectangle([wx - 3, wy - 3, wx + 75, wy + 43], fill=(55, 55, 60))
            draw.rectangle([wx, wy, wx + 72, wy + 40], fill=(110, 125, 135))
            # Bars
            for bar in range(1, 3):
                draw.line([(wx + bar * 24, wy), (wx + bar * 24, wy + 40)],
                          fill=(70, 70, 75), width=2)


def paint_cell_23_roof_shared(atlas: Image.Image, cx: int, cy: int) -> None:
    """Shared roof: flat dark gray, HVAC/AC units, parapet edge stripe."""
    BG = (72, 72, 72)
    cell = _base_cell(BG)
    atlas.paste(cell, (cx, cy))
    draw = ImageDraw.Draw(atlas)

    # Roofing membrane with seam lines
    seam_color = (60, 60, 60)
    for x in range(cx + BORDER, cx + CELL - BORDER, 40):
        draw.line([(x, cy + BORDER), (x, cy + CELL - BORDER)], fill=seam_color, width=1)

    # Parapet edge stripe (top 12px of usable area)
    draw.rectangle([cx + BORDER, cy + BORDER,
                    cx + CELL - BORDER - 1, cy + BORDER + 10],
                   fill=(55, 55, 58))

    # HVAC unit 1 (large)
    ux, uy = cx + BORDER + 30, cy + BORDER + 40
    draw.rectangle([ux, uy, ux + 120, uy + 80], fill=(85, 88, 88))
    draw.rectangle([ux + 4, uy + 4, ux + 116, uy + 20], fill=(95, 98, 98))
    # Vent grill lines
    for vx in range(ux + 10, ux + 120, 12):
        draw.line([(vx, uy + 25), (vx, uy + 75)], fill=(60, 63, 63), width=2)

    # HVAC unit 2 (small)
    ux2, uy2 = cx + BORDER + 220, cy + BORDER + 50
    draw.rectangle([ux2, uy2, ux2 + 70, uy2 + 55], fill=(80, 83, 83))
    draw.rectangle([ux2 + 3, uy2 + 3, ux2 + 67, uy2 + 15], fill=(90, 93, 93))

    # AC condenser unit
    ux3, uy3 = cx + BORDER + 340, cy + BORDER + 60
    draw.rectangle([ux3, uy3, ux3 + 80, uy3 + 80], fill=(78, 81, 81))
    draw.ellipse([ux3 + 10, uy3 + 10, ux3 + 70, uy3 + 70], fill=(65, 68, 68))
    draw.ellipse([ux3 + 25, uy3 + 25, ux3 + 55, uy3 + 55], fill=(78, 81, 81))

    # Drain gutters
    for gx in range(cx + BORDER + 80, cx + CELL - BORDER, 100):
        draw.rectangle([gx - 4, cy + CELL - BORDER - 15, gx + 4, cy + CELL - BORDER - 1],
                       fill=(50, 50, 55))


def paint_cell_30_facade_balcony(atlas: Image.Image, cx: int, cy: int) -> None:
    """Balcony/window-bay detail: concrete with shadow insets and railing lines."""
    BG = (144, 144, 144)
    cell = _base_cell(BG)
    atlas.paste(cell, (cx, cy))
    draw = ImageDraw.Draw(atlas)

    # Balcony modules: 3 columns × 4 rows
    cols, rows = 3, 4
    mod_w = USABLE // cols
    mod_h = USABLE // rows
    shadow_depth = 8
    rail_color = (110, 110, 112)

    for r in range(rows):
        for c in range(cols):
            bx = cx + BORDER + c * mod_w
            by = cy + BORDER + r * mod_h
            # Slab soffit shadow (bottom of balcony)
            draw.rectangle([bx + 4, by + 4, bx + mod_w - 4, by + shadow_depth + 4],
                           fill=(100, 100, 102))
            # Floor slab
            draw.rectangle([bx + 4, by + mod_h - 12, bx + mod_w - 4, by + mod_h - 4],
                           fill=(120, 120, 122))
            # Railing posts (4 per balcony)
            for post in range(4):
                px = bx + 10 + post * ((mod_w - 16) // 3)
                draw.line([(px, by + shadow_depth + 5), (px, by + mod_h - 12)],
                          fill=rail_color, width=3)
            # Horizontal rail
            draw.line([(bx + 8, by + shadow_depth + 5 + 15),
                       (bx + mod_w - 8, by + shadow_depth + 5 + 15)],
                      fill=rail_color, width=2)
            # Inset window area
            draw.rectangle([bx + 18, by + shadow_depth + 24,
                            bx + mod_w - 18, by + mod_h - 20],
                           fill=(160, 170, 185))


def paint_cell_31_facade_pilaster(atlas: Image.Image, cx: int, cy: int) -> None:
    """Pilaster/cornice detail: stone-colored with vertical relief shadow lines."""
    BG = (176, 168, 144)
    cell = _base_cell(BG)
    atlas.paste(cell, (cx, cy))
    draw = ImageDraw.Draw(atlas)

    # Pilaster columns (3 pilasters across usable width)
    pilaster_w = 40
    pilaster_spacing = USABLE // 3
    shadow_c  = (145, 138, 118)
    light_c   = (200, 195, 175)
    cap_c     = (160, 153, 130)

    for p in range(3):
        px = cx + BORDER + p * pilaster_spacing + (pilaster_spacing - pilaster_w) // 2
        # Pilaster shaft
        draw.rectangle([px, cy + BORDER, px + pilaster_w, cy + CELL - BORDER - 1],
                       fill=BG)
        # Left shadow edge
        draw.rectangle([px, cy + BORDER, px + 6, cy + CELL - BORDER - 1], fill=shadow_c)
        # Right highlight edge
        draw.rectangle([px + pilaster_w - 6, cy + BORDER,
                        px + pilaster_w, cy + CELL - BORDER - 1], fill=light_c)
        # Capital top
        draw.rectangle([px - 8, cy + BORDER, px + pilaster_w + 8, cy + BORDER + 28],
                       fill=cap_c)
        draw.rectangle([px - 4, cy + BORDER + 28, px + pilaster_w + 4, cy + BORDER + 36],
                       fill=shadow_c)
        # Base pedestal
        draw.rectangle([px - 8, cy + CELL - BORDER - 28, px + pilaster_w + 8, cy + CELL - BORDER - 1],
                       fill=cap_c)

    # Cornice band at top
    draw.rectangle([cx + BORDER, cy + BORDER, cx + CELL - BORDER - 1, cy + BORDER + 16],
                   fill=(155, 148, 126))
    # Dentils
    for dx in range(cx + BORDER + 10, cx + CELL - BORDER, 18):
        draw.rectangle([dx, cy + BORDER + 4, dx + 10, cy + BORDER + 16], fill=light_c)


def paint_cell_32_service(atlas: Image.Image, cx: int, cy: int) -> None:
    """Service buildings palette: 3 horizontal zones — concrete / glass / utility panels."""
    draw = ImageDraw.Draw(atlas)

    # Zone 1: Concrete (top 30%)
    conc_h = int(USABLE * 0.30)
    conc_base = (144, 144, 136)
    arr_conc = np.full((conc_h, USABLE, 4), (*conc_base, 255), dtype=np.uint8)
    arr_conc = _add_noise(arr_conc, 6)
    conc_img = Image.fromarray(arr_conc, 'RGBA')
    atlas.paste(conc_img, (cx + BORDER, cy + BORDER))
    # Aggregate scoring lines (horizontal)
    for i in range(0, conc_h, 24):
        draw.line([(cx + BORDER, cy + BORDER + i), (cx + CELL - BORDER - 1, cy + BORDER + i)],
                  fill=(120, 120, 112), width=1)
    # Vertical crack simulation
    draw.line([(cx + BORDER + 180, cy + BORDER + 10), (cx + BORDER + 175, cy + BORDER + conc_h - 10)],
              fill=(115, 115, 108), width=1)

    # Zone 2: Glass / glazing (middle 40%)
    glass_y = cy + BORDER + conc_h
    glass_h = int(USABLE * 0.40)
    glass_base = (96, 128, 160)
    draw.rectangle([cx + BORDER, glass_y, cx + CELL - BORDER - 1, glass_y + glass_h - 1],
                   fill=glass_base)
    # Vertical glass bays
    bay_w = 55
    for bx in range(cx + BORDER, cx + CELL - BORDER, bay_w):
        draw.rectangle([bx, glass_y, bx + bay_w - 3, glass_y + glass_h - 1],
                       fill=(100, 135, 168))
        # Highlight column
        draw.rectangle([bx, glass_y, bx + 8, glass_y + glass_h // 2],
                       fill=(130, 165, 195))
        # Mullion
        draw.rectangle([bx + bay_w - 3, glass_y, bx + bay_w - 1, glass_y + glass_h - 1],
                       fill=(65, 90, 115))
    # Spandrel
    draw.rectangle([cx + BORDER, glass_y, cx + CELL - BORDER - 1, glass_y + 8],
                   fill=(70, 100, 130))

    # Zone 3: Utility panels (bottom 30%) with hazard stripe
    util_y = glass_y + glass_h
    util_h = USABLE - conc_h - glass_h
    util_base = (112, 112, 112)
    draw.rectangle([cx + BORDER, util_y, cx + CELL - BORDER - 1, cy + CELL - BORDER - 1],
                   fill=util_base)
    # Panel grid
    panel_w = 64
    panel_h_u = 40
    for pr in range(0, util_h, panel_h_u):
        for pc in range(0, USABLE, panel_w):
            draw.rectangle([cx + BORDER + pc + 2, util_y + pr + 2,
                            cx + BORDER + pc + panel_w - 3, util_y + pr + panel_h_u - 3],
                           fill=(118, 118, 118))
    # Hazard diagonal stripe (bottom 14px)
    stripe_y = cy + CELL - BORDER - 14
    stripe_colors = [(200, 165, 0), (30, 30, 30)]
    stripe_w = 20
    for si, sx in enumerate(range(cx + BORDER, cx + CELL - BORDER, stripe_w)):
        draw.rectangle([sx, stripe_y, sx + stripe_w - 1, cy + CELL - BORDER - 1],
                       fill=stripe_colors[si % 2])


def paint_cell_33_reserved(atlas: Image.Image, cx: int, cy: int) -> None:
    """Reserved cell: magenta/black checkerboard so accidental use is obvious."""
    draw = ImageDraw.Draw(atlas)
    sq = 16
    for row in range(CELL // sq):
        for col in range(CELL // sq):
            color = (255, 0, 255) if (row + col) % 2 == 0 else (0, 0, 0)
            draw.rectangle([cx + col * sq, cy + row * sq,
                            cx + col * sq + sq - 1, cy + row * sq + sq - 1],
                           fill=color)


# ===========================================================================
# Building Atlas Assembly
# ===========================================================================

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


def generate_building_atlas() -> None:
    print("\n--- Building atlas (2048x2048 DXT1, 4 mips) ---")
    atlas = Image.new('RGBA', (2048, 2048), (0, 0, 0, 255))

    for (row, col), painter in CELL_PAINTERS.items():
        cx = col * CELL
        cy = row * CELL
        painter(atlas, cx, cy)
        print(f"  Painted cell ({row},{col})")

    tmp_png = Path('/tmp/buildings_atlas_d.png')
    atlas.save(str(tmp_png))
    print(f"  Saved source PNG: {tmp_png}")

    out = ROOT / 'assets/textures/buildings/buildings_atlas_d.dds'
    _ensure_dir(out)
    _im_convert([
        str(tmp_png),
        '-define', 'dds:compression=dxt1',
        '-define', 'dds:mipmaps=3',  # 3 extra mips beyond level 0 = 4 total
        str(out),
    ])
    print(f"  Created: assets/textures/buildings/buildings_atlas_d.dds  ({out.stat().st_size:,} bytes)")


# ===========================================================================
# Wall Normal Maps — DXT5nm
# ===========================================================================

def _make_flat_normal_dxt5nm(size: int) -> bytes:
    """Return DXT5nm pixel data for a flat normal map (X=0, Y=0, pointing outward).

    DXT5nm packing: alpha = X component (128 = neutral), green = Y component (128 = neutral).
    Fill = RGBA(0, 128, 0, 128) per spec.
    Using ImageMagick to compress.
    """
    # We'll write via PIL + ImageMagick so we just return the fill color
    return (0, 128, 0, 128)  # sentinel — handled in generate_wall_normal_maps


def generate_wall_normal_maps() -> None:
    """9 wall normal maps, 512×512 DXT5nm, 4 mip levels."""
    print("\n--- Wall normal maps (512x512 DXT5, 4 mips, DXT5nm flat) ---")

    # DXT5nm flat normal: RGBA(0, 128, 0, 128)
    img = Image.new('RGBA', (512, 512), (0, 128, 0, 128))
    tmp_png = Path('/tmp/wall_flat_normal.png')
    img.save(str(tmp_png))

    names = [
        'wall_residential_low_n', 'wall_residential_med_n', 'wall_residential_high_n',
        'wall_commercial_low_n',  'wall_commercial_med_n',  'wall_commercial_high_n',
        'wall_industrial_low_n',  'wall_industrial_med_n',  'wall_industrial_high_n',
    ]
    for name in names:
        out = ROOT / f'assets/textures/buildings/{name}.dds'
        _ensure_dir(out)
        _im_convert([
            str(tmp_png),
            '-define', 'dds:compression=dxt5',
            '-define', 'dds:mipmaps=3',
            str(out),
        ])
        print(f"  Created: assets/textures/buildings/{name}.dds  ({out.stat().st_size:,} bytes)")


# ===========================================================================
# Wall Specular Maps
# ===========================================================================

def generate_wall_specular_maps() -> None:
    """9 wall specular maps, 512×512 DXT1, 4 mip levels, zone-appropriate intensity."""
    print("\n--- Wall specular maps (512x512 DXT1, 4 mips) ---")

    # Zone intensities: residential=64 (25%), commercial=192 (75%), industrial=128 (50%)
    specs = {
        'residential': 64,
        'commercial': 192,
        'industrial': 128,
    }
    tiers = ['low', 'med', 'high']

    for zone, intensity in specs.items():
        val = intensity
        img = Image.new('RGB', (512, 512), (val, val, val))
        tmp_png = Path(f'/tmp/wall_{zone}_spec.png')
        img.save(str(tmp_png))

        for tier in tiers:
            name = f'wall_{zone}_{tier}_s'
            out = ROOT / f'assets/textures/buildings/{name}.dds'
            _ensure_dir(out)
            _im_convert([
                str(tmp_png),
                '-define', 'dds:compression=dxt1',
                '-define', 'dds:mipmaps=3',
                str(out),
            ])
            print(f"  Created: assets/textures/buildings/{name}.dds  ({out.stat().st_size:,} bytes)")


# ===========================================================================
# Vehicle Diffuse Atlas
# ===========================================================================

def _paint_vehicle_cell(atlas: Image.Image,
                        cx: int, cy: int,
                        body_color: tuple[int, int, int],
                        roof_color: tuple[int, int, int],
                        accent_color: tuple[int, int, int] | None,
                        tail_color: tuple[int, int, int] | None = None,
                        window_color: tuple[int, int, int] = (180, 200, 215)) -> None:
    """Paint a vehicle face-layout cell within the 512×512 vehicle atlas.

    Layout:
      Left 10%  (0-51px)    : left side panel
      Center 80% (52-459px) : main body with top/side view
      Right 10% (460-511px) : right side panel
      Top area               : roof view
      Middle area            : windows + body
      Bottom area            : undercarriage / base
    """
    draw = ImageDraw.Draw(atlas)
    W = CELL  # 512

    # Background fill
    arr = np.full((W, W, 4), (*body_color, 255), dtype=np.uint8)
    arr = _add_noise(arr, 5)
    patch = Image.fromarray(arr, 'RGBA')
    atlas.paste(patch, (cx, cy))

    # --- Left side panel (0–51px wide) ---
    lp_x0, lp_x1 = cx, cx + 51
    draw.rectangle([lp_x0, cy, lp_x1, cy + W - 1], fill=body_color)
    # Side windows
    sw_y = cy + int(W * 0.28)
    sw_h = int(W * 0.28)
    draw.rectangle([lp_x0 + 6, sw_y, lp_x1 - 6, sw_y + sw_h], fill=window_color)
    # Door line
    draw.line([(lp_x0 + 26, sw_y - 10), (lp_x0 + 26, sw_y + sw_h + 20)],
              fill=tuple(max(c - 25, 0) for c in body_color), width=2)

    # --- Right side panel (460-511px) ---
    rp_x0, rp_x1 = cx + 460, cx + W - 1
    draw.rectangle([rp_x0, cy, rp_x1, cy + W - 1], fill=body_color)
    draw.rectangle([rp_x0 + 6, sw_y, rp_x1 - 6, sw_y + sw_h], fill=window_color)
    draw.line([(rp_x0 + 26, sw_y - 10), (rp_x0 + 26, sw_y + sw_h + 20)],
              fill=tuple(max(c - 25, 0) for c in body_color), width=2)

    # --- Center main area (52-459px) ---
    main_x0 = cx + 52
    main_w = 408

    # Roof zone (top 28%)
    roof_h = int(W * 0.28)
    draw.rectangle([main_x0, cy, main_x0 + main_w - 1, cy + roof_h], fill=roof_color)
    # Roof highlights
    draw.rectangle([main_x0, cy, main_x0 + main_w - 1, cy + 6],
                   fill=tuple(min(c + 30, 255) for c in roof_color))

    # Windshield / front window band (28-42%)
    ws_y = cy + roof_h
    ws_h = int(W * 0.14)
    draw.rectangle([main_x0 + 20, ws_y, main_x0 + main_w - 20, ws_y + ws_h],
                   fill=window_color)
    # Pillar A (left)
    draw.rectangle([main_x0, ws_y, main_x0 + 20, ws_y + ws_h],
                   fill=body_color)
    # Pillar A (right)
    draw.rectangle([main_x0 + main_w - 20, ws_y, main_x0 + main_w - 1, ws_y + ws_h],
                   fill=body_color)

    # Body middle (42-68%): side face front + body
    body_mid_y = ws_y + ws_h
    body_mid_h = int(W * 0.26)
    draw.rectangle([main_x0, body_mid_y, main_x0 + main_w - 1, body_mid_y + body_mid_h],
                   fill=body_color)
    # Door division lines
    door_x = main_x0 + main_w // 2
    draw.line([(door_x, body_mid_y), (door_x, body_mid_y + body_mid_h)],
              fill=tuple(max(c - 20, 0) for c in body_color), width=3)
    # Door handles
    for dx in [door_x - 80, door_x + 60]:
        draw.rectangle([dx, body_mid_y + body_mid_h // 2 - 4,
                        dx + 22, body_mid_y + body_mid_h // 2 + 4],
                       fill=tuple(min(c + 40, 255) for c in body_color))

    # Accent stripe if provided
    if accent_color:
        stripe_y = body_mid_y + body_mid_h // 2 - 5
        draw.rectangle([main_x0 + 10, stripe_y, main_x0 + main_w - 10, stripe_y + 8],
                       fill=accent_color)

    # Tail / taillights strip (68-76%)
    if tail_color:
        tail_y = body_mid_y + body_mid_h
        tail_h = int(W * 0.08)
        draw.rectangle([main_x0, tail_y, main_x0 + main_w - 1, tail_y + tail_h],
                       fill=body_color)
        # Taillight modules
        for tlx in [main_x0 + 10, main_x0 + main_w - 80]:
            draw.rectangle([tlx, tail_y + 4, tlx + 60, tail_y + tail_h - 4],
                           fill=tail_color)

    # Underbody/base (bottom 24%)
    base_y = cy + int(W * 0.76)
    base_h = W - int(W * 0.76)
    draw.rectangle([main_x0, base_y, main_x0 + main_w - 1, cy + W - 1],
                   fill=tuple(max(c - 30, 0) for c in body_color))
    # Wheel arch shadows
    for arch_x in [main_x0 + 40, main_x0 + main_w - 120]:
        draw.ellipse([arch_x, base_y - 10, arch_x + 80, base_y + 25],
                     fill=tuple(max(c - 50, 0) for c in body_color))


def generate_vehicle_diffuse_atlas() -> None:
    """Vehicle diffuse atlas: 2048×2048, 4×4 cells, DXT1 sRGB, 4 mips."""
    print("\n--- Vehicle diffuse atlas (2048x2048 DXT1, 4 mips) ---")
    atlas = Image.new('RGBA', (2048, 2048), (20, 20, 20, 255))

    # (0,0) car_sedan: pearl white, dark charcoal roof, red taillights
    _paint_vehicle_cell(atlas, 0, 0,
                        body_color=(240, 238, 232),
                        roof_color=(48, 48, 48),
                        accent_color=(180, 180, 185),
                        tail_color=(200, 30, 30))

    # (0,1) car_hatchback: red body, dark roof, silver door strip
    _paint_vehicle_cell(atlas, CELL, 0,
                        body_color=(192, 48, 48),
                        roof_color=(40, 40, 40),
                        accent_color=(160, 160, 165),
                        tail_color=(180, 20, 20))

    # (0,2) car_suv: dark navy, black roof rails, silver accents
    _paint_vehicle_cell(atlas, CELL * 2, 0,
                        body_color=(32, 48, 80),
                        roof_color=(18, 18, 18),
                        accent_color=(140, 145, 150),
                        tail_color=(160, 20, 20))

    # (0,3) RESERVED checker
    _paint_reserved_checker(atlas, CELL * 3, 0)

    # (1,0) bus_standard: yellow-cream body, white window band, dark roof, red stripe
    _paint_bus_cell(atlas, 0, CELL)

    # (1,1) truck_cargo: orange-red cab, beige cargo box
    _paint_truck_cell(atlas, CELL, CELL)

    # Remaining cells: reserved checker
    for col in range(2, 4):
        _paint_reserved_checker(atlas, CELL * col, CELL)
    for row in range(2, 4):
        for col in range(4):
            _paint_reserved_checker(atlas, CELL * col, CELL * row)

    tmp_png = Path('/tmp/vehicles_diffuse_atlas_d.png')
    atlas.save(str(tmp_png))
    out = ROOT / 'assets/textures/vehicles/vehicles_diffuse_atlas_d.dds'
    _ensure_dir(out)
    _im_convert([
        str(tmp_png),
        '-define', 'dds:compression=dxt1',
        '-define', 'dds:mipmaps=3',
        str(out),
    ])
    print(f"  Created: assets/textures/vehicles/vehicles_diffuse_atlas_d.dds  ({out.stat().st_size:,} bytes)")


def _paint_reserved_checker(atlas: Image.Image, cx: int, cy: int, sq: int = 32) -> None:
    draw = ImageDraw.Draw(atlas)
    for row in range(CELL // sq):
        for col in range(CELL // sq):
            color = (255, 0, 255) if (row + col) % 2 == 0 else (0, 0, 0)
            draw.rectangle([cx + col * sq, cy + row * sq,
                            cx + col * sq + sq - 1, cy + row * sq + sq - 1],
                           fill=color)


def _paint_bus_cell(atlas: Image.Image, cx: int, cy: int) -> None:
    """Bus: yellow-cream body, white upper window band, dark roof, red bottom stripe."""
    draw = ImageDraw.Draw(atlas)
    W = CELL
    BUS_BODY = (232, 208, 80)
    BUS_ROOF = (64, 64, 64)
    BUS_WIN  = (245, 248, 252)
    BUS_STRIPE = (180, 30, 30)

    # Fill body
    arr = np.full((W, W, 4), (*BUS_BODY, 255), dtype=np.uint8)
    arr = _add_noise(arr, 4)
    atlas.paste(Image.fromarray(arr, 'RGBA'), (cx, cy))

    # Roof band (top 18%)
    roof_h = int(W * 0.18)
    draw.rectangle([cx, cy, cx + W - 1, cy + roof_h], fill=BUS_ROOF)

    # Window band (upper 35% of remaining body)
    win_y = cy + roof_h
    win_h = int((W - roof_h) * 0.35)
    draw.rectangle([cx, win_y, cx + W - 1, win_y + win_h], fill=BUS_WIN)
    # Window division lines (every 64px)
    for wx in range(cx, cx + W, 64):
        draw.line([(wx, win_y), (wx, win_y + win_h)],
                  fill=(200, 205, 210), width=3)

    # Red bottom stripe (bottom 12%)
    stripe_y = cy + W - int(W * 0.12)
    draw.rectangle([cx, stripe_y, cx + W - 1, cy + W - 1], fill=BUS_STRIPE)

    # Door section
    door_x = cx + 40
    door_y = win_y + win_h
    door_h = stripe_y - door_y
    draw.rectangle([door_x, door_y, door_x + 55, door_y + door_h],
                   fill=(60, 65, 70))
    draw.rectangle([door_x + 4, door_y + 4, door_x + 51, door_y + door_h - 4],
                   fill=(150, 175, 200))


def _paint_truck_cell(atlas: Image.Image, cx: int, cy: int) -> None:
    """Truck cargo: orange-red cab (left), beige cargo box (right)."""
    draw = ImageDraw.Draw(atlas)
    W = CELL
    CAB_COLOR   = (192, 72, 32)
    BOX_COLOR   = (208, 192, 160)
    BOX_DARK    = (190, 175, 143)
    EXHAUST_C   = (180, 180, 185)

    # Cab occupies left 35%
    cab_w = int(W * 0.35)
    arr = np.full((W, W, 4), (*BOX_COLOR, 255), dtype=np.uint8)
    arr = _add_noise(arr, 5)
    atlas.paste(Image.fromarray(arr, 'RGBA'), (cx, cy))

    # Cab
    draw.rectangle([cx, cy, cx + cab_w - 1, cy + W - 1], fill=CAB_COLOR)
    # Cab roof
    cab_roof_h = int(W * 0.15)
    draw.rectangle([cx, cy, cx + cab_w - 1, cy + cab_roof_h], fill=(140, 50, 20))
    # Cab windshield
    ws_y = cy + cab_roof_h + 4
    ws_h = int(W * 0.20)
    draw.rectangle([cx + 8, ws_y, cx + cab_w - 12, ws_y + ws_h],
                   fill=(180, 200, 215))
    # Cab door
    door_y = ws_y + ws_h + 4
    draw.line([(cx + cab_w // 2, door_y), (cx + cab_w // 2, cy + W - 30)],
              fill=(160, 55, 22), width=3)
    # Cab window
    cwin_y = door_y + 10
    draw.rectangle([cx + 8, cwin_y, cx + cab_w // 2 - 6, cwin_y + int(W * 0.15)],
                   fill=(160, 185, 205))

    # Cargo box (right 65%)
    box_x = cx + cab_w
    box_w = W - cab_w
    draw.rectangle([box_x, cy + int(W * 0.06), box_x + box_w - 1, cy + W - 1], fill=BOX_COLOR)
    # Cargo box panels (horizontal ribs)
    panel_h = 48
    for py in range(int(W * 0.06), W, panel_h):
        alt = BOX_DARK if (py // panel_h) % 2 == 0 else BOX_COLOR
        draw.rectangle([box_x + 2, cy + py + 2, box_x + box_w - 3, cy + py + panel_h - 3],
                       fill=alt)
        draw.line([(box_x, cy + py), (box_x + box_w - 1, cy + py)],
                  fill=(170, 155, 125), width=2)
    # Rear door handle area
    draw.rectangle([box_x + box_w - 30, cy + W // 2 - 20,
                    box_x + box_w - 6, cy + W // 2 + 20],
                   fill=(170, 155, 125))

    # Exhaust pipe
    ep_x = cx + cab_w - 8
    draw.rectangle([ep_x, cy + 4, ep_x + 10, cy + cab_roof_h + 10], fill=EXHAUST_C)
    draw.ellipse([ep_x, cy + 2, ep_x + 10, cy + 14], fill=(120, 120, 125))


# ===========================================================================
# Vehicle Sprite Atlas — binary DDS write
# ===========================================================================

def generate_vehicle_sprite_atlas() -> None:
    """256×256 DXT5 sprite atlas, 1 mip level. Written as binary DDS.

    16×16 grid, each cell 16×16px = 4×4 DXT5 blocks.
    Roof color swatches per spec; all other cells black.
    Linear upload path (not sRGB).
    """
    print("\n--- Vehicle sprite atlas (256x256 DXT5, 1 mip, binary write) ---")

    GRID = 16
    CELL_PX = 16   # pixels per grid cell
    W = GRID * CELL_PX  # 256

    # Cell (row, col) -> (R, G, B) solid fill
    swatches = {
        (0, 0): (204, 204, 204),   # car_sedan — light gray roof
        (0, 1): (128, 32, 32),     # car_hatchback — dark red
        (0, 2): (21, 32, 53),      # car_suv — very dark blue
        (1, 0): (56, 56, 56),      # bus_standard — dark gray
        (1, 1): (144, 64, 32),     # truck_cargo — dark orange
    }

    # Build flat array of DXT5 blocks
    # Each 16×16 cell = (16/4) × (16/4) = 4×4 = 16 DXT5 blocks
    # Atlas: 256/4 = 64 blocks wide, 256/4 = 64 blocks high

    blocks_wide = W // 4   # 64
    blocks_high = W // 4   # 64
    total_blocks = blocks_wide * blocks_high

    # Default: black
    black_block = _make_dxt5_solid_block(0, 0, 0, 255)
    all_blocks = [black_block] * total_blocks

    # Paint swatch cells
    for (cell_row, cell_col), (r, g, b) in swatches.items():
        color_block = _make_dxt5_solid_block(r, g, b, 255)
        # Each cell is 4×4 DXT5 blocks at block offset
        block_col_start = cell_col * (CELL_PX // 4)  # = cell_col * 4
        block_row_start = cell_row * (CELL_PX // 4)  # = cell_row * 4
        for br in range(4):
            for bc in range(4):
                block_idx = (block_row_start + br) * blocks_wide + (block_col_start + bc)
                all_blocks[block_idx] = color_block

    pixel_data = b''.join(all_blocks)
    # Verify size: 64×64 blocks × 16 bytes = 65536 bytes
    assert len(pixel_data) == blocks_wide * blocks_high * 16, \
        f"Sprite atlas pixel data size mismatch: {len(pixel_data)}"

    header = _write_dds_header(W, W, b'DXT5', mip_levels=1)
    dds_data = header + pixel_data

    out = ROOT / 'assets/textures/vehicles/vehicles_sprite_atlas_d.dds'
    _ensure_dir(out)
    out.write_bytes(dds_data)
    print(f"  Created: assets/textures/vehicles/vehicles_sprite_atlas_d.dds  ({len(dds_data):,} bytes)")


# ===========================================================================
# Building Lightmaps — AO gradient
# ===========================================================================

def _make_ao_gradient_image(size: int = 512) -> Image.Image:
    """Create a 512×512 RGBA lightmap with lighter center, darker edges (AO simulation)."""
    arr = np.full((size, size, 4), 255, dtype=np.uint8)
    cx, cy = size / 2, size / 2
    max_dist = np.sqrt(cx ** 2 + cy ** 2)

    # Create coordinate grids
    ys = np.arange(size)
    xs = np.arange(size)
    xx, yy = np.meshgrid(xs, ys)
    dist = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2)

    # Falloff: center = 210, edge = 140
    centre_val = 210
    edge_val = 140
    vals = (centre_val - (dist / max_dist) * (centre_val - edge_val)).astype(np.uint8)

    arr[:, :, 0] = vals
    arr[:, :, 1] = vals
    arr[:, :, 2] = vals
    arr[:, :, 3] = 255
    return Image.fromarray(arr, 'RGBA')


def generate_building_lightmaps() -> None:
    """18 per-building lightmaps, 512×512 DXT5, 1 mip level."""
    print("\n--- Building lightmaps (512x512 DXT5, 1 mip, AO gradient) ---")

    ao_img = _make_ao_gradient_image(512)
    tmp_png = Path('/tmp/building_lightmap_ao.png')
    ao_img.save(str(tmp_png))

    zones    = ['res', 'com', 'ind']
    tiers    = ['low', 'med', 'high']
    variants = ['01', '02']

    for zone in zones:
        for tier in tiers:
            for var in variants:
                name = f'{zone}_{tier}_{var}_lm.dds'
                out = ROOT / f'assets/3d/buildings/{name}'
                _im_convert([
                    str(tmp_png),
                    '-define', 'dds:compression=dxt5',
                    '-define', 'dds:mipmaps=0',  # 0 extra mips = 1 total level only
                    str(out),
                ])
                print(f"  Created: assets/3d/buildings/{name}  ({out.stat().st_size:,} bytes)")


# ===========================================================================
# Billboard Atlases — DX10 BC3_UNORM_SRGB
# ===========================================================================

def _paint_billboard_strip(zone: str, tier: str) -> Image.Image:
    """Paint a 1024×128 billboard elevation strip with 8 × 128×128 frames.

    Each frame shows a simple building elevation silhouette against transparent sky.
    Frames 0-7 are 8-direction bakes (0°, 45°, 90°, ... 315°).
    For placeholders all 8 frames show the same elevation.
    """
    W, H = 1024, 128
    img = Image.new('RGBA', (W, H), (0, 0, 0, 0))  # transparent background
    draw = ImageDraw.Draw(img)

    FRAME_W = 128
    BORDER_PX = 8   # per spec: 8-texel border per frame

    if zone == 'res':
        body_color = (212, 192, 144, 255)
        win_color  = (160, 185, 210, 255)
        roof_color = (185, 165, 120, 255)
        floors = 2 if tier == 'low' else 4
    elif zone == 'com':
        body_color = (128, 144, 168, 255)
        win_color  = (160, 200, 225, 255)
        roof_color = (90, 100, 115, 255)
        floors = 2 if tier == 'low' else 4
    else:  # ind
        body_color = (140, 140, 140, 255)
        win_color  = (100, 115, 125, 255)
        roof_color = (100, 100, 105, 255)
        floors = 2 if tier == 'low' else 4

    # Building dimensions within each frame's usable area (inside 8px border)
    usable_w = FRAME_W - 2 * BORDER_PX  # 112px
    usable_h = H - 2 * BORDER_PX         # 112px
    bldg_w = int(usable_w * 0.80)
    bldg_h = int(usable_h * (0.60 if tier == 'low' else 0.85))
    bldg_x_offset = (usable_w - bldg_w) // 2
    bldg_y_bottom = usable_h  # align to bottom

    floor_h = bldg_h // floors
    win_w = max(10, bldg_w // 4 - 6)
    win_h = max(8, floor_h - 14)

    for frame in range(8):
        fx = frame * FRAME_W
        # Building body (opaque)
        bx0 = fx + BORDER_PX + bldg_x_offset
        by0 = BORDER_PX + bldg_y_bottom - bldg_h
        bx1 = bx0 + bldg_w
        by1 = BORDER_PX + bldg_y_bottom
        draw.rectangle([bx0, by0, bx1, by1], fill=body_color)

        # Roof
        draw.rectangle([bx0, by0, bx1, by0 + 8], fill=roof_color)

        # Floor bands + windows
        for f in range(floors):
            fy0 = by0 + f * floor_h
            # Floor separator
            draw.line([(bx0, fy0), (bx1, fy0)], fill=roof_color, width=2)
            # Windows (3 per floor row)
            wins_per_floor = 3
            for w in range(wins_per_floor):
                wx = bx0 + w * (bldg_w // wins_per_floor) + (bldg_w // wins_per_floor - win_w) // 2
                wy = fy0 + (floor_h - win_h) // 2 + 2
                draw.rectangle([wx, wy, wx + win_w, wy + win_h], fill=win_color)

    return img


def generate_billboard_atlases() -> None:
    """12 billboard atlases, 1024×128, DX10 BC3_UNORM_SRGB, 4 mips."""
    print("\n--- Billboard atlases (1024x128 DX10 BC3_UNORM_SRGB, 4 mips) ---")

    DXGI_FORMAT_BC3_UNORM_SRGB = 78
    zones    = ['res', 'com', 'ind']
    tiers    = ['low', 'med']
    variants = ['01', '02']

    for zone in zones:
        for tier in tiers:
            for var in variants:
                name = f'{zone}_{tier}_{var}_billboard'
                img = _paint_billboard_strip(zone, tier)

                # Save temporary PNG
                tmp_png = Path(f'/tmp/{name}.png')
                img.save(str(tmp_png))

                # Compress to DXT5 via ImageMagick (standard DDS first)
                tmp_dds = Path(f'/tmp/{name}_im.dds')
                _im_convert([
                    str(tmp_png),
                    '-define', 'dds:compression=dxt5',
                    '-define', 'dds:mipmaps=3',
                    str(tmp_dds),
                ])

                # Read the ImageMagick-generated DDS pixel body (strip the header)
                im_data = tmp_dds.read_bytes()
                # Standard DDS header is 128 bytes
                pixel_body = im_data[128:]

                # Build a DX10 header + pixel body
                # DX10 header = 128 base + 20 DX10 extension = 148 bytes
                dx10_header = _write_dds_header(
                    1024, 128,
                    fourcc=b'DXT5',   # overridden internally for DX10
                    mip_levels=4,
                    is_dx10=True,
                    dxgi_format=DXGI_FORMAT_BC3_UNORM_SRGB,
                )
                dds_final = dx10_header + pixel_body

                out = ROOT / f'assets/3d/buildings/{name}.dds'
                out.write_bytes(dds_final)
                print(f"  Created: assets/3d/buildings/{name}.dds  ({len(dds_final):,} bytes)")


# ===========================================================================
# Entry point
# ===========================================================================

def main() -> None:
    print("=== AI Town — Production Texture Generator ===")
    print(f"Output root: {ROOT}")
    print()

    generate_building_atlas()
    generate_wall_normal_maps()
    generate_wall_specular_maps()
    generate_vehicle_diffuse_atlas()
    generate_vehicle_sprite_atlas()
    generate_building_lightmaps()
    generate_billboard_atlases()

    print("\n=== All production textures generated successfully ===")


if __name__ == '__main__':
    main()
