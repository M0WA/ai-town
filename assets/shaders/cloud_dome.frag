#version 130

// cloud_dome.frag — cloud dome fragment shader (Phase 10b, rev 2).
//
// Horizon fade is now ELEVATION-ANGLE based (not per-vertex alpha):
//   - Fully opaque at or above kElevFadeHigh  (default 10° = 0.1745 rad)
//   - Fully transparent at or below kElevFadeEnd (default 2° = 0.0349 rad)
//   - Smooth smoothstep between them
//
// This produces a SYMMETRIC horizontal band at a fixed elevation above the
// horizon — identical in every compass direction.  It eliminates the
// directional arc that appeared with per-vertex alpha because the cloud
// texture varies by azimuth in the fade band.
//
// The per-vertex alpha artefact (EMT_TRANSPARENT_VERTEX_ALPHA ignoring tex.a)
// is already fixed by the shader multiplying tex.a.  The elevation-angle fade
// additionally guarantees no directional arc.

uniform sampler2D u_tex;

in vec2  v_texCoord;
in float v_elevAngle;   // elevation angle (radians) from camera to fragment

void main() {
    vec4 tex = texture2D(u_tex, v_texCoord);

    // ---------- elevation-angle alpha fade ----------
    // Clouds become fully transparent below kElevAlphaEnd (+5°, above the terrain
    // horizon) and fully opaque above kElevAlphaHigh (20°).
    // Raising the fade start to +5° ensures the dome is fully transparent at and
    // below +5° elevation, eliminating any cloud arch artefact at the horizon.
    const float kElevAlphaEnd  =  0.0873;  // +5° — transparent at/below 5° above horizon (eliminates arch artefact)
    const float kElevAlphaHigh =  0.3491;  //  20° — fully opaque above this

    float ta = clamp((v_elevAngle - kElevAlphaEnd) / (kElevAlphaHigh - kElevAlphaEnd),
                     0.0, 1.0);
    float horizFade = ta * ta * (3.0 - 2.0 * ta);   // smoothstep [0, 1]

    // ---------- atmospheric haze colour blend ----------
    // Near the horizon any residual directional variation in the cloud texture
    // (azimuthal non-uniformity from the cylindrical UV) would create a visible arc.
    // Blending the cloud colour toward the sky background colour near the horizon
    // makes even dense cloud texels look like sky haze, eliminating the contrast
    // that made the arc perceptible.
    // Sky background matches driver clear colour SColor(255, 100, 149, 237).
    const vec3  kSkyColor    = vec3(0.392, 0.584, 0.929);
    const float kHazeEnd     =  0.0873;  // full sky colour at +5° and below (matches kElevAlphaEnd)
    const float kHazeHigh    =  0.3491;  //  20° — no haze above this

    float th = clamp((v_elevAngle - kHazeEnd) / (kHazeHigh - kHazeEnd), 0.0, 1.0);
    float hazeBlend = 1.0 - th * th * (3.0 - 2.0 * th); // 1 at horizon, 0 above 20°

    vec3 cloudColor = mix(tex.rgb, kSkyColor, hazeBlend);

    float alpha = tex.a * horizFade;
    gl_FragColor = vec4(cloudColor, alpha);
}
