#version 120

// cloud_dome.frag — cloud dome fragment shader (Phase 10b, rev 9).
//
// GLSL 1.20: uses 'varying', texture2D(), and gl_FragColor — all standard in
// 1.20.  Downgraded from 1.30 because 'out vec4 fragColor' caused silent
// compile failure on some drivers even when 'in/out' was otherwise correct.
//
// Elevation-angle fade (-5° → +20°):
//   Below -5° : fully transparent (no dome visible below horizon)
//   -5° → 20° : smoothstep alpha and haze blend toward sky colour
//   Above 20° : full cloud colour and opacity (tex.a)
//
// hazeBlend tints near-horizon fragments toward sky colour so that the fade
// boundary has no sharp dark-cloud edge regardless of cloud texture density.

uniform sampler2D u_tex;

varying vec2  v_texCoord;
varying float v_elevAngle;

void main() {
    vec4 tex = texture2D(u_tex, v_texCoord);

    // Elevation-angle smoothstep: 0 below -5°, 1 above +20°.
    const float kElevAlphaEnd  = -0.0873;  // -5° in radians
    const float kElevAlphaHigh =  0.3491;  // +20° in radians
    float t         = clamp((v_elevAngle - kElevAlphaEnd) / (kElevAlphaHigh - kElevAlphaEnd), 0.0, 1.0);
    float horizFade = t * t * (3.0 - 2.0 * t);  // smoothstep

    // Haze blend: near-horizon cloud RGB fades toward sky colour.
    // This eliminates the dark-cloud band artefact at the fade boundary.
    float hazeBlend = 1.0 - horizFade;
    vec3  skyColor  = vec3(0.392, 0.584, 0.929);  // SColor(255,100,149,237)/255
    vec3  cloudColor = mix(tex.rgb, skyColor, hazeBlend);

    gl_FragColor = vec4(cloudColor, tex.a * horizFade);
}
