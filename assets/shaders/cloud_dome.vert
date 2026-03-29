#version 130

// cloud_dome.vert — cloud dome vertex shader (Phase 10b, rev 2).
//
// Passes UV coordinates and the elevation angle from the camera to this
// vertex to the fragment shader.  The fragment shader computes a smooth
// elevation-angle fade that is SYMMETRIC in all azimuths — solving the
// directional arc artifact caused by the previous per-vertex alpha approach.
//
// Per-vertex alpha: REMOVED.  All dome vertices now have alpha=255 (SColor).
// The shader owns all horizon-fade logic, keyed on elevation angle.
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
