#version 130

// terrain.frag — Phase 5 full implementation.
// Terrain splat-map fragment shader.
// See architecture/graphics-architecture/shader-loading.md for the 5-unit binding sequence
// required in OnSetConstants() and the GL_ACTIVE_TEXTURE save/restore requirement.
//
// Texture unit assignments (must match shader_constants.h):
//   Unit 4: u_splatMap  — linear RGBA8 blend weights (NOT sRGB; splat map pool)
//   Unit 5: u_layer0    — sRGB DXT1 base biome layer (R channel weight)
//   Unit 6: u_layer1    — sRGB DXT1 asphalt          (G channel weight)
//   Unit 7: u_layer2    — sRGB DXT1 soil              (B channel weight)
//   Unit 8: u_layer3    — sRGB DXT1 concrete          (A channel weight)
//
// Splat channel lock (confirmed, see architecture/asset-standards/building-atlas-layout.md):
//   R = base biome layer (grassland=grass, desert=sand)
//   G = asphalt
//   B = soil
//   A = concrete
//
// sRGB gamma fallback: when GL_EXT_texture_sRGB is absent, textures are uploaded as linear.
// The uniform int u_srgbLinear (1=apply manual gamma, 0=GPU handles sRGB) activates a
// pow(color.rgb, vec3(2.2)) correction per layer before blending.
// This avoids the need for two shader variants (one for sRGB HW path, one for linear fallback).
// See architecture/graphics-architecture/shader-loading.md — sRGB Gamma Fallback section.

// Splat map — unit 4 — linear RGBA8 blend weights.
// NOT sRGB: splat map values are blend weights [0.0, 1.0], not perceptual colour.
uniform sampler2D u_splatMap;   // unit 4 — linear RGBA8 blend weights

// sRGB terrain detail layers — units 5-8.
// Uploaded via raw GL path with GL_COMPRESSED_SRGB_S3TC_DXT1_EXT.
// Tiled at 32x the splat map frequency to show surface detail.
uniform sampler2D u_layer0;     // unit 5 — sRGB DXT1 (R channel = base biome layer)
uniform sampler2D u_layer1;     // unit 6 — sRGB DXT1 (G channel = asphalt)
uniform sampler2D u_layer2;     // unit 7 — sRGB DXT1 (B channel = soil)
uniform sampler2D u_layer3;     // unit 8 — sRGB DXT1 (A channel = concrete)

// Gamma fallback flag: 1 = apply manual pow(x, 2.2) gamma, 0 = GPU handles sRGB decode.
// Passed as int (not bool) — Irrlicht's setPixelShaderConstant reads 4 bytes; bool is 1 byte (UB).
// In OnSetConstants(): int srgbLinearInt = srgbLinear ? 1 : 0;
//                      services->setPixelShaderConstant("u_srgbLinear", &srgbLinearInt, 1);
uniform int u_srgbLinear;       // 1 = apply manual gamma, 0 = GPU handles sRGB

// Inputs from vertex shader.
in vec2 v_texCoord;

// Fragment output (GLSL 1.30 uses explicit out instead of gl_FragColor).
out vec4 fragColor;

void main() {
    // Sample the splat map at the terrain UV.
    // Splat map is low-resolution (16x16 px per 64m chunk) — nearest/linear filtering is fine.
    // RGBA channels encode blend weights for the four terrain surface layers.
    vec4 splat = texture(u_splatMap, v_texCoord);

    // Tile the detail layers at higher frequency than the splat map.
    // Factor 32.0 matches the spec: each detail texture repeats 32 times per chunk UV [0,1].
    // This produces visible surface detail (grass blades, asphalt texture, etc.) at close range.
    vec2 detailUV = v_texCoord * 32.0;
    vec4 c0 = texture(u_layer0, detailUV);
    vec4 c1 = texture(u_layer1, detailUV);
    vec4 c2 = texture(u_layer2, detailUV);
    vec4 c3 = texture(u_layer3, detailUV);

    // Apply manual gamma correction when GPU sRGB decode is unavailable.
    // This converts linear-uploaded textures to approximately linear light for correct blending.
    // Skipped when sRGB HW path is active (GPU decodes sRGB to linear automatically).
    if (u_srgbLinear != 0) {
        c0.rgb = pow(c0.rgb, vec3(2.2));
        c1.rgb = pow(c1.rgb, vec3(2.2));
        c2.rgb = pow(c2.rgb, vec3(2.2));
        c3.rgb = pow(c3.rgb, vec3(2.2));
    }

    // Blend the four layers by their splat weights.
    // RGBA splat channels map to layer0/1/2/3 weights respectively.
    // The splat weights are authored to sum to 1.0 per texel (no over-brightening).
    vec4 color = c0 * splat.r + c1 * splat.g + c2 * splat.b + c3 * splat.a;
    fragColor = vec4(color.rgb, 1.0);
}
