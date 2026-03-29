#version 130

// cloud_dome.vert — cloud dome vertex shader (Phase 10b, rev 3).
//
// Passes UV coordinates, the elevation angle, and the vertex colour to the
// fragment shader.  The fragment shader uses all three for horizon fade:
//   - v_elevAngle: per-fragment elevation-based smoothstep fade
//   - gl_Color (via gl_FrontColor): per-vertex alpha baked into the mesh
//   - v_texCoord: cloud texture sampling
//
// The per-vertex alpha (baked by buildCloudDomeMesh) is the PRIMARY fade
// mechanism — it works even when this shader fails to compile and Irrlicht
// falls back to EMT_TRANSPARENT_VERTEX_ALPHA.  The per-fragment elevation
// fade is a refinement that adds smoother transitions when the shader IS active.
//
// NOTE: u_cameraY has been removed.  The dome node is positioned at the full
// camera XYZ each frame (setPosition(camPos) in IrrlichtRenderer::update()),
// so gl_Vertex is already in camera-relative local space.  No world-Y offset
// is needed.

out vec2  v_texCoord;
out float v_elevAngle;   // elevation angle (radians) from camera to this vertex

void main() {
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
    v_texCoord  = gl_MultiTexCoord0.st;

    // Forward vertex colour to the fragment shader via the built-in varying.
    // The fragment shader reads gl_Color.a to incorporate the per-vertex alpha
    // fade baked into the dome mesh by buildCloudDomeMesh().
    gl_FrontColor = gl_Color;

    // Elevation angle from camera to this dome vertex.
    // The dome node tracks the camera's full XYZ position, so gl_Vertex is
    // camera-relative local space.  gl_Vertex.y is already the vertical offset
    // from the camera — do NOT subtract u_cameraY again (that was the bug that
    // produced a camera-height-dependent arch artifact at the horizon).
    float horizDist = length(gl_Vertex.xz);
    float deltaY    = gl_Vertex.y;   // node tracks camera XYZ → local Y = cam-relative offset
    // atan(y, x) = atan2; guard horizDist with a tiny epsilon to avoid
    // division by zero at the apex where horizDist == 0.
    v_elevAngle = atan(deltaY, max(horizDist, 0.1));
}
