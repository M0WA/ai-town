// stub_succeed.cpp — Phase 0 stub for opengl_tests target (requires-opengl label).
// The lod_swap_smoke_test.cpp mentioned in documentation is a Phase 6 deliverable
// and must NOT appear in this source list before Phase 6.
// This compile-check also verifies IRenderer.h and its transitive include (vec3.h)
// are well-formed — a stub that only includes gtest headers does not perform this check.
#include "src/interfaces/IRenderer.h"
#include <gtest/gtest.h>

TEST(OpenGLStub, Succeed) {
    SUCCEED();
}
