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
//
// Transform: uses gl_ModelViewProjectionMatrix (GLSL compatibility built-in).
// Irrlicht uses an OpenGL compatibility-profile context and maintains the OpenGL
// matrix stacks via glLoadMatrix / glMultMatrix internally when processing scene nodes.
// gl_ModelViewProjectionMatrix = Projection * View * World — always correct for the
// current scene node being drawn. No manual uWVPMatrix uniform is needed.

// Outputs to fragment shader.
out vec2 v_texCoord;
out vec3 v_normal;

void main() {
    // Transform vertex position to clip space using the GLSL compatibility built-in.
    // Irrlicht uses a legacy OpenGL compatibility-profile context and drives the scene
    // graph via glVertexPointer / glNormalPointer / glTexCoordPointer (fixed-function
    // arrays) and glLoadMatrix / glMultMatrix on the GL_MODELVIEW / GL_PROJECTION stacks.
    // gl_ModelViewProjectionMatrix (built-in) = Projection * ModelView = P * V * W —
    // always current for the active scene node being drawn.  No manual uWVPMatrix uniform.
    //
    // gl_Vertex      — position from glVertexPointer   (compatibility built-in, location 0)
    // gl_Normal      — normal  from glNormalPointer    (compatibility built-in, location 2)
    // gl_MultiTexCoord0 — texcoord from glTexCoordPointer unit 0 (location 8)
    // Using these built-ins avoids attribute-location ambiguity when the GLSL linker assigns
    // user-defined `in` variable locations in implementation-defined order.
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;

    // Pass UV coordinates from the fixed-function texture coordinate array (unit 0).
    // The fragment shader tiles 2× by multiplying v_texCoord * 2.0.
    v_texCoord = gl_MultiTexCoord0.st;

    // Pass normal for potential future per-fragment lighting.
    v_normal = gl_Normal;
}
