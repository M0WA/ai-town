#version 130
// ui_quad.vert — 2D UI textured-quad vertex shader (Phase 8).
// Positions are already in NDC [-1,1] range; no projection matrix needed.
// Attribute locations bound via glBindAttribLocation() before glLinkProgram()
// in IrrlichtUIBackend: a_pos = 0, a_uv = 1.
// See architecture/graphics-architecture/shader-loading.md for co-landing requirement.

in vec2 a_pos;
in vec2 a_uv;

out vec2 v_uv;

void main() {
    v_uv = a_uv;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
