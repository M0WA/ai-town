#!/usr/bin/env python3
"""
Generate building atlas DDS files for AI Town Phase 11e.

Outputs:
  assets/textures/buildings/buildings_atlas_d.dds     4096x4096 DXT1 sRGB, 5 mip levels
  assets/textures/buildings/buildings_atlas_d_2k.dds  2048x2048 DXT1 sRGB, 4 mip levels

DDS format: standard 128-byte header (no DX10 extension), FourCC = "DXT1".
This matches the spec byte-size table in architecture/asset-standards/2d-texture-standards.md.

Expected sizes:
  buildings_atlas_d.dds    = 11,174,016 bytes  (128 header + 11,173,888 data)
  buildings_atlas_d_2k.dds =  2,785,408 bytes  (128 header +  2,785,280 data)
"""

import struct
import os


# ---------------------------------------------------------------------------
# Per-variant architectural color table
# ---------------------------------------------------------------------------
# Each entry: (base_r, base_g, base_b, stripe1_r, stripe1_g, stripe1_b,
#              stripe2_r, stripe2_g, stripe2_b)
# stripe colors are applied as horizontal bands at V=0.30–0.38 and V=0.62–0.70
# of the cell height, to suggest window bands / cladding breaks.
# The base color fills the rest of the cell.
#
# Cell assignment: (row, col) → variant name
#   Row 0: res_low_01..04, res_med_01..04
#   Row 1: res_high_01..04, com_low_01..04
#   Row 2: com_med_01..04, com_high_01..04
#   Row 3: ind_low_01..04, ind_med_01..04
#   Row 4: ind_high_01..04, svc_fire/police/power/water
#   Row 5, col 0: ROOF_CELL

# Stripe band V-fractions (start, end) for two stripe bands per cell
_STRIPE1_V = (0.28, 0.36)
_STRIPE2_V = (0.60, 0.68)

