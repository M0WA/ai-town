#version 130

// road.frag — Phase 9 road tile fragment shader.
// Samples road_asphalt_tileable.dds at UV × 2.0 (tiles 2× per road tile per spec).
//
// Texture unit assignments (must match shader_constants.h):
//   Unit 0: u_diffuseMap — road_asphalt_tileable.dds
//
// This project does NOT use an sRGB framebuffer (GL_FRAMEBUFFER_SRGB is not enabled).
// The goal is that the authored texel values (~RGB 82,80,82 asphalt gray) appear on screen
// with their authored colors regardless of whether GL_EXT_texture_sRGB is present.
//
// Two upload paths (selected in IrrlichtRenderer::initRoadShader):
//   u_srgbLinear == 0 (sRGB extension present):
//     Texture uploaded as GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT.
//     GPU decodes stored sRGB values to linear on sample (~0.085 for asphalt ~82/255).
//     Must re-encode linear → sRGB (pow(x, 1/2.2)) before writing to the non-sRGB
//     framebuffer, recovering the authored ~RGB(82,80,82) display appearance.
//   u_srgbLinear == 1 (sRGB extension absent):
//     Texture uploaded as GL_COMPRESSED_RGBA_S3TC_DXT5_EXT (linear, no GPU decode).
//     Stored bytes are the raw authored values (~0.32 for asphalt gray).
//     Output directly — no correction needed for a non-sRGB framebuffer.
//
// UV tiling: spec requires UV tiles 2× per road tile (architecture/asset-standards/
// 2d-texture-standards.md — road_asphalt_tileable.dds row). Road tile quad has UV [0,1];
// multiplying by 2.0 in the shader produces the required 2× tiling.

// Road diffuse texture — unit 0.
uniform sampler2D u_diffuseMap;   // unit 0 — road_asphalt_tileable.dds

// Upload-path flag.
// 0 = sRGB upload (GPU decoded to linear); apply pow(x, 1/2.2) inverse to recover display.
// 1 = linear upload (raw authored values); output directly.
// Passed as int (not bool) — Irrlicht's setPixelShaderConstant reads 4 bytes; bool is 1 byte (UB).
uniform int u_srgbLinear;

// Inputs from vertex shader.
in vec2 v_texCoord;

// Fragment output (GLSL 1.30 uses explicit out instead of gl_FragColor).
out vec4 fragColor;

void main() {
    // Sample road diffuse at UV × 2.0.
    // Per spec: road_asphalt_tileable.dds UV tiles 2× per road tile.
    vec2 roadUV = v_texCoord * 2.0;
    vec4 color = texture(u_diffuseMap, roadUV);

    // Re-encode linear → sRGB when the GPU decoded the texture from sRGB format.
    // pow(x, 1/2.2) is the approximate sRGB gamma encode (linear → display-referred sRGB).
    // Without this, the linearly decoded values (~0.085) appear very dark on the
    // non-sRGB framebuffer. With this correction the authored ~RGB(82,80,82) is recovered.
    if (u_srgbLinear == 0) {
        color.rgb = pow(color.rgb, vec3(1.0 / 2.2));
    }

    fragColor = color;
}
