#version 130

// cloud_dome.frag — cloud dome fragment shader (Phase 10b, rev 4).
//
// Horizon fade uses BOTH per-vertex alpha AND per-fragment elevation-angle fade.
// The per-vertex alpha (baked into the dome mesh by buildCloudDomeMesh()) is the
// PRIMARY fade mechanism — it works even when this shader fails to compile and
// Irrlicht falls back to EMT_TRANSPARENT_VERTEX_ALPHA.  The per-fragment fade
// adds a smoother, higher-quality transition on top when the shader IS active.
//
// Vertex alpha fade: -7° to +5° (baked into mesh, see buildCloudDomeMesh).
// Fragment elevation fade: -7° to +5° (matches vertex fade — reinforces it).
// Atmospheric haze: blends cloud RGB toward sky colour near horizon.
//
// The elevation angle is computed in the vertex shader: the dome node tracks the
// camera's full XYZ position each frame, so gl_Vertex.y is the camera-relative
// local Y offset.  No u_cameraY uniform is needed or used.

uniform sampler2D u_tex;

in vec2  v_texCoord;
in float v_elevAngle;   // elevation angle (radians) from camera to fragment

void main() {
    vec4 tex = texture2D(u_tex, v_texCoord);

    // ---------- elevation-angle alpha fade ----------
    // Matches the vertex alpha fade band so both reinforce each other.
    // Below -7°: fully transparent.  Above +5°: fully opaque.
    const float kElevAlphaEnd  = -0.12;   // ~-7° — transparent below
    const float kElevAlphaHigh =  0.09;   // ~+5° — fully opaque above

    float ta = clamp((v_elevAngle - kElevAlphaEnd) / (kElevAlphaHigh - kElevAlphaEnd),
                     0.0, 1.0);
    float horizFade = ta * ta * (3.0 - 2.0 * ta);   // smoothstep [0, 1]

    // ---------- atmospheric haze colour blend ----------
    // Near the horizon, blend cloud RGB toward the sky background colour so any
    // residual azimuthal texture variation has no visible colour contrast.
    // Sky background matches driver clear colour SColor(255, 100, 149, 237).
    const vec3  kSkyColor    = vec3(0.392, 0.584, 0.929);
    const float kHazeEnd     = -0.12;   // matches kElevAlphaEnd
    const float kHazeHigh    =  0.09;   // matches kElevAlphaHigh

    float th = clamp((v_elevAngle - kHazeEnd) / (kHazeHigh - kHazeEnd), 0.0, 1.0);
    float hazeBlend = 1.0 - th * th * (3.0 - 2.0 * th); // 1 at bottom, 0 above +5°

    vec3 cloudColor = mix(tex.rgb, kSkyColor, hazeBlend);

    // Multiply texture alpha, fragment elevation fade, AND vertex alpha (from
    // gl_Color which carries the interpolated vertex colour).  This ensures
    // the vertex-baked fade is respected even when this shader is active.
    float alpha = tex.a * horizFade * gl_Color.a;
    gl_FragColor = vec4(cloudColor, alpha);
}
