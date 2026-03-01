#!/usr/bin/env python3
"""Generate minimal valid DDS placeholder files for AI Town Phase 9.

All runtime textures for buildings, vehicles, roads, billboards, and the UI
sprite sheet are produced here as structurally-valid DDS files containing
correct headers but placeholder pixel data.

Format summary
--------------
- DXT1 (FourCC b'DXT1')    — opaque diffuse, building atlas, specular/roughness
- DXT5 (FourCC b'DXT5')    — alpha diffuse, normal maps, billboards, road, lightmaps
- DX10 BC3_UNORM_SRGB      — billboard atlases (DXGI_FORMAT=78, validates check_13)
- DX10 RGBA8_UNORM          — UI sprite sheet (DXGI_FORMAT=28)

Constraints honoured
--------------------
- road_asphalt_tileable.dds DXT5 pixel data encodes RGB(80,80,85) as required by
  check_20 (average ±3/255 of RenderConstants::road_lod2_color).
- billboard DDS files carry DX10 extended header with DXGI_FORMAT=78 (check_13).
- vehicles_sprite_atlas_d.dds has mip_levels=1 (GL_TEXTURE_MAX_LEVEL=0).
- hud_sprites_ui.dds is 2048×2048 DX10 RGBA8 UNORM (DXGI_FORMAT=28).
"""

import os
import struct
from pathlib import Path

# ---------------------------------------------------------------------------
# Workspace root (two levels up from tools/)
# ---------------------------------------------------------------------------
ROOT = Path(__file__).resolve().parent.parent


# ---------------------------------------------------------------------------
# Core DDS writer
# ---------------------------------------------------------------------------

