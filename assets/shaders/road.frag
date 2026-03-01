#version 130

// road.frag — Phase 9 road tile fragment shader.
// Samples road_asphalt_tileable.dds at UV × 2.0 (tiles 2× per road tile per spec).
// Applies optional manual gamma correction when GPU sRGB decode is unavailable.
//
// Texture unit assignments (must match shader_constants.h):
//   Unit 0: u_diffuseMap — road_asphalt_tileable.dds (sRGB DXT5 raw-GL path)
//
// sRGB gamma fallback: when GL_EXT_texture_sRGB is absent, the road texture is
// uploaded as linear (GL_COMPRESSED_RGBA_S3TC_DXT5_EXT). The uniform int u_srgbLinear
// (1 = apply manual gamma, 0 = GPU handles sRGB) activates a pow(color.rgb, vec3(2.2))
// correction. This avoids the need for two shader variants.
// See architecture/graphics-architecture/shader-loading.md §sRGB Gamma Fallback.
//
// UV tiling: spec requires UV tiles 2× per road tile (architecture/asset-standards/
// 2d-texture-standards.md — road_asphalt_tileable.dds row). Road tile quad has UV [0,1];
// multiplying by 2.0 in the shader produces the required 2× tiling.

// Road diffuse texture — unit 0.
// Uploaded via raw-GL sRGB path: glCompressedTexImage2D(GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT).
uniform sampler2D u_diffuseMap;   // unit 0 — road_asphalt_tileable.dds

// Gamma fallback flag: 1 = apply manual pow(x, 2.2) gamma, 0 = GPU handles sRGB decode.
// Passed as int (not bool) — Irrlicht's setPixelShaderConstant reads 4 bytes; bool is 1 byte (UB).
// In OnSetConstants(): int v = m_srgbSupported ? 0 : 1;
//                      services->setPixelShaderConstant("u_srgbLinear", &v, 1);
uniform int u_srgbLinear;         // 1 = apply manual gamma, 0 = GPU handles sRGB

// Inputs from vertex shader.
in vec2 v_texCoord;

// Fragment output (GLSL 1.30 uses explicit out instead of gl_FragColor).
out vec4 fragColor;

void main() {
    // Sample road diffuse at UV × 2.0.
    // Per spec: road_asphalt_tileable.dds UV tiles 2× per road tile.
    vec2 roadUV = v_texCoord * 2.0;
    vec4 color = texture(u_diffuseMap, roadUV);

    // Apply manual gamma correction when GPU sRGB decode is unavailable.
    // Converts linear-uploaded texture to approximately linear light for
    // correct blending. Skipped when sRGB HW path is active.
    if (u_srgbLinear != 0) {
        color.rgb = pow(color.rgb, vec3(2.2));
    }

    fragColor = color;
}
