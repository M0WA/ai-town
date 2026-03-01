#version 130

// road.vert — Phase 9 road tile vertex shader.
// Standard vertex passthrough — transforms position to clip space and forwards
// UV coordinates to the fragment shader for diffuse sampling.
//
// See architecture/asset-standards/2d-texture-standards.md for road texture spec:
//   road_asphalt_tileable.dds — 1024×1024 DDS DXT5 sRGB, UV tiles 2× per road tile.
//
// See architecture/graphics-architecture/shader-loading.md for callback lifetime
// and GL_ACTIVE_TEXTURE save/restore requirements.
//
// NOTE: Irrlicht's GLSL backend ignores the EVST_VS_* enum value entirely.
// The active GLSL version is determined exclusively by the #version directive above.
// Use EVST_VS_1_1 as the conventional placeholder when calling addHighLevelShaderMaterialFromFiles.

// Irrlicht standard vertex attribute names for GLSL (used by COpenGLSLMaterialRenderer).
in vec3 inVertexPosition;
in vec3 inVertexNormal;
in vec2 inTexCoord0;

// World-view-projection matrix (combined) — set by Irrlicht's material renderer.
// uWVPMatrix = Projection * View * World.
uniform mat4 uWVPMatrix;

// Outputs to fragment shader.
out vec2 v_texCoord;
out vec3 v_normal;

void main() {
    // Transform vertex position to clip space.
    gl_Position = uWVPMatrix * vec4(inVertexPosition, 1.0);

    // Pass UV coordinates to the fragment shader.
    // The fragment shader tiles the road texture 2× by multiplying v_texCoord * 2.0.
    v_texCoord = inTexCoord0;

    // Pass normal for potential future per-fragment lighting.
    v_normal = inVertexNormal;
}
