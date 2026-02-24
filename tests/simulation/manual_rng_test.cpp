#include "tests/simulation/manual_rng.h"
#include <gtest/gtest.h>
#include <stdexcept>

// Test 1: verifyAllConsumed throws if int sequence not fully consumed
TEST(ManualRNG, VerifyAllConsumed_ThrowsOnOverProvision) {
    ManualRNG rng{{42, 99}, {0.1f}};
    rng.nextFloat();
    // Only 0 ints consumed out of 2 provided
    EXPECT_THROW(rng.verifyAllConsumed(), std::logic_error);
}

// Test 2: verifyAllConsumed does not throw when fully consumed
TEST(ManualRNG, VerifyAllConsumed_NoThrowWhenFullyConsumed) {
    ManualRNG rng{{42}, {0.5f}};
    rng.nextInt(0, 100);
    rng.nextFloat();
    EXPECT_NO_THROW(rng.verifyAllConsumed());
}

// Test 3: construction throws if int sequence is empty
TEST(ManualRNG, EmptyIntSeq_ThrowsAtConstruction) {
    EXPECT_THROW((ManualRNG{{}, {0.1f}}), std::invalid_argument);
}

// Test 4: construction throws if float value out of [0,1) range
// The ManualRNG constructor throws std::out_of_range for out-of-range float values.
TEST(ManualRNG, FloatSeqOutOfRange_ThrowsAtConstruction) {
    EXPECT_THROW((ManualRNG{{1}, {1.5f}}), std::out_of_range);
}

// Test 5: construction throws if float sequence is empty
TEST(ManualRNG, EmptyFloatSeq_ThrowsAtConstruction) {
    EXPECT_THROW((ManualRNG{{1}, {}}), std::invalid_argument);
}

// Test 6: nextInt throws when called with exhausted sequence (strict mode default)
// In strict mode, exhausted int sequence throws std::logic_error.
TEST(ManualRNG, NextInt_Exhausted_ThrowsLogicError) {
    ManualRNG rng{{5}, {0.1f}};
    rng.nextInt(0, 100);  // consumes the only int
    EXPECT_THROW(rng.nextInt(0, 100), std::logic_error);
}
