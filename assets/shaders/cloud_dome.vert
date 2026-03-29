#version 120

// cloud_dome.vert — cloud dome vertex shader (Phase 10b, rev 7).
//
// GLSL 1.20: uses 'varying' (not in/out), gl_Color are all standard in 1.20.
// Downgraded from 1.30 because mixing gl_FrontColor with 'out' varyings caused
// silent compile failure on some drivers.
//
// Passes UV coordinates and elevation angle to the fragment shader.
// Horizon fade and haze blend are handled entirely in the fragment shader.

varying vec2  v_texCoord;
varying float v_elevAngle;   // elevation angle (radians) from camera to vertex

void main() {
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
    v_texCoord  = gl_MultiTexCoord0.st;

    // Elevation angle from camera to this dome vertex.
    // The dome node tracks the camera's full XYZ position each frame, so
    // gl_Vertex.y is already the vertical offset from the camera.
    float horizDist = length(gl_Vertex.xz);
    float deltaY    = gl_Vertex.y;
    v_elevAngle = atan(deltaY, max(horizDist, 0.1));
}
