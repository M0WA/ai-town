# Terrain Texture Asset Placeholders

Phase 5 placeholder stubs. Real DDS assets to be produced before Phase 9.

## Grassland Biome

- terrain_grass_d.dds — 2048×2048 DDS DXT1 sRGB, 4-level mip, anisotropy ≥8× (PLACEHOLDER)
- terrain_grass_n.dds — 2048×2048 DDS DXT5nm, pre-baked mips (PLACEHOLDER)
- terrain_asphalt_d.dds — 2048×2048 DDS DXT1 sRGB, 4-level mip (PLACEHOLDER)
- terrain_soil_d.dds — 2048×2048 DDS DXT1 sRGB, 4-level mip (PLACEHOLDER)
- terrain_concrete_d.dds — 2048×2048 DDS DXT1 sRGB, 4-level mip (PLACEHOLDER)

## Desert Biome

- terrain_sand_d.dds — 2048×2048 DDS DXT1 sRGB, 4-level mip (PLACEHOLDER)
- terrain_sand_n.dds — 2048×2048 DDS DXT5nm, pre-baked mips (PLACEHOLDER)

## Splat Map

- terrain_chunk_splat.png — 16×16 RGBA8 PNG, GL_TEXTURE_MAX_LEVEL=0 (PLACEHOLDER)
  Channels: R=grass/sand base, G=asphalt, B=soil, A=concrete

## Splat Channel Assignment (LOCKED — 2026-02-25 by graphics-artist-2d-texture)

R = base layer (grassland=grass, desert=sand) — biome-specific, content-only swap
G = asphalt road surface
B = soil / dirt
A = concrete / paved plaza