# Table format: (row, col) -> (base_rgb, stripe1_rgb, stripe2_rgb)
# Each RGB is a 3-tuple of ints 0-255.
_CELL_COLORS = {
    # ---- Row 0: Residential Low (cols 0-3) ----
    # 01 red brick
    (0, 0): ((178,  78,  56), (200, 160, 100), (155,  60,  42)),
    # 02 cream render
    (0, 1): ((220, 210, 185), (240, 230, 205), (195, 185, 158)),
    # 03 tan sandstone
    (0, 2): ((193, 168, 120), (210, 190, 145), (165, 140,  96)),
    # 04 grey pebbledash
    (0, 3): ((155, 152, 148), (175, 170, 165), (128, 125, 120)),

    # ---- Row 0: Residential Medium (cols 4-7) ----
    # 01 buff brick
    (0, 4): ((200, 175, 120), (220, 200, 150), (175, 150,  95)),
    # 02 white render + balcony stripe
    (0, 5): ((235, 235, 230), (180, 195, 215), (215, 215, 210)),
    # 03 terracotta tile panels
    (0, 6): ((195, 105,  65), (215, 140, 100), (168,  82,  48)),
    # 04 pale render + green spandrel stripe
    (0, 7): ((210, 215, 200), ( 90, 140,  95), (192, 198, 182)),

    # ---- Row 1: Residential High (cols 0-3) ----
    # 01 blue-grey curtain wall
    (1, 0): ((115, 140, 168), (145, 168, 195), ( 88, 112, 140)),
    # 02 warm beige precast panels
    (1, 1): ((205, 188, 158), (225, 210, 182), (178, 160, 130)),
    # 03 white with horizontal bands
    (1, 2): ((235, 235, 232), (160, 175, 190), (218, 218, 215)),
    # 04 amber glass + concrete
    (1, 3): ((195, 155,  75), (160, 148, 130), (215, 175,  90)),

    # ---- Row 1: Commercial Low (cols 4-7) ----
    # 01 red shopfront awning
    (1, 4): ((188,  45,  38), (230, 215, 195), (162,  35,  28)),
    # 02 blue-grey cladding
    (1, 5): ((108, 128, 150), (145, 162, 180), ( 82, 100, 122)),
    # 03 brick with signage band
    (1, 6): ((165,  72,  50), (235, 228, 210), (145,  58,  38)),
    # 04 white render + green fascia
    (1, 7): ((232, 232, 228), ( 72, 142,  80), (215, 215, 210)),

    # ---- Row 2: Commercial Medium (cols 0-3) ----
    # 01 silver-grey aluminium cladding
    (2, 0): ((188, 192, 195), (208, 212, 215), (162, 165, 168)),
    # 02 dark blue reflective glass
    (2, 1): (( 35,  60, 105), ( 55,  82, 135), ( 22,  42,  82)),
    # 03 cream stone facade
    (2, 2): ((218, 208, 182), (235, 228, 205), (195, 185, 158)),
    # 04 bronze-tinted glass curtain wall
    (2, 3): ((148, 108,  55), (175, 138,  82), (122,  85,  35)),

    # ---- Row 2: Commercial High (cols 4-7) ----
    # 01 deep blue mirror glass
    (2, 4): (( 25,  48,  98), ( 45,  72, 128), ( 12,  30,  72)),
    # 02 silver steel + clear glass
    (2, 5): ((198, 205, 212), (155, 185, 210), (175, 182, 188)),
    # 03 green-tinted glass
    (2, 6): (( 55, 118,  85), ( 78, 148, 108), ( 35,  90,  62)),
    # 04 gold reflective glass
    (2, 7): ((195, 162,  48), (218, 188,  72), (168, 138,  25)),

    # ---- Row 3: Industrial Low (cols 0-3) ----
    # 01 corrugated steel grey
    (3, 0): ((148, 152, 155), (168, 172, 175), (122, 125, 128)),
    # 02 red/rust metal cladding
    (3, 1): ((168,  68,  38), (192, 105,  72), (142,  48,  22)),
    # 03 beige brick
    (3, 2): ((195, 178, 138), (215, 198, 162), (168, 150, 112)),
    # 04 dark olive corrugated
    (3, 3): (( 95, 108,  72), (118, 132,  90), ( 72,  82,  52)),

    # ---- Row 3: Industrial Medium (cols 4-7) ----
    # 01 white corrugated steel
    (3, 4): ((228, 230, 228), (200, 208, 215), (210, 212, 210)),
    # 02 yellow/ochre brick
    (3, 5): ((198, 168,  65), (218, 192,  95), (172, 142,  42)),
    # 03 dark blue metal
    (3, 6): (( 42,  62, 105), ( 62,  85, 132), ( 25,  42,  80)),
    # 04 light grey precast
    (3, 7): ((185, 188, 192), (205, 208, 212), (158, 162, 165)),

    # ---- Row 4: Industrial High (cols 0-3) ----
    # 01 bare concrete panels
    (4, 0): ((158, 158, 152), (178, 178, 172), (132, 132, 125)),
    # 02 dark steel + orange safety stripe
    (4, 1): (( 62,  68,  75), (215, 118,  32), ( 42,  48,  55)),
    # 03 grey/blue industrial
    (4, 2): (( 88, 108, 128), (112, 132, 152), ( 65,  82, 102)),
    # 04 weathered steel + rust patches
    (4, 3): ((105,  85,  68), (148,  78,  38), ( 82,  65,  48)),

    # ---- Row 4: Service buildings (cols 4-7) ----
    # svc_fire_station: red brick + bay doors, off-white trim
    (4, 4): ((178,  52,  38), (235, 228, 210), (155,  38,  25)),
    # svc_police_station: dark blue/navy brick + white signage stripe
    (4, 5): (( 28,  45,  88), (235, 235, 232), ( 18,  30,  68)),
    # svc_power_plant: grey concrete + yellow hazard stripe
    (4, 6): ((145, 148, 145), (218, 192,  35), (118, 120, 118)),
    # svc_water_tower: weathered steel + rust, pale blue tank
    (4, 7): ((118, 105,  88), (178, 212, 225), ( 95,  82,  65)),

    # ---- Row 5, col 0: ROOF_CELL ----
    # flat grey/beige roof material
    (5, 0): ((168, 162, 148), (148, 142, 128), (182, 175, 160)),
}

