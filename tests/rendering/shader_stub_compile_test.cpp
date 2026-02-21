// shader_stub_compile_test.cpp — Phase 1 stub for opengl_tests target.
// This file exists to satisfy the Phase 1 atomicity requirement:
//   - shader_stub_compile_test.cpp must be listed in add_executable(opengl_tests ...)
//     in the same commit that adds aitown_add_tests(opengl_tests LABEL "requires-opengl")
//     and the target_link_libraries(opengl_tests PRIVATE aitown_render ...) update.
// See phase-1.md §opengl_tests co-landing atomicity table for the full four-item
// atomicity requirement.
//
// Phase 1 does not have a real shader compilation test — the GLSL/HLSL shader
// loader is a Phase 5+ deliverable (architecture/graphics-architecture/shader-loading.md).
// This placeholder ensures CTest can discover at least one test from opengl_tests
// under the requires-opengl label from the first CI green pass.
#include <gtest/gtest.h>

TEST(ShaderStubCompileTest, Placeholder) {
    SUCCEED();
}
