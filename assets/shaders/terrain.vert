#version 130

// terrain.vert — Phase 5 full implementation.
// Terrain splat-map vertex shader.
// See architecture/graphics-architecture/shader-loading.md for callback and uniform requirements.
// See architecture/graphics-architecture/texture-cache.md for texture unit assignments.
//
// NOTE: Irrlicht's GLSL backend ignores the EVST_VS_* enum value entirely.
// The active GLSL version is determined exclusively by the #version directive above.
// Use EVST_VS_1_1 as the conventional placeholder when calling addHighLevelShaderMaterialFromFiles.
//
// Transform: uses gl_ModelViewProjectionMatrix (GLSL compatibility built-in).
// Irrlicht uses an OpenGL compatibility-profile context and maintains the OpenGL
// matrix stacks via glLoadMatrix / glMultMatrix internally when processing scene nodes.
// gl_ModelViewProjectionMatrix = Projection * View * World — always correct for the
// current scene node being drawn. No manual uWVPMatrix uniform is needed.
//
// gl_Vertex         — position from glVertexPointer   (compatibility built-in, location 0)
// gl_MultiTexCoord0 — texcoord from glTexCoordPointer unit 0 (location 8)
// Using these built-ins avoids attribute-location ambiguity when the GLSL linker assigns
// user-defined `in` variable locations in implementation-defined order.

// Outputs to fragment shader.
out vec2 v_texCoord;

void main() {
    // Transform vertex position to clip space using the GLSL compatibility built-in.
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;

    // Pass UV coordinates straight through to the fragment shader.
    // The fragment shader tiles detail layers at 32x frequency (detailUV = v_texCoord * 32.0).
    v_texCoord = gl_MultiTexCoord0.st;
}
