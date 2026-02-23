#pragma once

// shader_constants.h — texture unit index table for all AI Town GLSL shaders.
// These constants are shared across terrain, building, vehicle, and billboard shaders.
// Must be present before Phase 3 begins; Phase 4 verifies correctness; Phase 5 wires into GLSL.
//
// Source of truth: architecture/asset-standards/2d-texture-standards.md (Texture Unit Assignments table).
// OpenGL 3.3 guarantees at least 16 texture image units per stage — kTexUnitBillboard must be <= 15.

// Unit 0: Diffuse/albedo (sRGB DXT1/DXT5 raw-GL path — not ITexture*)
constexpr int kTexUnitDiffuse       = 0;

// Unit 1: Normal map (DXT5nm, linear-pool ITexture*)
constexpr int kTexUnitNormal        = 1;

// Unit 2: Specular/roughness (BC1 or BC3 packed, linear-pool ITexture*)
constexpr int kTexUnitSpecular      = 2;

// Unit 3: Lightmap bake (DXT5/BC3, linear-pool ITexture*)
constexpr int kTexUnitLightmap      = 3;

// Unit 4: Terrain splat/blend map (RGBA8 uncompressed, splat-map pool — raw glTexImage2D, NOT linear-pool)
constexpr int kTexUnitSplatMap      = 4;

// Unit 5: Terrain detail layer 0 — Grass (DXT1 sRGB, raw-GL)
constexpr int kTexUnitTerrainLayer0 = 5;

// Unit 6: Terrain detail layer 1 — Asphalt (DXT1 sRGB, raw-GL)
constexpr int kTexUnitTerrainLayer1 = 6;

// Unit 7: Terrain detail layer 2 — Soil (DXT1 sRGB, raw-GL)
constexpr int kTexUnitTerrainLayer2 = 7;

// Unit 8: Terrain detail layer 3 — Concrete (DXT1 sRGB, raw-GL)
constexpr int kTexUnitTerrainLayer3 = 8;

// Unit 9: Billboard imposter atlas (DXT5 sRGB, raw-GL; 1024x128 strip, 8 directions x 128x128)
constexpr int kTexUnitBillboard     = 9;

// Compile-time range guard: OpenGL 3.3 guarantees at least 16 texture image units per stage.
// kTexUnitBillboard must not exceed 15 (units 0–15 are guaranteed).
static_assert(kTexUnitBillboard <= 15,
    "Texture unit index exceeds GL_MAX_TEXTURE_IMAGE_UNITS minimum (16 units guaranteed per stage in OpenGL 3.3)");
