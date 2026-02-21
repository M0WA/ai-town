// integration_smoke_test.cpp — Phase 0 stub for integration_tests target.
// Required so that ctest -L "^integration$" CI step discovers at least one test
// in the integration bucket from the first green pass. Without this stub, the
// integration ctest step vacuously exits 0 with no tests, making the three-step
// CI structure unverifiable at Phase 0.
#include <gtest/gtest.h>

TEST(IntegrationSmoke, Succeed) {
    SUCCEED();
}
