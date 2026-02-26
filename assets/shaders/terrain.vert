#version 130

// terrain.vert — Phase 5 full implementation.
// Terrain splat-map vertex shader.
// See architecture/graphics-architecture/shader-loading.md for callback and uniform requirements.
// See architecture/graphics-architecture/texture-cache.md for texture unit assignments.
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

// World matrix alone — used to transform position and normal to world space for lighting.
uniform mat4 uWMatrix;

// Outputs to fragment shader.
out vec2 v_texCoord;
out vec3 v_worldPos;
out vec3 v_normal;

void main() {
    // Transform vertex position to clip space.
    gl_Position = uWVPMatrix * vec4(inVertexPosition, 1.0);

    // Pass UV coordinates straight through to the fragment shader.
    // The fragment shader tiles detail layers at 32x frequency (detailUV = v_texCoord * 32.0).
    v_texCoord = inTexCoord0;

    // Compute world-space position for potential future per-fragment lighting.
    v_worldPos = (uWMatrix * vec4(inVertexPosition, 1.0)).xyz;

    // Transform normal to world space.
    // Using the world matrix directly (not the inverse-transpose) is an approximation
    // that is valid for uniform-scale transforms. Terrain chunks use uniform scale only.
    v_normal = normalize((uWMatrix * vec4(inVertexNormal, 0.0)).xyz);
}
