#version 130

// cloud_dome.frag — cloud dome fragment shader (Phase 10b, rev 4).
//
// Horizon fade uses BOTH per-vertex alpha AND per-fragment elevation-angle fade.
// The per-vertex alpha (baked into the dome mesh by buildCloudDomeMesh()) is the
// PRIMARY fade mechanism — it works even when this shader fails to compile and
// Irrlicht falls back to EMT_TRANSPARENT_VERTEX_ALPHA.  The per-fragment fade
// adds a smoother, higher-quality transition on top when the shader IS active.
//
// Vertex alpha fade: opaque below +2°, transparent above +15° (baked into mesh).
// Fragment elevation fade: opaque below +2°, transparent above +15° (matches vertex fade).
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

    // ---------- alpha fade ----------
    // Dome is fully opaque at and below +2° (horizon) where haze makes it sky-blue.
    // Fades to transparent above +15° so open sky is visible higher up.
    const float kAlphaFadeStart =  0.035;  // ~+2°  — fully opaque below this
    const float kAlphaFadeEnd   =  0.26;   // ~+15° — fully transparent above this

    float ta = clamp((v_elevAngle - kAlphaFadeStart) / (kAlphaFadeEnd - kAlphaFadeStart),
                     0.0, 1.0);
    float horizFade = 1.0 - ta * ta * (3.0 - 2.0 * ta);  // 1 at bottom, 0 above +15°

    // ---------- atmospheric haze colour blend ----------
    // Near the horizon, blend cloud RGB toward the sky background colour so any
    // residual azimuthal texture variation has no visible colour contrast.
    // Sky background matches driver clear colour SColor(255, 100, 149, 237).
    const vec3  kSkyColor    = vec3(0.392, 0.584, 0.929);
    const float kHazeEnd     =  0.035;  // ~+2° — sky-blue below the opaque threshold
    const float kHazeHigh    =  0.09;   // ~+5° — cloud texture above this

    float th = clamp((v_elevAngle - kHazeEnd) / (kHazeHigh - kHazeEnd), 0.0, 1.0);
    float hazeBlend = 1.0 - th * th * (3.0 - 2.0 * th); // 1 below +2°, 0 above +5°

    vec3 cloudColor = mix(tex.rgb, kSkyColor, hazeBlend);

    // Near the horizon (hazeBlend=1), use full alpha regardless of tex.a so that
    // transparent gaps in the cloud texture do not expose the terrain edge.
    // Above +5° (hazeBlend=0), tex.a controls cloud sparseness normally.
    float alpha = mix(tex.a, 1.0, hazeBlend) * horizFade * gl_Color.a;
    gl_FragColor = vec4(cloudColor, alpha);
}
