// irrlicht_ui_backend_compile_test.cpp — Phase 1 IrrlichtUIBackend non-abstract
// compile check and CTest registration for the integration_tests target.
//
// Purpose (two-part):
//   1. static_assert: verifies at compile time that IrrlichtUIBackend overrides
//      ALL 17 IUIBackend pure-virtual methods. A missing override leaves
//      IrrlichtUIBackend abstract; instantiating it in production would fail to
//      link. This compile-time gate catches the error before runtime.
//   2. TEST() body: required so CTest can discover at least one test from the
//      integration_tests binary. A binary with only a static_assert and no
//      TEST() registrations reports 0 discovered tests, breaking the Phase 3
//      integration label routing non-zero discovery CI step.
//
// CMake registration notes (per phase-1.md §IrrlichtUIBackend Non-Abstract):
//   - This file is added to integration_tests (NOT ui_tests).
//   - integration_tests target_link_libraries MUST include aitown_render and
//     aitown_ui — IrrlichtUIBackend.h depends on Irrlicht headers available
//     only when aitown_render is linked.
//   - aitown_add_tests(integration_tests LABEL "integration") MUST be present
//     in the same commit (atomicity requirement).
//   - target_include_directories(integration_tests ...) MUST include src/rendering/
//     because IrrlichtUIBackend.h lives there.
//
// Spec ref: phase-1.md §IrrlichtUIBackend Non-Abstract Compile Check
//           architecture/testing/testability-architecture.md §IUIBackend
#include "src/rendering/IrrlichtUIBackend.h"
#include <type_traits>
#include <gtest/gtest.h>

static_assert(!std::is_abstract_v<IrrlichtUIBackend>,
    "IrrlichtUIBackend must override all 17 IUIBackend pure-virtual methods");

TEST(IrrlichtUIBackendCompileCheck, IsNonAbstract) {
    // Static assert above already validates at compile time.
    // This TEST body exists solely for CTest discovery registration.
    SUCCEED();
}
