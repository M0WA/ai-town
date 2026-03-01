#version 130
// ui_quad.frag — 2D UI textured-quad fragment shader (Phase 8).
// Samples u_tex at interpolated UV and outputs the texel colour.
// See architecture/graphics-architecture/shader-loading.md for co-landing requirement.

uniform sampler2D u_tex;

in vec2 v_uv;

void main() {
    gl_FragColor = texture(u_tex, v_uv);
}