# Reserved cells (rows 5-7 except the roof cell) use neutral dark grey.
_RESERVED_COLOR = (72, 72, 72)


# ---------------------------------------------------------------------------
# Color lookup function — architectural per-cell scheme
# ---------------------------------------------------------------------------

def get_architectural_color(row: int, col: int, cell_w: int, cell_h: int,
                             px_in_cell: int, py_in_cell: int):
    """
    Return (r, g, b) for a pixel at (px_in_cell, py_in_cell) within the cell.
    Applies base color + two horizontal stripe bands to suggest window bands.
    """
    key = (row, col)
    if key not in _CELL_COLORS:
        return _RESERVED_COLOR

    base_rgb, stripe1_rgb, stripe2_rgb = _CELL_COLORS[key]

    # Compute V-fraction within the cell (0.0 = top, 1.0 = bottom)
    v = py_in_cell / max(1, cell_h - 1)

    if _STRIPE1_V[0] <= v < _STRIPE1_V[1]:
        return stripe1_rgb
    if _STRIPE2_V[0] <= v < _STRIPE2_V[1]:
        return stripe2_rgb
    return base_rgb


# ---------------------------------------------------------------------------
# Color lookup function for the atlas
# ---------------------------------------------------------------------------

def make_atlas_color_fn(atlas_width: int, atlas_height: int,
                        grid_cols: int, grid_rows: int):
    """
    Return a get_color_fn(x, y) -> (r, g, b) that maps pixel coordinates to
    per-cell architectural colors with stripe patterns.
    """
    cell_w = atlas_width  // grid_cols
    cell_h = atlas_height // grid_rows

    def get_color(x: int, y: int):
        row = min(y // cell_h, grid_rows - 1)
        col = min(x // cell_w, grid_cols - 1)
        px_in_cell = x - col * cell_w
        py_in_cell = y - row * cell_h
        return get_architectural_color(row, col, cell_w, cell_h,
                                       px_in_cell, py_in_cell)

    return get_color


# ---------------------------------------------------------------------------
# DXT1 / BC1 helpers
# ---------------------------------------------------------------------------

def rgb_to_565(r: int, g: int, b: int) -> int:
    """Pack uint8 RGB into a 16-bit RGB565 value."""
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def encode_dxt1_block_uniform(r: int, g: int, b: int) -> bytes:
    """
    Encode a uniform-color 4x4 block as DXT1 (8 bytes).
    Both color endpoints are set to the same 565 value; all 16 indices are 0.
    """
    c565 = rgb_to_565(r, g, b)
    # Two identical 16-bit endpoints (little-endian), then 4 bytes of zero indices.
    return struct.pack('<HH4s', c565, c565, b'\x00\x00\x00\x00')


def encode_dxt1_image(width: int, height: int, get_color_fn) -> bytes:
    """
    Encode a full image to DXT1.
    get_color_fn(x, y) -> (r, g, b) uint8 for the pixel at column x, row y.
    Width and height must be multiples of 4 (or at least 4 for mip tails).
    """
    blocks_x = max(1, (width + 3) // 4)
    blocks_y = max(1, (height + 3) // 4)
    out = bytearray()
    for by in range(blocks_y):
        for bx in range(blocks_x):
            # Sample the top-left pixel of each block as representative color.
            px = bx * 4
            py = by * 4
            r, g, b = get_color_fn(px, py)
            out += encode_dxt1_block_uniform(r, g, b)
    return bytes(out)


# ---------------------------------------------------------------------------
# DDS header builder (standard 128-byte, no DX10 extension)
# ---------------------------------------------------------------------------

def build_dds_header(width: int, height: int, mip_levels: int) -> bytes:
    """
    Build a standard 128-byte DDS file header for a DXT1/BC1 compressed texture.
    Layout (byte offsets):
      0-3   : magic "DDS "
      4-7   : dwSize = 124
      8-11  : dwFlags
      12-15 : dwHeight
      16-19 : dwWidth
      20-23 : dwPitchOrLinearSize  (linear size of mip 0 for compressed textures)
      24-27 : dwDepth = 0
      28-31 : dwMipMapCount
      32-75 : dwReserved1[11] (44 bytes, zeroed)
      76-107: DDPIXELFORMAT (32 bytes)
        76-79 : pfSize = 32
        80-83 : pfFlags = 0x4 (DDPF_FOURCC)
        84-87 : pfFourCC = "DXT1"
        88-91 : pfRGBBitCount = 0
        92-95 : pfRBitMask = 0
        96-99 : pfGBitMask = 0
        100-103: pfGBitMask = 0
        104-107: pfABitMask = 0
      108-123: DDSCAPS
        108-111: dwCaps  = DDSCAPS_COMPLEX | DDSCAPS_TEXTURE | DDSCAPS_MIPMAP
        112-115: dwCaps2 = 0
        116-119: dwCaps3 = 0
        120-123: dwCaps4 = 0
      124-127: dwReserved2 = 0
    Total: 128 bytes.
    """
    DDSD_CAPS        = 0x00000001
    DDSD_HEIGHT      = 0x00000002
    DDSD_WIDTH       = 0x00000004
    DDSD_PIXELFORMAT = 0x00001000
    DDSD_MIPMAPCOUNT = 0x00020000
    DDSD_LINEARSIZE  = 0x00080000

    flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_MIPMAPCOUNT | DDSD_LINEARSIZE

    # Linear size of the top-level mip for DXT1: (width/4) * (height/4) * 8 bytes per block
    blocks_x = max(1, (width  + 3) // 4)
    blocks_y = max(1, (height + 3) // 4)
    linear_size = blocks_x * blocks_y * 8

    DDPF_FOURCC = 0x00000004

    DDSCAPS_COMPLEX = 0x00000008
    DDSCAPS_TEXTURE = 0x00001000
    DDSCAPS_MIPMAP  = 0x00400000
    caps1 = DDSCAPS_COMPLEX | DDSCAPS_TEXTURE | DDSCAPS_MIPMAP

    magic = b'DDS '

    # dwSize through dwMipMapCount
    hdr = struct.pack('<IIIIIII',
        124,          # dwSize
        flags,        # dwFlags
        height,       # dwHeight
        width,        # dwWidth
        linear_size,  # dwPitchOrLinearSize
        0,            # dwDepth
        mip_levels,   # dwMipMapCount
    )
    # dwReserved1[11] (44 bytes)
    hdr += bytes(44)
    # DDPIXELFORMAT (32 bytes)
    hdr += struct.pack('<II4sIIIII',
        32,          # pfSize
        DDPF_FOURCC, # pfFlags
        b'DXT1',     # pfFourCC
        0,           # pfRGBBitCount
        0, 0, 0, 0,  # R,G,B,A masks
    )
    # DDSCAPS (16 bytes)
    hdr += struct.pack('<IIII', caps1, 0, 0, 0)
    # dwReserved2 (4 bytes)
    hdr += struct.pack('<I', 0)

    assert len(hdr) == 124, f"Header body should be 124 bytes, got {len(hdr)}"
    return magic + hdr  # 4 + 124 = 128 bytes


# ---------------------------------------------------------------------------
# Mip data generator
# ---------------------------------------------------------------------------

def generate_mip_data(base_width: int, base_height: int, mip_levels: int,
                      get_color_fn) -> bytes:
    """
    Generate DXT1-compressed data for all mip levels.
    For each mip level N: dimensions are max(1, base >> N) x max(1, base >> N).
    The color function is queried in terms of the base-level atlas coordinates —
    at mip level N we sample at stride 2^N to produce the correctly downscaled color.
    """
    all_data = bytearray()
    for mip in range(mip_levels):
        scale = 1 << mip  # 1, 2, 4, 8, 16 for mips 0-4
        mip_w = max(1, base_width  >> mip)
        mip_h = max(1, base_height >> mip)

        def make_scaled_color_fn(s):
            def scaled_get_color(x, y):
                # Map mip-level pixel back to base-level coordinates
                return get_color_fn(x * s, y * s)
            return scaled_get_color

        mip_data = encode_dxt1_image(mip_w, mip_h, make_scaled_color_fn(scale))
        all_data += mip_data

    return bytes(all_data)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def generate_dds(output_path: str, width: int, height: int, mip_levels: int,
                 grid_cols: int = 8, grid_rows: int = 8):
    """Generate a DXT1 sRGB DDS file with per-cell architectural colors."""
    get_color = make_atlas_color_fn(width, height, grid_cols, grid_rows)

    print(f"  Generating mip data for {width}x{height}, {mip_levels} mip levels...")
    mip_data = generate_mip_data(width, height, mip_levels, get_color)

    header = build_dds_header(width, height, mip_levels)
    assert len(header) == 128, f"Expected 128-byte header, got {len(header)}"

    total = len(header) + len(mip_data)
    print(f"  Header: {len(header)} bytes, data: {len(mip_data)} bytes, total: {total} bytes")

    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, 'wb') as f:
        f.write(header)
        f.write(mip_data)

    return total


def generate_source_png(output_path: str, width: int, height: int,
                        grid_cols: int = 8, grid_rows: int = 8):
    """
    Generate the source PNG authoring file (buildings_atlas_d.png) at the
    specified resolution with per-cell architectural colors and stripe patterns.
    Requires Pillow.  Called from the main entry point after DDS generation.
    """
    try:
        from PIL import Image  # noqa: PLC0415
    except ImportError:
        print("  Skipping PNG generation: Pillow not installed.")
        return

    img = Image.new("RGB", (width, height))
    pixels = img.load()

    get_color = make_atlas_color_fn(width, height, grid_cols, grid_rows)
    for y in range(height):
        for x in range(width):
            pixels[x, y] = get_color(x, y)

    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    img.save(output_path, "PNG")
    print(f"  Saved source PNG: {output_path} ({os.path.getsize(output_path):,} bytes)")


def verify_expected_sizes(primary_path: str, fallback_path: str):
    """Verify the generated files match the spec byte sizes."""
    EXPECTED_PRIMARY  = 11_174_016
    EXPECTED_FALLBACK =  2_785_408

    p_size = os.path.getsize(primary_path)
    f_size = os.path.getsize(fallback_path)

    print(f"\nByte-size verification:")
    print(f"  {primary_path}:  {p_size:>12,} bytes  (expected {EXPECTED_PRIMARY:>12,})  {'OK' if p_size == EXPECTED_PRIMARY else 'MISMATCH'}")
    print(f"  {fallback_path}: {f_size:>12,} bytes  (expected {EXPECTED_FALLBACK:>12,})  {'OK' if f_size == EXPECTED_FALLBACK else 'MISMATCH'}")

    if p_size != EXPECTED_PRIMARY:
        raise RuntimeError(
            f"Primary atlas size mismatch: got {p_size}, expected {EXPECTED_PRIMARY}")
    if f_size != EXPECTED_FALLBACK:
        raise RuntimeError(
            f"Fallback atlas size mismatch: got {f_size}, expected {EXPECTED_FALLBACK}")
    print("  All sizes match spec.")


def verify_headers(primary_path: str, fallback_path: str):
    """Read back the DDS headers and confirm magic, dimensions, mip count."""
    print("\nHeader verification:")
    for path, expected_w, expected_h, expected_mips in [
        (primary_path,  4096, 4096, 5),
        (fallback_path, 2048, 2048, 4),
    ]:
        with open(path, 'rb') as f:
            magic = f.read(4)           # offset 0
            _size  = struct.unpack('<I', f.read(4))[0]  # offset 4
            _flags = struct.unpack('<I', f.read(4))[0]  # offset 8
            height = struct.unpack('<I', f.read(4))[0]  # offset 12
            width  = struct.unpack('<I', f.read(4))[0]  # offset 16
            _pitch = struct.unpack('<I', f.read(4))[0]  # offset 20
            _depth = struct.unpack('<I', f.read(4))[0]  # offset 24
            mips   = struct.unpack('<I', f.read(4))[0]  # offset 28
            f.seek(84)
            fourcc = f.read(4)                           # offset 84 = pfFourCC

        ok_magic  = magic == b'DDS '
        ok_w      = width  == expected_w
        ok_h      = height == expected_h
        ok_mips   = mips   == expected_mips
        ok_fourcc = fourcc == b'DXT1'

        status = 'OK' if all([ok_magic, ok_w, ok_h, ok_mips, ok_fourcc]) else 'MISMATCH'
        print(f"  {os.path.basename(path)}: magic={magic} {width}x{height} mips={mips} fourcc={fourcc}  [{status}]")

        if not all([ok_magic, ok_w, ok_h, ok_mips, ok_fourcc]):
            raise RuntimeError(f"Header verification failed for {path}")
    print("  All headers valid.")


if __name__ == '__main__':
    import sys

    # Resolve repo root relative to this script's location
    script_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root  = os.path.dirname(script_dir)

    primary_path  = os.path.join(repo_root, 'assets', 'textures', 'buildings',
                                 'buildings_atlas_d.dds')
    fallback_path = os.path.join(repo_root, 'assets', 'textures', 'buildings',
                                 'buildings_atlas_d_2k.dds')
    png_path      = os.path.join(repo_root, 'assets', 'textures', 'buildings',
                                 'buildings_atlas_d.png')

    # -----------------------------------------------------------------------
    # Expected byte sizes (from 2d-texture-standards.md §DDS Reference Byte-Size table)
    # -----------------------------------------------------------------------
    # Primary  4096x4096 DXT1, 5-mip: 128 + 8388608+2097152+524288+131072+32768 = 128+11173888 = 11174016
    # Fallback 2048x2048 DXT1, 4-mip: 128 + 2097152+524288+131072+32768         = 128+2785280  = 2785408
    EXPECTED_PRIMARY  = 11_174_016
    EXPECTED_FALLBACK =  2_785_408

    # Quick pre-flight check of our size formulas
    def dxt1_mip_bytes(w, h):
        bx = max(1, (w + 3) // 4)
        by = max(1, (h + 3) // 4)
        return bx * by * 8

    primary_data_bytes = sum(dxt1_mip_bytes(max(1, 4096 >> m), max(1, 4096 >> m))
                             for m in range(5))
    fallback_data_bytes = sum(dxt1_mip_bytes(max(1, 2048 >> m), max(1, 2048 >> m))
                              for m in range(4))
    assert 128 + primary_data_bytes  == EXPECTED_PRIMARY,  \
        f"Formula check failed: {128 + primary_data_bytes} != {EXPECTED_PRIMARY}"
    assert 128 + fallback_data_bytes == EXPECTED_FALLBACK, \
        f"Formula check failed: {128 + fallback_data_bytes} != {EXPECTED_FALLBACK}"

    print("=== AI Town Phase 11e — Building Atlas DDS Generator ===\n")

    # -----------------------------------------------------------------------
    # Primary atlas: 4096x4096, 5 mip levels
    # -----------------------------------------------------------------------
    print(f"Generating primary atlas: {primary_path}")
    primary_total = generate_dds(primary_path, 4096, 4096, 5)

    # -----------------------------------------------------------------------
    # Fallback atlas: 2048x2048, 4 mip levels
    # The 2k atlas uses the same 8x8 color scheme; the atlas is just smaller
    # so each cell maps to 256x256 px (512x512 at full res, halved for 2k).
    # We generate it fresh with its own color fn at 2048x2048 resolution.
    # -----------------------------------------------------------------------
    print(f"\nGenerating fallback atlas: {fallback_path}")
    fallback_total = generate_dds(fallback_path, 2048, 2048, 4)

    # -----------------------------------------------------------------------
    # Source PNG: 2048x2048, 8x8 grid at 256px per cell
    # This is the authoring intermediate read by validate_assets.py Check #28.
    # -----------------------------------------------------------------------
    print(f"\nGenerating source PNG: {png_path}")
    generate_source_png(png_path, 2048, 2048, 8, 8)

    # -----------------------------------------------------------------------
    # Verify
    # -----------------------------------------------------------------------
    verify_expected_sizes(primary_path, fallback_path)
    verify_headers(primary_path, fallback_path)

    print("\n=== Generation complete ===")
    print(f"  {primary_path}")
    print(f"    {os.path.getsize(primary_path):,} bytes")
    print(f"  {fallback_path}")
    print(f"    {os.path.getsize(fallback_path):,} bytes")
    print(f"  {png_path}")
    print(f"    {os.path.getsize(png_path):,} bytes")
