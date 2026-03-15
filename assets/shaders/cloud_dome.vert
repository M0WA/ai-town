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
// u_cameraY — world-space Y of the camera (updated each frame by callback).

uniform float u_cameraY;

out vec2  v_texCoord;
out float v_elevAngle;   // elevation angle (radians) from camera to this vertex

void main() {
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
    v_texCoord  = gl_MultiTexCoord0.st;

    // Elevation angle from camera to this dome vertex.
    // The dome node is positioned at camera.XZ so gl_Vertex.xz is the
    // horizontal offset from camera; gl_Vertex.y is world-space Y.
    float horizDist = length(gl_Vertex.xz);
    float deltaY    = gl_Vertex.y - u_cameraY;
    // atan(y, x) = atan2; guard horizDist with a tiny epsilon to avoid
    // division by zero at the apex where horizDist == 0.
    v_elevAngle = atan(deltaY, max(horizDist, 0.1));
}