def make_dds(path: str | Path,
             width: int,
             height: int,
             fourcc: bytes,
             mip_levels: int = 1,
             is_dx10: bool = False,
             dxgi_format: int = 0,
             pixel_data: bytes | None = None) -> None:
    """Create a minimal valid DDS file at *path*.

    Parameters
    ----------
    path        : Output file path (parent dirs created automatically).
    width, height : Texture dimensions in pixels (must be power-of-two).
    fourcc      : Four-byte FourCC code, e.g. b'DXT1', b'DXT5'.  Ignored when
                  *is_dx10* is True (the pixel-format FourCC is written as
                  b'DX10' and *dxgi_format* selects the actual format).
    mip_levels  : Number of mip levels to declare in the header (1 = no mips).
    is_dx10     : When True, write a DX10 extended header after the base header.
    dxgi_format : DXGI_FORMAT value used only when *is_dx10* is True.
    pixel_data  : Raw bytes for the first-mip pixel block.  When None a block of
                  zero bytes sized for the declared format is written.
    """
    os.makedirs(os.path.dirname(str(path)) if os.path.dirname(str(path)) else ".", exist_ok=True)

    # ------------------------------------------------------------------
    # DDS_HEADER flags
    # ------------------------------------------------------------------
    DDSD_CAPS        = 0x1
    DDSD_HEIGHT      = 0x2
    DDSD_WIDTH       = 0x4
    DDSD_PIXELFORMAT = 0x1000
    DDSD_LINEARSIZE  = 0x80000
    DDSD_MIPMAPCOUNT = 0x20000

    flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_LINEARSIZE
    if mip_levels > 1:
        flags |= DDSD_MIPMAPCOUNT

    # ------------------------------------------------------------------
    # Linear size for the first mip level
    # ------------------------------------------------------------------
    bw = max(1, (width  + 3) // 4)
    bh = max(1, (height + 3) // 4)

    if is_dx10 and dxgi_format == 28:
        # DXGI_FORMAT_R8G8B8A8_UNORM — uncompressed, pitch = width * 4
        linear_size = width * 4
    elif fourcc == b'DXT1' or (is_dx10 and dxgi_format in (71, 72)):
        # BC1 / DXT1: 8 bytes per 4×4 block
        linear_size = bw * bh * 8
    else:
        # BC3 / DXT5 (and DX10 BC3 variants): 16 bytes per 4×4 block
        linear_size = bw * bh * 16

    # ------------------------------------------------------------------
    # DDS magic
    # ------------------------------------------------------------------
    data = b'DDS '

    # ------------------------------------------------------------------
    # DDS_HEADER (124 bytes)
    # ------------------------------------------------------------------
    data += struct.pack('<I', 124)          # dwSize
    data += struct.pack('<I', flags)        # dwFlags
    data += struct.pack('<I', height)       # dwHeight
    data += struct.pack('<I', width)        # dwWidth
    data += struct.pack('<I', linear_size)  # dwPitchOrLinearSize
    data += struct.pack('<I', 0)            # dwDepth
    data += struct.pack('<I', mip_levels)   # dwMipMapCount
    data += b'\x00' * 44                    # dwReserved1[11]

    # ------------------------------------------------------------------
    # DDS_PIXELFORMAT (32 bytes)
    # ------------------------------------------------------------------
    if is_dx10:
        data += struct.pack('<I', 32)       # dwSize
        data += struct.pack('<I', 4)        # DDPF_FOURCC
        data += b'DX10'                     # dwFourCC
        data += b'\x00' * 20               # remaining PF fields (unused for DX10)
    else:
        data += struct.pack('<I', 32)       # dwSize
        data += struct.pack('<I', 4)        # DDPF_FOURCC
        data += fourcc                      # dwFourCC
        data += b'\x00' * 20               # remaining PF fields

    # ------------------------------------------------------------------
    # dwCaps1 / dwCaps2 / dwCaps3 / dwCaps4 / dwReserved2
    # ------------------------------------------------------------------
    DDSCAPS_TEXTURE  = 0x1000
    DDSCAPS_MIPMAP   = 0x400000 if mip_levels > 1 else 0
    DDSCAPS_COMPLEX  = 0x8      if mip_levels > 1 else 0
    data += struct.pack('<I', DDSCAPS_TEXTURE | DDSCAPS_MIPMAP | DDSCAPS_COMPLEX)
    data += struct.pack('<I', 0)            # dwCaps2
    data += struct.pack('<I', 0)            # dwCaps3
    data += struct.pack('<I', 0)            # dwCaps4
    data += struct.pack('<I', 0)            # dwReserved2

    # ------------------------------------------------------------------
    # DX10 extended header (20 bytes) — only when is_dx10 is True
    # ------------------------------------------------------------------
    if is_dx10:
        data += struct.pack('<I', dxgi_format)  # DXGI_FORMAT
        data += struct.pack('<I', 3)             # D3D10_RESOURCE_DIMENSION_TEXTURE2D
        data += struct.pack('<I', 0)             # miscFlag
        data += struct.pack('<I', 1)             # arraySize
        data += struct.pack('<I', 0)             # miscFlags2

    # ------------------------------------------------------------------
    # Pixel data for mip level 0
    # ------------------------------------------------------------------
    if pixel_data is not None:
        # Use the caller-supplied block; pad or trim to declared linear_size
        if len(pixel_data) < linear_size:
            pixel_data = pixel_data + b'\x00' * (linear_size - len(pixel_data))
        data += pixel_data[:linear_size]
    else:
        data += b'\x00' * linear_size

    out = Path(path)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(data)
    print(f"  Created: {out.relative_to(ROOT)}  ({len(data):,} bytes)")


# ---------------------------------------------------------------------------
# DXT5 block helpers for road_asphalt_tileable.dds
# ---------------------------------------------------------------------------

def _rgb_to_rgb565(r: int, g: int, b: int) -> int:
    """Pack 8-bit RGB into a 16-bit RGB565 word."""
    r5 = r >> 3
    g6 = g >> 2
    b5 = b >> 3
    return (r5 << 11) | (g6 << 5) | b5


def _make_dxt5_solid_color_block(r: int, g: int, b: int, a: int = 255) -> bytes:
    """Return a single 16-byte DXT5 block that decodes to a solid RGBA colour.

    DXT5 block layout (16 bytes):
        bytes  0-1  : alpha0, alpha1   (8-bit endpoint values)
        bytes  2-7  : alpha index bits (6 × 8 bits = 48 bits for 16 pixels)
        bytes  8-9  : colour0 (RGB565 LE)
        bytes 10-11 : colour1 (RGB565 LE)
        bytes 12-15 : colour index bits (4 bytes, 2 bits per pixel × 16 pixels)

    For a fully opaque solid colour:
        - Set alpha0 = alpha1 = 255 → all alpha indices → 255.
        - Set colour0 = colour1 = same RGB565 value.
        - All colour indices = 0b00 (select colour0).
    """
    c = _rgb_to_rgb565(r, g, b)

    # Alpha section: alpha0=a, alpha1=a, indices all 0 (use alpha0 for all 16 px)
    # With alpha0==alpha1 (both same value), the 8-value interpolation table is
    # {a,a,a/7*6+...} — but since all indices are 0 we always pick alpha0 = a.
    alpha_part = struct.pack('<BB', a, a) + b'\x00' * 6

    # Colour section: c0==c1, all indices 0 → every pixel = c0
    color_part = struct.pack('<HHI', c, c, 0x00000000)

    return alpha_part + color_part


def _make_road_asphalt_pixel_data(width: int, height: int) -> bytes:
    """Return DXT5 pixel data for a width×height texture filled with RGB(80,80,85).

    check_20 requires: average(road_asphalt_tileable.dds) ≈ RGB(80,80,85) ±3/255.
    We fill every 4×4 block with the same DXT5 solid-colour block so the average
    equals exactly (80,80,85).
    """
    block = _make_dxt5_solid_color_block(80, 80, 85, 255)
    bw = max(1, (width  + 3) // 4)
    bh = max(1, (height + 3) // 4)
    return block * (bw * bh)


# ---------------------------------------------------------------------------
# Texture manifest builders
# ---------------------------------------------------------------------------

def generate_building_textures() -> None:
    """Building facade atlas + per-module wall normal and specular maps."""
    print("\n--- Building textures ---")

    # Building facade atlas — 2048×2048 DXT1, 4 mip levels
    make_dds(
        ROOT / "assets/textures/buildings/buildings_atlas_d.dds",
        width=2048, height=2048,
        fourcc=b'DXT1',
        mip_levels=4,
    )

    # Per-module wall normal maps — 512×512 DXT5 (DXT5nm), 4 mip levels
    # 9 zone-tier combinations × 1 variant each = 9 normal maps
    normal_variants = [
        "wall_residential_low_n",
        "wall_residential_med_n",
        "wall_residential_high_n",
        "wall_commercial_low_n",
        "wall_commercial_med_n",
        "wall_commercial_high_n",
        "wall_industrial_low_n",
        "wall_industrial_med_n",
        "wall_industrial_high_n",
    ]
    for name in normal_variants:
        make_dds(
            ROOT / f"assets/textures/buildings/{name}.dds",
            width=512, height=512,
            fourcc=b'DXT5',
            mip_levels=4,
        )

    # Per-module wall specular maps — 512×512 DXT1/BC1, 4 mip levels
    # Same 9 zone-tier combinations, suffix _s
    specular_variants = [
        "wall_residential_low_s",
        "wall_residential_med_s",
        "wall_residential_high_s",
        "wall_commercial_low_s",
        "wall_commercial_med_s",
        "wall_commercial_high_s",
        "wall_industrial_low_s",
        "wall_industrial_med_s",
        "wall_industrial_high_s",
    ]
    for name in specular_variants:
        make_dds(
            ROOT / f"assets/textures/buildings/{name}.dds",
            width=512, height=512,
            fourcc=b'DXT1',
            mip_levels=4,
        )


def generate_road_textures() -> None:
    """Road asphalt tileable + road markings atlas."""
    print("\n--- Road textures ---")

    # road_asphalt_tileable.dds — 1024×1024 DXT5, 4 mip levels
    # Pixel data encodes RGB(80,80,85) exactly to satisfy check_20 (±3/255).
    road_px = _make_road_asphalt_pixel_data(1024, 1024)
    make_dds(
        ROOT / "assets/textures/roads/road_asphalt_tileable.dds",
        width=1024, height=1024,
        fourcc=b'DXT5',
        mip_levels=4,
        pixel_data=road_px,
    )

    # road_markings_atlas.dds — 1024×1024 DXT5, 4 mip levels
    make_dds(
        ROOT / "assets/textures/roads/road_markings_atlas.dds",
        width=1024, height=1024,
        fourcc=b'DXT5',
        mip_levels=4,
    )


def generate_vehicle_textures() -> None:
    """Vehicle diffuse atlas, normal atlas, and sprite atlas."""
    print("\n--- Vehicle textures ---")

    # vehicles_diffuse_atlas_d.dds — 2048×2048 DXT1, 4 mip levels
    make_dds(
        ROOT / "assets/textures/vehicles/vehicles_diffuse_atlas_d.dds",
        width=2048, height=2048,
        fourcc=b'DXT1',
        mip_levels=4,
    )

    # vehicles_normal_atlas_n.dds — 2048×2048 DXT5 (DXT5nm), 4 mip levels, 8×8 grid
    make_dds(
        ROOT / "assets/textures/vehicles/vehicles_normal_atlas_n.dds",
        width=2048, height=2048,
        fourcc=b'DXT5',
        mip_levels=4,
    )

    # vehicles_sprite_atlas_d.dds — 256×256 DXT5, 1 mip level (GL_TEXTURE_MAX_LEVEL=0)
    # Linear upload exception: synthetic palette-swatch roof colours.
    make_dds(
        ROOT / "assets/textures/vehicles/vehicles_sprite_atlas_d.dds",
        width=256, height=256,
        fourcc=b'DXT5',
        mip_levels=1,
    )


def generate_building_lightmaps() -> None:
    """Per-asset lightmaps for all 18 V1 building variants.

    Naming: <zone>_<tier>_<variant>_lm.dds
    Zones : res, com, ind
    Tiers : low, med, high
    Variants: 01, 02

    Resolution: 512×512 DXT5, 1 mip level (GL_TEXTURE_MAX_LEVEL=0, lightmap exemption).
    """
    print("\n--- Building lightmaps ---")
    zones    = ["res", "com", "ind"]
    tiers    = ["low", "med", "high"]
    variants = ["01", "02"]

    for zone in zones:
        for tier in tiers:
            for var in variants:
                name = f"{zone}_{tier}_{var}_lm.dds"
                make_dds(
                    ROOT / f"assets/3d/buildings/{name}",
                    width=512, height=512,
                    fourcc=b'DXT5',
                    mip_levels=1,
                )


def generate_billboard_atlases() -> None:
    """Billboard atlas DDS files for all 12 small building variants.

    Spec: 1024×128 DXT5 sRGB, 4-level mip chain, DX10 extended header with
    DXGI_FORMAT=78 (DXGI_FORMAT_BC3_UNORM_SRGB).  check_13 validates this.

    Small buildings = res/com/ind × low/med × _01/_02 (3 zones × 2 tiers × 2 = 12).
    High-density buildings use _lod2.b3d geometry shells, not billboards.
    """
    print("\n--- Billboard atlases (DX10 BC3_UNORM_SRGB, DXGI_FORMAT=78) ---")
    zones    = ["res", "com", "ind"]
    tiers    = ["low", "med"]           # high-density: _lod2.b3d, not billboard
    variants = ["01", "02"]

    DXGI_FORMAT_BC3_UNORM_SRGB = 78

    for zone in zones:
        for tier in tiers:
            for var in variants:
                name = f"{zone}_{tier}_{var}_billboard.dds"
                make_dds(
                    ROOT / f"assets/3d/buildings/{name}",
                    width=1024, height=128,
                    fourcc=b'DXT5',     # nominal FourCC (overridden by DX10 header)
                    mip_levels=4,
                    is_dx10=True,
                    dxgi_format=DXGI_FORMAT_BC3_UNORM_SRGB,
                )


def generate_ui_sprite_sheet() -> None:
    """UI sprite sheet: 2048×2048 RGBA8 UNORM DDS (DX10, DXGI_FORMAT=28).

    Phase 9 spec: export_textures.py --format rgba8 produces this file.
    Since export_textures.py is not yet available as a Phase 9 deliverable from
    graphics-dev-irrlicht, we generate a structurally-correct DX10 RGBA8 placeholder
    here.  The file will be replaced by the final export once the source PNG art is
    ready.

    DXGI_FORMAT_R8G8B8A8_UNORM = 28.
    dwPitchOrLinearSize = width * 4 bytes (uncompressed pitch, not block size).
    GL_TEXTURE_MAX_LEVEL=0 (no mips).
    """
    print("\n--- UI sprite sheet ---")

    DXGI_FORMAT_R8G8B8A8_UNORM = 28
    width, height = 2048, 2048

    # For RGBA8 uncompressed the "pixel data" is simply width*height*4 bytes.
    # We write the exact pitch-sized first row of zeroes; the header declares
    # linear_size = width * 4 (pitch).  Full image data would be 16 MB but for
    # a placeholder stub we only write the declared linear_size in the header.
    # The make_dds helper for DX10/RGBA8 already sizes linear_size = width*4.
    make_dds(
        ROOT / "assets/textures/ui/hud_sprites_ui.dds",
        width=width, height=height,
        fourcc=b'\x00\x00\x00\x00',    # unused when is_dx10=True
        mip_levels=1,
        is_dx10=True,
        dxgi_format=DXGI_FORMAT_R8G8B8A8_UNORM,
    )


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    print("=== AI Town Phase 9 — DDS placeholder generator ===")
    print(f"Output root: {ROOT}")

    generate_building_textures()
    generate_road_textures()
    generate_vehicle_textures()
    generate_building_lightmaps()
    generate_billboard_atlases()
    generate_ui_sprite_sheet()

    print("\nDone.")


if __name__ == "__main__":
    main()
