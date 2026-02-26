#!/usr/bin/env python3
"""
generate_terrain_textures.py — Phase 5 terrain texture DDS generator.

Produces 8 technically-valid DDS files for the AI Town terrain system:
  4 diffuse textures (_d.dds): DXT1/BC1, 4 mip levels (2048→1024→512→256)
  4 normal maps  (_n.dds): DXT5/BC3 (DXT5nm swizzle), 4 mip levels

These are procedurally-generated placeholder assets for Phase 5.
Artistically authored replacements are scheduled for Phase 9.

Compression approach
--------------------
DXT1 and DXT5 both operate on 4x4 texel blocks.

DXT1 (BC1) per block — 8 bytes:
  2 × RGB565 endpoints  (4 bytes)
  16 × 2-bit colour indices (4 bytes)

DXT5 (BC3) per block — 16 bytes:
  2 × 8-bit alpha endpoints (2 bytes)
  16 × 3-bit alpha indices  (6 bytes)
  2 × RGB565 colour endpoints (4 bytes)
  16 × 2-bit colour indices   (4 bytes)

For the Phase 5 procedural generation we use a simplified block encoder:
  - Each 4x4 block is encoded from its actual 16 pixel colours (computed
    procedurally per texel).  This produces correct DXT1/DXT5 bitstreams
    that any conformant decoder (Irrlicht / OpenGL) will decompress cleanly.
  - The encoder picks the two most-extreme colours in each block as
    endpoints, then assigns each texel to the nearest endpoint or the two
    interpolated values.  This is a standard "min/max endpoint selection"
    encoder — not rate-distortion optimal, but perfectly valid.
  - DXT5nm normal maps have their X component in the alpha channel and Y in
    the green channel; blue is set to 127, red to 0, matching the DXT5nm
    swizzle required by the terrain shader.

DDS header layout
-----------------
All sizes in bytes.  Header is always 128 bytes (magic 4 + DDSURFACEDESC2 124).
We use the legacy FourCC path (not DX10 extended header) so that Irrlicht's
built-in DDS loader recognises the files without requiring the DX10 extension.

    Offset  Size  Field
    0       4     Magic "DDS "
    4       4     dwSize = 124
    8       4     dwFlags  (DDSD_* bitmask)
    12      4     dwHeight
    16      4     dwWidth
    20      4     dwPitchOrLinearSize  (byte size of top mip level)
    24      4     dwDepth = 0
    28      4     dwMipMapCount = 4
    32      44    reserved1[11]
    76      32    DDPIXELFORMAT (ddspf)
      76    4       pfSize = 32
      80    4       pfFlags = DDPF_FOURCC
      84    4       pfFourCC  ("DXT1" or "DXT5")
      88    4       pfRGBBitCount = 0
      92    4       pfRBitMask = 0
      96    4       pfGBitMask = 0
      100   4       pfBBitMask = 0
      104   4       pfABitMask = 0
    108     4     dwCaps   (DDSCAPS_* bitmask)
    112     4     dwCaps2 = 0
    116     4     dwCaps3 = 0
    120     4     dwCaps4 = 0
    124     4     dwReserved2 = 0
    128         <- pixel data starts here

Usage
-----
    python3 tools/generate_terrain_textures.py
    # writes 8 DDS files to assets/terrain/
"""

import struct
import math
import os

# ---------------------------------------------------------------------------
# DDS constants
# ---------------------------------------------------------------------------

DDS_MAGIC = b"DDS "

DDSD_CAPS        = 0x00000001
DDSD_HEIGHT      = 0x00000002
DDSD_WIDTH       = 0x00000004
DDSD_PIXELFORMAT = 0x00001000
DDSD_MIPMAPCOUNT = 0x00020000
DDSD_LINEARSIZE  = 0x00080000

DDPF_FOURCC = 0x00000004

DDSCAPS_COMPLEX = 0x00000008
DDSCAPS_MIPMAP  = 0x00400000
DDSCAPS_TEXTURE = 0x00001000

FOURCC_DXT1 = b"DXT1"
FOURCC_DXT5 = b"DXT5"

MIP_LEVELS = 4   # 2048 → 1024 → 512 → 256

# ---------------------------------------------------------------------------
# Noise helpers (standard-library only — no numpy/PIL)
# ---------------------------------------------------------------------------

def _hash(x: int, y: int, seed: int = 0) -> int:
    """Fast integer hash (Wang hash variant)."""
    h = (x * 1619 + y * 31337 + seed * 6971) & 0xFFFFFFFF
    h = ((h >> 16) ^ h) * 0x45D9F3B & 0xFFFFFFFF
    h = ((h >> 16) ^ h) * 0x45D9F3B & 0xFFFFFFFF
    h = (h >> 16) ^ h
    return h & 0xFFFFFFFF


def _fhash(x: int, y: int, seed: int = 0) -> float:
    """Return a float in [0.0, 1.0] from integer coords."""
    return _hash(x, y, seed) / 0xFFFFFFFF


