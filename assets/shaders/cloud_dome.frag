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

    // Elevation-angle horizon fade — symmetric in all compass directions.
    const float kElevFadeEnd  = 0.2094;   // 12° in radians — transparent below this
    const float kElevFadeHigh = 0.5236;   // 30° in radians — fully opaque above this

    // smoothstep maps [kElevFadeEnd, kElevFadeHigh] → [0, 1]
    float t = clamp((v_elevAngle - kElevFadeEnd) / (kElevFadeHigh - kElevFadeEnd), 0.0, 1.0);
    float horizFade = t * t * (3.0 - 2.0 * t);   // smoothstep

    // Cloud alpha = texture cloud mask × elevation fade.
    float alpha = tex.a * horizFade;

    gl_FragColor = vec4(tex.rgb, alpha);
}
