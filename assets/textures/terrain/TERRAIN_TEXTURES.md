# Terrain Texture Assets

## Grassland Biome

Phase 5 procedural DDS placeholders generated 2026-02-26 by `tools/generate_terrain_textures.py`.
Artistically authored replacements are scheduled for Phase 9.

- terrain_grass_d.dds — 2048×2048 DDS DXT1/BC1, 4-level mip (2048→1024→512→256), sRGB upload path
- terrain_grass_n.dds — 2048×2048 DDS DXT5/BC3 DXT5nm, 4 pre-baked mip levels, linear upload path
- terrain_asphalt_d.dds — 2048×2048 DDS DXT1/BC1, 4-level mip, sRGB upload path
- terrain_asphalt_n.dds — 2048×2048 DDS DXT5/BC3 DXT5nm, 4 pre-baked mip levels, linear upload path
- terrain_soil_d.dds — 2048×2048 DDS DXT1/BC1, 4-level mip, sRGB upload path
- terrain_soil_n.dds — 2048×2048 DDS DXT5/BC3 DXT5nm, 4 pre-baked mip levels, linear upload path
- terrain_concrete_d.dds — 2048×2048 DDS DXT1/BC1, 4-level mip, sRGB upload path
- terrain_concrete_n.dds — 2048×2048 DDS DXT5/BC3 DXT5nm, 4 pre-baked mip levels, linear upload path

## Desert Biome

- terrain_sand_d.dds — 2048×2048 DDS DXT1 sRGB, 4-level mip (PLACEHOLDER — Phase 9)
- terrain_sand_n.dds — 2048×2048 DDS DXT5nm, pre-baked mips (PLACEHOLDER — Phase 9)

## Splat Map

- terrain_chunk_splat.png — 16×16 RGBA8 PNG, GL_TEXTURE_MAX_LEVEL=0 (PLACEHOLDER)
  Channels: R=grass/sand base, G=asphalt, B=soil, A=concrete

## Splat Channel Assignment (LOCKED — 2026-02-25 by graphics-artist-2d-texture)

R = base layer (grassland=grass, desert=sand) — biome-specific, content-only swap
G = asphalt road surface
B = soil / dirt
A = concrete / paved plaza