def _smooth_noise(fx: float, fy: float, seed: int = 0) -> float:
    """Bilinear-interpolated noise in [0.0, 1.0]."""
    ix = int(math.floor(fx))
    iy = int(math.floor(fy))
    tx = fx - ix
    ty = fy - iy
    # Smoothstep
    tx = tx * tx * (3.0 - 2.0 * tx)
    ty = ty * ty * (3.0 - 2.0 * ty)
    v00 = _fhash(ix,     iy,     seed)
    v10 = _fhash(ix + 1, iy,     seed)
    v01 = _fhash(ix,     iy + 1, seed)
    v11 = _fhash(ix + 1, iy + 1, seed)
    return (v00 * (1 - tx) + v10 * tx) * (1 - ty) + \
           (v01 * (1 - tx) + v11 * tx) * ty


def _fbm(x: float, y: float, seed: int, octaves: int = 4,
         persistence: float = 0.5, lacunarity: float = 2.0) -> float:
    """Fractional Brownian Motion, returns value in [0.0, 1.0] approx."""
    value = 0.0
    amplitude = 0.5
    frequency = 1.0
    max_val = 0.0
    for _ in range(octaves):
        value += _smooth_noise(x * frequency, y * frequency, seed) * amplitude
        max_val += amplitude
        amplitude *= persistence
        frequency *= lacunarity
    return value / max_val


# ---------------------------------------------------------------------------
# RGB <-> RGB565 conversion
# ---------------------------------------------------------------------------

def _rgb_to_565(r: int, g: int, b: int) -> int:
    """Pack 8-bit R,G,B into a 16-bit RGB565 word."""
    r5 = (r >> 3) & 0x1F
    g6 = (g >> 2) & 0x3F
    b5 = (b >> 3) & 0x1F
    return (r5 << 11) | (g6 << 5) | b5


def _565_to_rgb(c: int):
    """Unpack RGB565 to 8-bit R,G,B (rounded)."""
    r5 = (c >> 11) & 0x1F
    g6 = (c >> 5)  & 0x3F
    b5 = c         & 0x1F
    r = (r5 << 3) | (r5 >> 2)
    g = (g6 << 2) | (g6 >> 4)
    b = (b5 << 3) | (b5 >> 2)
    return r, g, b


def _colour_dist_sq(r0: int, g0: int, b0: int,
                    r1: int, g1: int, b1: int) -> int:
    dr = r0 - r1
    dg = g0 - g1
    db = b0 - b1
    return dr * dr + dg * dg + db * db


# ---------------------------------------------------------------------------
# DXT1 block encoder
# ---------------------------------------------------------------------------

def _encode_dxt1_block(pixels) -> bytes:
    """
    Encode a list of 16 (r,g,b) tuples into an 8-byte DXT1 block.

    Endpoint selection: min/max across all 16 pixels using a luminance-
    weighted distance metric.  Produces valid (if not rate-distortion-
    optimal) DXT1 bitstreams.

    For blocks with no colour variance (solid colour), both endpoints are
    set to the same colour and all indices are 0.
    """
    # Find min/max luminance pixel for endpoints
    def lum(rgb):
        return rgb[0] * 299 + rgb[1] * 587 + rgb[2] * 114

    lum_values = [lum(p) for p in pixels]
    min_idx = lum_values.index(min(lum_values))
    max_idx = lum_values.index(max(lum_values))

    c0_rgb = pixels[max_idx]   # brighter endpoint → c0 > c1 (4-colour mode)
    c1_rgb = pixels[min_idx]

    c0 = _rgb_to_565(*c0_rgb)
    c1 = _rgb_to_565(*c1_rgb)

    # Ensure c0 > c1 for 4-colour mode (no transparency).
    # If equal, force c0 >= c1 by incrementing one channel of c0.
    if c0 <= c1:
        c0, c1 = c1, c0
        c0_rgb, c1_rgb = c1_rgb, c0_rgb
    if c0 == c1:
        # Solid colour block
        indices = 0
        return struct.pack("<HHI", c0, c1, indices)

    # Decode back to 8-bit (quantisation artefacts included)
    r0, g0, b0 = _565_to_rgb(c0)
    r1, g1, b1 = _565_to_rgb(c1)

    # 4-colour interpolants
    r2 = (2 * r0 + r1 + 1) // 3
    g2 = (2 * g0 + g1 + 1) // 3
    b2 = (2 * b0 + b1 + 1) // 3

    r3 = (r0 + 2 * r1 + 1) // 3
    g3 = (g0 + 2 * g1 + 1) // 3
    b3 = (b0 + 2 * b1 + 1) // 3

    palette = [
        (r0, g0, b0),
        (r1, g1, b1),
        (r2, g2, b2),
        (r3, g3, b3),
    ]

    # Assign indices
    indices = 0
    for i, (r, g, b) in enumerate(pixels):
        best = 0
        best_dist = 10 ** 9
        for j, (pr, pg, pb) in enumerate(palette):
            d = _colour_dist_sq(r, g, b, pr, pg, pb)
            if d < best_dist:
                best_dist = d
                best = j
        indices |= (best << (i * 2))

    return struct.pack("<HHI", c0, c1, indices)


# ---------------------------------------------------------------------------
# DXT5 block encoder
# ---------------------------------------------------------------------------

