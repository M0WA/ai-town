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
import colorsys
import os


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
# Cell color assignment (40 assigned cells, rows 0-4)
# ---------------------------------------------------------------------------

def cell_color(row: int, col: int):
    """
    Return a unique (r, g, b) uint8 color for atlas cell (row, col).
    Uses a spread across the HSV color wheel so each of the 40 assigned
    cells has a visually distinct color.
    """
    cell_idx = row * 8 + col
    # Spread hues across the full spectrum using a prime step to avoid clustering.
    hue = (cell_idx * 37) % 360 / 360.0
    sat = 0.6 + (cell_idx % 4) * 0.1    # 0.60 – 0.90
    val = 0.5 + (cell_idx % 3) * 0.15   # 0.50 – 0.80
    r_f, g_f, b_f = colorsys.hsv_to_rgb(hue, sat, val)
    return int(r_f * 255), int(g_f * 255), int(b_f * 255)


# ---------------------------------------------------------------------------
# Color lookup function for the primary 4096x4096 atlas (8x8 grid, 512px cells)
# ---------------------------------------------------------------------------

def make_atlas_color_fn(atlas_width: int, atlas_height: int,
                        grid_cols: int, grid_rows: int):
    """
    Return a get_color_fn(x, y) -> (r, g, b) that maps pixel coordinates to
    per-cell colors.  Cells beyond the assigned range (rows 5-7) use a neutral
    dark grey to indicate RESERVED status.
    """
    cell_w = atlas_width // grid_cols   # 512 for 4096/8
    cell_h = atlas_height // grid_rows  # 512 for 4096/8
    assigned_rows = 5                   # rows 0-4

    def get_color(x: int, y: int):
        row = y // cell_h
        col = x // cell_w
        # Clamp to grid bounds (handles edge pixels at exact atlas boundary)
        row = min(row, grid_rows - 1)
        col = min(col, grid_cols - 1)
        if row < assigned_rows:
            return cell_color(row, col)
        # Reserved rows: neutral mid-grey
        return (80, 80, 80)

    return get_color


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
        100-103: pfBBitMask = 0
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
    """Generate a DXT1 sRGB DDS file with per-cell unique colors."""
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
            magic = f.read(4)
            f.seek(8)   # skip dwSize
            h, w = struct.unpack('<II', f.read(8))      # offsets 8,12 = height, width
            # Actually: offset 4=dwSize, 8=dwFlags, 12=dwHeight, 16=dwWidth, 28=dwMipMapCount
            # Re-read properly:
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
    # Verify
    # -----------------------------------------------------------------------
    verify_expected_sizes(primary_path, fallback_path)
    verify_headers(primary_path, fallback_path)

    print("\n=== Generation complete ===")
    print(f"  {primary_path}")
    print(f"    {os.path.getsize(primary_path):,} bytes")
    print(f"  {fallback_path}")
    print(f"    {os.path.getsize(fallback_path):,} bytes")