def _encode_alpha_block_dxt5(alphas) -> bytes:
    """
    Encode 16 alpha values (0..255) into a 6-byte DXT5 alpha block.
    Uses 8-alpha mode (a0 > a1): 8 interpolated values including 0 and 255
    are available.  We pick min/max as endpoints and assign each texel to
    the nearest interpolated value.
    """
    a0 = max(alphas)
    a1 = min(alphas)

    if a0 == a1:
        # Solid alpha
        bits = 0
        return struct.pack("BB", a0, a1) + struct.pack("<I", bits)[:3] + \
               struct.pack("<I", bits)[:3]

    # Use 8-alpha mode: a0 > a1
    # Interpolated values: a0, a1, (6a0+1a1)/7, (5a0+2a1)/7,
    #                      (4a0+3a1)/7, (3a0+4a1)/7, (2a0+5a1)/7, (1a0+6a1)/7
    palette = [a0, a1]
    for i in range(1, 7):
        palette.append((a0 * (7 - i) + a1 * i + 3) // 7)

    # Build 48-bit index block (16 × 3 bits)
    idx_bits = 0
    for i, a in enumerate(alphas):
        best = 0
        best_d = abs(a - palette[0])
        for j in range(1, 8):
            d = abs(a - palette[j])
            if d < best_d:
                best_d = d
                best = j
        idx_bits |= (best << (i * 3))

    # Pack 48 bits (6 bytes) little-endian
    idx_bytes = struct.pack("<Q", idx_bits)[:6]
    return struct.pack("BB", a0, a1) + idx_bytes


def _encode_dxt5_block(pixels) -> bytes:
    """
    Encode a list of 16 (r,g,b,a) tuples into a 16-byte DXT5 block.
    Alpha component uses the DXT5 alpha section; RGB uses DXT1 sub-block.
    """
    alphas = [p[3] for p in pixels]
    rgb_pixels = [(p[0], p[1], p[2]) for p in pixels]

    alpha_block = _encode_alpha_block_dxt5(alphas)
    colour_block = _encode_dxt1_block(rgb_pixels)
    return alpha_block + colour_block


# ---------------------------------------------------------------------------
# Mip chain helpers
# ---------------------------------------------------------------------------

def _downsample_2x(pixels, width: int, height: int):
    """
    Simple 2x2 box downsample.  Returns (new_pixels, new_width, new_height).
    pixels is a flat list of (r,g,b) or (r,g,b,a) tuples.
    """
    nw = max(1, width  // 2)
    nh = max(1, height // 2)
    channels = len(pixels[0])
    out = []
    for y in range(nh):
        for x in range(nw):
            sx, sy = x * 2, y * 2
            p00 = pixels[sy * width + sx]
            p10 = pixels[sy * width + min(sx + 1, width - 1)]
            p01 = pixels[min(sy + 1, height - 1) * width + sx]
            p11 = pixels[min(sy + 1, height - 1) * width + min(sx + 1, width - 1)]
            avg = tuple(
                (p00[c] + p10[c] + p01[c] + p11[c] + 2) // 4
                for c in range(channels)
            )
            out.append(avg)
    return out, nw, nh


def _encode_mip_dxt1(pixels, width: int, height: int) -> bytes:
    """Encode a full mip level as DXT1."""
    # Pad to multiple of 4
    pw = max(4, (width  + 3) & ~3)
    ph = max(4, (height + 3) & ~3)
    # Expand pixel list to padded dimensions
    if pw != width or ph != height:
        padded = []
        for y in range(ph):
            for x in range(pw):
                sx = min(x, width - 1)
                sy = min(y, height - 1)
                padded.append(pixels[sy * width + sx])
        pixels = padded
        effective_width = pw
        effective_height = ph
    else:
        effective_width = width
        effective_height = height

    out = bytearray()
    for by in range(0, effective_height, 4):
        for bx in range(0, effective_width, 4):
            block = []
            for ty in range(4):
                for tx in range(4):
                    x = bx + tx
                    y = by + ty
                    block.append(pixels[y * effective_width + x])
            out += _encode_dxt1_block(block)
    return bytes(out)


def _encode_mip_dxt5(pixels, width: int, height: int) -> bytes:
    """Encode a full mip level as DXT5."""
    pw = max(4, (width  + 3) & ~3)
    ph = max(4, (height + 3) & ~3)
    if pw != width or ph != height:
        padded = []
        for y in range(ph):
            for x in range(pw):
                sx = min(x, width - 1)
                sy = min(y, height - 1)
                padded.append(pixels[sy * width + sx])
        pixels = padded
        effective_width = pw
        effective_height = ph
    else:
        effective_width = width
        effective_height = height

    out = bytearray()
    for by in range(0, effective_height, 4):
        for bx in range(0, effective_width, 4):
            block = []
            for ty in range(4):
                for tx in range(4):
                    x = bx + tx
                    y = by + ty
                    block.append(pixels[y * effective_width + x])
            out += _encode_dxt5_block(block)
    return bytes(out)


# ---------------------------------------------------------------------------
# DDS header writer
# ---------------------------------------------------------------------------

def _write_dds_header(width: int, height: int, mip_count: int,
                      fourcc: bytes, block_size: int) -> bytes:
    """
    Write a 128-byte DDS header (legacy FourCC path).
    block_size: bytes per 4×4 block (8 for DXT1, 16 for DXT5).
    """
    # Linear size = byte size of top-level mip
    bw = max(1, (width  + 3) // 4)
    bh = max(1, (height + 3) // 4)
    linear_size = bw * bh * block_size

    flags = (DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH |
             DDSD_PIXELFORMAT | DDSD_MIPMAPCOUNT | DDSD_LINEARSIZE)
    caps = DDSCAPS_COMPLEX | DDSCAPS_MIPMAP | DDSCAPS_TEXTURE

    # DDPIXELFORMAT (32 bytes): pfSize + pfFlags + FourCC + 5 unused DWORD fields
    ddspf = struct.pack("<II", 32, DDPF_FOURCC) + fourcc + b"\x00" * 20
    # Caps block (16 bytes)
    caps_block = struct.pack("<IIII", caps, 0, 0, 0)
    # reserved2
    reserved2 = struct.pack("<I", 0)

    # Assemble correctly: magic(4) + dwSize(4) + dwFlags(4) + dwHeight(4) +
    #   dwWidth(4) + dwPitchOrLinearSize(4) + dwDepth(4) + dwMipMapCount(4) +
    #   reserved1[11×4=44] + DDPIXELFORMAT[32] + caps[16] + reserved2[4]
    # Total = 4+4+4+4+4+4+4+4+44+32+16+4 = 128 bytes
    h = struct.pack(
        "<4s" + "I" * 7,
        DDS_MAGIC,
        124,          # dwSize
        flags,
        height,
        width,
        linear_size,
        0,            # dwDepth
        mip_count,
    )
    h += b"\x00" * 44      # reserved1[11]
    h += ddspf             # 32 bytes
    h += caps_block        # 16 bytes
    h += reserved2         # 4 bytes
    assert len(h) == 128, f"Header length {len(h)}, expected 128"
    return h


# ---------------------------------------------------------------------------
# Pixel generators — diffuse textures (RGB)
# ---------------------------------------------------------------------------

def _clamp(v: int, lo: int = 0, hi: int = 255) -> int:
    return max(lo, min(hi, v))


def _make_grass_pixel(x: int, y: int, w: int, h: int):
    """
    Olive-green grass with stochastic blade impressions and soil show-through.
    Primary #7A8C3E.  Soil show-through #7A6448.  Highlight #B4B068.
    """
    fx = x / w
    fy = y / h

    # Low-frequency luminance variation (±12 units)
    lf = _fbm(fx * 4, fy * 4, seed=11, octaves=3)
    lf_bias = int((lf - 0.5) * 24)

    # Medium-frequency blade cluster density
    density = _fbm(fx * 16, fy * 16, seed=22, octaves=2)

    # High-frequency blade impression (hash-based)
    hf = _fhash(x, y, seed=33)
    blade_seed = _hash(x // 8, y // 8, seed=44)
    blade_presence = (hf * 7 + (blade_seed & 7)) / 14.0  # 0..1

    # Blend between soil show-through and blade color
    soil_r, soil_g, soil_b = 122, 100, 72   # #7A6448
    blade_r, blade_g, blade_b = 122, 140, 62  # #7A8C3E
    hi_r, hi_g, hi_b = 180, 176, 104          # #B4B068

    # Density gate: low density -> more soil
    if density < 0.35:
        t = blade_presence * density / 0.35
    else:
        t = blade_presence * 0.4 + density * 0.6

    t = min(1.0, t)

    r = int(soil_r * (1 - t) + blade_r * t)
    g = int(soil_g * (1 - t) + blade_g * t)
    b = int(soil_b * (1 - t) + blade_b * t)

    # Highlights on some blades
    if hf > 0.80 and density > 0.5:
        ht = (hf - 0.80) / 0.20
        r = int(r * (1 - ht) + hi_r * ht)
        g = int(g * (1 - ht) + hi_g * ht)
        b = int(b * (1 - ht) + hi_b * ht)

    # Apply low-frequency luminance bias
    r = _clamp(r + lf_bias)
    g = _clamp(g + lf_bias)
    b = _clamp(b + lf_bias)

    # Enforce: no pure green — G channel max 195
    g = min(g, 195)

    return (r, g, b)


def _make_asphalt_pixel(x: int, y: int, w: int, h: int):
    """
    Dark grey aged asphalt with aggregate specks and hairline cracks.
    Primary #3A3C3C (slight cool bias per spec).
    """
    fx = x / w
    fy = y / h

    # Low-frequency weathering variation (±8 units)
    lf = _fbm(fx * 8, fy * 8, seed=55, octaves=2)
    lf_bias = int((lf - 0.5) * 16)

    # Crack network: Voronoi-inspired using hashed grid
    # Cell size ~96px at 2048
    cell_size = 96
    cx = x // cell_size
    cy = y // cell_size
    best_dist = cell_size * 2.0
    for dcx in (-1, 0, 1):
        for dcy in (-1, 0, 1):
            px_seed = _hash(cx + dcx, cy + dcy, seed=66)
            px_offset_x = (px_seed & 0xFF) / 255.0 * cell_size
            py_offset_y = ((px_seed >> 8) & 0xFF) / 255.0 * cell_size
            px_abs = (cx + dcx) * cell_size + px_offset_x
            py_abs = (cy + dcy) * cell_size + py_offset_y
            d = math.sqrt((x - px_abs) ** 2 + (y - py_abs) ** 2)
            if d < best_dist:
                best_dist = d

    # Close to Voronoi edge -> crack
    crack_threshold = 2.5
    is_crack = best_dist < crack_threshold

    # Aggregate specks: stochastic point process
    speck_h = _fhash(x // 5, y // 5, seed=77)
    is_speck = (speck_h < 0.12) and not is_crack

    if is_crack:
        r = g = b = 28   # near-black crack
    elif is_speck:
        r = g = b = 106  # light aggregate
        # Cool bias: reduce R slightly for aggregate highlights too
        r = max(r - 4, 0)
    else:
        # Primary cool-grey #3A3C3C
        r = _clamp(58 + lf_bias - 4)   # slight cool bias: r < g=b
        g = _clamp(60 + lf_bias)
        b = _clamp(60 + lf_bias)

    return (r, g, b)


def _make_soil_pixel(x: int, y: int, w: int, h: int):
    """
    Rust-brown bare soil with granule field and desiccation cracks.
    Primary #7A5C3C.  Dry highlight #A07850.  Wet shadow #4E3820.
    """
    fx = x / w
    fy = y / h

    # Moisture variation (±20 units)
    moisture = _fbm(fx * 6, fy * 6, seed=88, octaves=2)
    lf_bias = int((moisture - 0.5) * 40)

    # Desiccation cracks: larger cells than asphalt
    cell_size = 140
    cx = x // cell_size
    cy = y // cell_size
    best_dist = cell_size * 2.0
    for dcx in (-1, 0, 1):
        for dcy in (-1, 0, 1):
            px_seed = _hash(cx + dcx, cy + dcy, seed=99)
            px_off_x = (px_seed & 0xFF) / 255.0 * cell_size
            py_off_y = ((px_seed >> 8) & 0xFF) / 255.0 * cell_size
            px_abs = (cx + dcx) * cell_size + px_off_x
            py_abs = (cy + dcy) * cell_size + py_off_y
            d = math.sqrt((x - px_abs) ** 2 + (y - py_abs) ** 2)
            if d < best_dist:
                best_dist = d

    is_crack = best_dist < 3.0   # 2-4 px wide crack per spec (approx)

    # Granule specks — three size classes
    g_h = _fhash(x, y, seed=101)
    is_coarse = g_h < 0.05
    is_medium = 0.05 <= g_h < 0.15
    is_fine   = 0.15 <= g_h < 0.35

    if is_crack:
        r, g, b = 78, 56, 32    # wet shadow #4E3820
    elif is_coarse:
        r, g, b = 138, 112, 96  # aggregate grit #8A7060
    elif is_medium:
        r, g, b = 106, 72, 48   # clay show-through #6A4830
    elif is_fine:
        r, g, b = 160, 120, 80  # dry crust #A07850
    else:
        r, g, b = 122, 92, 60   # primary #7A5C3C

    r = _clamp(r + lf_bias)
    g = _clamp(g + lf_bias // 2)
    b = _clamp(b)

    # Enforce G < R throughout (no green tones in soil)
    if g >= r:
        g = max(0, r - 2)

    return (r, g, b)


def _make_concrete_pixel(x: int, y: int, w: int, h: int):
    """
    Warm grey concrete with 512px expansion joint grid.
    Primary #C0B8A8 (warm: R>=G>=B+8).
    """
    fx = x / w
    fy = y / h

    # Joint grid period: 512 px
    joint_period = 512
    # Position within slab
    sx = x % joint_period
    sy = y % joint_period
    # Joint lines: 2-3 px wide, with 1 px bevel
    joint_width = 2
    bevel_width = 1

    dist_to_joint_x = min(sx, joint_period - 1 - sx)
    dist_to_joint_y = min(sy, joint_period - 1 - sy)
    dist_to_joint = min(dist_to_joint_x, dist_to_joint_y)

    is_joint  = dist_to_joint <  joint_width
    is_bevel  = dist_to_joint < (joint_width + bevel_width)

    # Slab age variation per slab cell
    slab_x = x // joint_period
    slab_y = y // joint_period
    slab_seed = _fhash(slab_x, slab_y, seed=111)
    slab_bias = int((slab_seed - 0.5) * 16)

    # Low-frequency weathering
    lf = _fbm(fx * 10, fy * 10, seed=122, octaves=2)
    lf_bias = int((lf - 0.5) * 20)

    # Aggregate show-through (low contrast)
    agg_h = _fhash(x // 3, y // 3, seed=133)
    agg_bump = 8 if agg_h < 0.10 else 0

    if is_joint:
        r, g, b = 122, 120, 112   # shadow/expansion joint #7A7870
    elif is_bevel:
        r, g, b = 180, 172, 156   # bevel highlight (midway to primary)
        r = _clamp(r + slab_bias // 2)
        g = _clamp(g + slab_bias // 2)
        b = _clamp(b + slab_bias // 2)
    else:
        # Primary warm grey #C0B8A8
        r = _clamp(192 + slab_bias + lf_bias + agg_bump)
        g = _clamp(184 + slab_bias + lf_bias)
        b = _clamp(168 + slab_bias + lf_bias)

    # Enforce warm bias R >= G > B (by ~8-16 units)
    if not is_joint:
        if r < g:
            r = g
        if g <= b:
            g = b + 8

    return (r, g, b)


# ---------------------------------------------------------------------------
# Pixel generators — normal maps (RGBA with DXT5nm swizzle)
# ---------------------------------------------------------------------------
# DXT5nm layout: alpha = X (nx), green = Y (ny), blue = 127, red = 0
# Flat reference: alpha=128, green=128 (nx=0, ny=0, nz=1)
# ---------------------------------------------------------------------------

def _flat_normal_pixel():
    """Pure flat-surface normal (no surface detail)."""
    return (0, 128, 127, 128)  # R=0, G=ny=128, B=127, A=nx=128


def _encode_nx_ny(nx: float, ny: float):
    """
    Encode tangent-space normal vector components nx, ny in [-1,1]
    to DXT5nm layout (R=0, G=ny_8bit, B=127, A=nx_8bit).
    """
    # Map [-1,1] -> [0,255]
    a = _clamp(int((nx + 1.0) * 127.5))  # alpha = X
    g = _clamp(int((ny + 1.0) * 127.5))  # green = Y
    return (0, g, 127, a)


def _make_grass_normal_pixel(x: int, y: int, w: int, h: int):
    """
    Moderate blade normals (|nx|,|ny| 0.15-0.40) with low-frequency undulation.
    """
    fx = x / w
    fy = y / h

    # Low-frequency base undulation (period ~512-1024px at 2048)
    lf_nx = (_fbm(fx * 2, fy * 2, seed=201, octaves=2) - 0.5) * 0.20
    lf_ny = (_fbm(fx * 2, fy * 2, seed=202, octaves=2) - 0.5) * 0.20

    # Blade perturbation: moderate intensity
    hf_x = (_fhash(x // 6, y // 6, seed=203) - 0.5) * 0.80
    hf_y = (_fhash(x // 6, y // 6, seed=204) - 0.5) * 0.80

    # Presence gate: only add blade normals where blades are dense
    density = _fbm(fx * 16, fy * 16, seed=22, octaves=2)
    blade_t = min(1.0, density * 1.4)

    nx = lf_nx + hf_x * blade_t * 0.40
    ny = lf_ny + hf_y * blade_t * 0.40

    # Clamp to spec range [-0.40, +0.40]
    nx = max(-0.40, min(0.40, nx))
    ny = max(-0.40, min(0.40, ny))

    return _encode_nx_ny(nx, ny)


def _make_asphalt_normal_pixel(x: int, y: int, w: int, h: int):
    """
    Subtle aggregate dome normals (|nx|,|ny| max 0.25).
    """
    fx = x / w
    fy = y / h

    # Aggregate pebble normals
    pebble_x = x // 4
    pebble_y = y // 4
    ph = _fhash(pebble_x, pebble_y, seed=301)
    # Dome normal: outward deflection
    local_x = (x % 4) - 1.5
    local_y = (y % 4) - 1.5
    radius = math.sqrt(local_x ** 2 + local_y ** 2) + 0.001

    pebble_strength = 0.20 if ph < 0.15 else 0.0
    nx_pebble = (local_x / radius) * pebble_strength
    ny_pebble = (local_y / radius) * pebble_strength

    # Crack groove normals: inward deflection at crack edges
    cell_size = 96
    cx = x // cell_size
    cy = y // cell_size
    best_dist = float(cell_size * 2)
    for dcx in (-1, 0, 1):
        for dcy in (-1, 0, 1):
            px_seed = _hash(cx + dcx, cy + dcy, seed=66)
            px_abs = (cx + dcx) * cell_size + (px_seed & 0xFF) / 255.0 * cell_size
            py_abs = (cy + dcy) * cell_size + ((px_seed >> 8) & 0xFF) / 255.0 * cell_size
            d = math.sqrt((x - px_abs) ** 2 + (y - py_abs) ** 2)
            if d < best_dist:
                best_dist = d

    # Near crack edge (2-4 px): inward bevel
    crack_groove = 0.0
    if 1.0 < best_dist < 5.0:
        crack_groove = (1.0 - (best_dist - 1.0) / 4.0) * 0.15

    nx = nx_pebble
    ny = ny_pebble - crack_groove  # inward (upward in Y)

    nx = max(-0.25, min(0.25, nx))
    ny = max(-0.25, min(0.25, ny))

    return _encode_nx_ny(nx, ny)


def _make_soil_normal_pixel(x: int, y: int, w: int, h: int):
    """
    Strong granule normals (|nx|,|ny| up to 0.50) with desiccation groove normals.
    """
    fx = x / w
    fy = y / h

    # Low-frequency undulation (period 256-512px)
    lf_nx = (_fbm(fx * 4, fy * 4, seed=401, octaves=2) - 0.5) * 0.16
    lf_ny = (_fbm(fx * 4, fy * 4, seed=402, octaves=2) - 0.5) * 0.16

    # Granule dome normals — three size classes
    g_h = _fhash(x, y, seed=101)  # same seed as diffuse for registration
    if g_h < 0.05:      # coarse
        local_x = (x % 7) - 3.0
        local_y = (y % 7) - 3.0
        r = math.sqrt(local_x ** 2 + local_y ** 2) + 0.001
        nx_g = (local_x / r) * 0.45
        ny_g = (local_y / r) * 0.45
    elif g_h < 0.15:    # medium
        local_x = (x % 4) - 1.5
        local_y = (y % 4) - 1.5
        r = math.sqrt(local_x ** 2 + local_y ** 2) + 0.001
        nx_g = (local_x / r) * 0.30
        ny_g = (local_y / r) * 0.30
    elif g_h < 0.35:    # fine
        nx_g = (_fhash(x, y, seed=403) - 0.5) * 0.25
        ny_g = (_fhash(x, y, seed=404) - 0.5) * 0.25
    else:
        nx_g = ny_g = 0.0

    # Desiccation crack groove normals
    cell_size = 140
    cx = x // cell_size
    cy = y // cell_size
    best_dist = float(cell_size * 2)
    for dcx in (-1, 0, 1):
        for dcy in (-1, 0, 1):
            px_seed = _hash(cx + dcx, cy + dcy, seed=99)
            px_abs = (cx + dcx) * cell_size + (px_seed & 0xFF) / 255.0 * cell_size
            py_abs = (cy + dcy) * cell_size + ((px_seed >> 8) & 0xFF) / 255.0 * cell_size
            d = math.sqrt((x - px_abs) ** 2 + (y - py_abs) ** 2)
            if d < best_dist:
                best_dist = d

    crack_nx = crack_ny = 0.0
    if 1.0 < best_dist < 7.0:
        bevel = (1.0 - (best_dist - 1.0) / 6.0) * 0.35
        crack_nx = bevel * 0.5  # inward toward crack center
        crack_ny = bevel * 0.5

    nx = lf_nx + nx_g + crack_nx
    ny = lf_ny + ny_g + crack_ny

    nx = max(-0.50, min(0.50, nx))
    ny = max(-0.50, min(0.50, ny))

    return _encode_nx_ny(nx, ny)


def _make_concrete_normal_pixel(x: int, y: int, w: int, h: int):
    """
    Subtle normals (max ±0.18).  Expansion joint bevel normals dominate.
    """
    joint_period = 512
    sx = x % joint_period
    sy = y % joint_period

    joint_width = 2
    bevel_width = 4

    dist_x = min(sx, joint_period - 1 - sx)
    dist_y = min(sy, joint_period - 1 - sy)

    nx = 0.0
    ny = 0.0

    # Bevel normal along joint edges
    if dist_x < (joint_width + bevel_width):
        # Which side of joint
        sign_x = 1.0 if sx < joint_period // 2 else -1.0
        t = max(0.0, 1.0 - dist_x / (joint_width + bevel_width))
        nx += sign_x * t * 0.15

    if dist_y < (joint_width + bevel_width):
        sign_y = 1.0 if sy < joint_period // 2 else -1.0
        t = max(0.0, 1.0 - dist_y / (joint_width + bevel_width))
        ny += sign_y * t * 0.15

    # Aggregate perturbation (very subtle)
    agg_h = _fhash(x // 3, y // 3, seed=133)
    if agg_h < 0.10:
        local_x = (x % 3) - 1.0
        local_y = (y % 3) - 1.0
        r = math.sqrt(local_x ** 2 + local_y ** 2) + 0.001
        nx += (local_x / r) * 0.08
        ny += (local_y / r) * 0.08

    nx = max(-0.18, min(0.18, nx))
    ny = max(-0.18, min(0.18, ny))

    return _encode_nx_ny(nx, ny)


# ---------------------------------------------------------------------------
# Top-level generation
# ---------------------------------------------------------------------------

DIFFUSE_GENERATORS = {
    "terrain_grass_d.dds":    _make_grass_pixel,
    "terrain_asphalt_d.dds":  _make_asphalt_pixel,
    "terrain_soil_d.dds":     _make_soil_pixel,
    "terrain_concrete_d.dds": _make_concrete_pixel,
}

NORMAL_GENERATORS = {
    "terrain_grass_n.dds":    _make_grass_normal_pixel,
    "terrain_asphalt_n.dds":  _make_asphalt_normal_pixel,
    "terrain_soil_n.dds":     _make_soil_normal_pixel,
    "terrain_concrete_n.dds": _make_concrete_normal_pixel,
}


def _generate_diffuse_dds(name: str, pixel_fn, out_dir: str,
                          base_size: int = 2048) -> str:
    """
    Generate a DXT1/BC1 DDS file with MIP_LEVELS mip levels.
    Returns the output path.
    """
    out_path = os.path.join(out_dir, name)
    print(f"  Generating {name} ({base_size}x{base_size} DXT1, {MIP_LEVELS} mips)...")

    # Build top-level pixel array
    w, h = base_size, base_size
    print(f"    Building mip 0 ({w}x{h}) pixels...", flush=True)
    pixels = [pixel_fn(x, y, w, h) for y in range(h) for x in range(w)]

    header = _write_dds_header(w, h, MIP_LEVELS, FOURCC_DXT1, 8)

    with open(out_path, "wb") as f:
        f.write(header)
        for mip in range(MIP_LEVELS):
            mw = max(1, w >> mip)
            mh = max(1, h >> mip)
            if mip == 0:
                mip_pixels = pixels
            else:
                # Downsample from previous mip level
                mip_pixels, _, _ = _downsample_2x(prev_pixels,
                                                   max(1, w >> (mip - 1)),
                                                   max(1, h >> (mip - 1)))
            print(f"    Encoding mip {mip} ({mw}x{mh})...", flush=True)
            f.write(_encode_mip_dxt1(mip_pixels, mw, mh))
            prev_pixels = mip_pixels

    size_mb = os.path.getsize(out_path) / (1024 * 1024)
    print(f"    Written: {out_path} ({size_mb:.2f} MB)")
    return out_path


def _generate_normal_dds(name: str, pixel_fn, out_dir: str,
                         base_size: int = 2048) -> str:
    """
    Generate a DXT5/BC3 DDS file (DXT5nm swizzle) with MIP_LEVELS mip levels.
    Returns the output path.
    """
    out_path = os.path.join(out_dir, name)
    print(f"  Generating {name} ({base_size}x{base_size} DXT5nm, {MIP_LEVELS} mips)...")

    w, h = base_size, base_size
    print(f"    Building mip 0 ({w}x{h}) pixels...", flush=True)
    pixels = [pixel_fn(x, y, w, h) for y in range(h) for x in range(w)]

    header = _write_dds_header(w, h, MIP_LEVELS, FOURCC_DXT5, 16)

    with open(out_path, "wb") as f:
        f.write(header)
        for mip in range(MIP_LEVELS):
            mw = max(1, w >> mip)
            mh = max(1, h >> mip)
            if mip == 0:
                mip_pixels = pixels
            else:
                mip_pixels, _, _ = _downsample_2x(prev_pixels,
                                                   max(1, w >> (mip - 1)),
                                                   max(1, h >> (mip - 1)))
            print(f"    Encoding mip {mip} ({mw}x{mh})...", flush=True)
            f.write(_encode_mip_dxt5(mip_pixels, mw, mh))
            prev_pixels = mip_pixels

    size_mb = os.path.getsize(out_path) / (1024 * 1024)
    print(f"    Written: {out_path} ({size_mb:.2f} MB)")
    return out_path


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    workspace_root = os.path.dirname(script_dir)
    out_dir = os.path.join(workspace_root, "assets", "terrain")
    os.makedirs(out_dir, exist_ok=True)

    print("AI Town — Phase 5 Terrain Texture Generator")
    print(f"Output directory: {out_dir}")
    print()

    print("--- Diffuse textures (DXT1/BC1 sRGB) ---")
    for name, fn in DIFFUSE_GENERATORS.items():
        _generate_diffuse_dds(name, fn, out_dir)

    print()
    print("--- Normal maps (DXT5/BC3 DXT5nm linear) ---")
    for name, fn in NORMAL_GENERATORS.items():
        _generate_normal_dds(name, fn, out_dir)

    print()
    print("Generation complete. Verifying file sizes...")
    print()

    expected = {
        "terrain_grass_d.dds":    (2_500_000, 3_100_000),
        "terrain_asphalt_d.dds":  (2_500_000, 3_100_000),
        "terrain_soil_d.dds":     (2_500_000, 3_100_000),
        "terrain_concrete_d.dds": (2_500_000, 3_100_000),
        "terrain_grass_n.dds":    (5_000_000, 6_200_000),
        "terrain_asphalt_n.dds":  (5_000_000, 6_200_000),
        "terrain_soil_n.dds":     (5_000_000, 6_200_000),
        "terrain_concrete_n.dds": (5_000_000, 6_200_000),
    }

    all_ok = True
    for fname, (lo, hi) in expected.items():
        path = os.path.join(out_dir, fname)
        if not os.path.exists(path):
            print(f"  MISSING: {fname}")
            all_ok = False
            continue
        sz = os.path.getsize(path)
        status = "OK" if lo <= sz <= hi else "SIZE_WARNING"
        print(f"  {status}: {fname}  {sz / (1024*1024):.2f} MB "
              f"(expected {lo/(1024*1024):.1f}–{hi/(1024*1024):.1f} MB)")
        if status != "OK":
            all_ok = False

    print()
    if all_ok:
        print("All 8 terrain DDS files generated successfully.")
    else:
        print("WARNING: one or more files are missing or outside expected size range.")
        print("Run validate_assets.py for detailed format checks.")


if __name__ == "__main__":
    main()
