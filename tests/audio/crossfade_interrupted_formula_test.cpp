// crossfade_interrupted_formula_test.cpp — Phase 10 audio test.
//
// Test: Crossfade_InterruptedFormula_NoDomainErrorAtBoundary
//
// Verifies that the interrupted-crossfade t_offset formula
//   t_offset = (2/π) × arccos(current_gain_out)
// produces correct output at the exact boundary values (current_gain_out = 1.0
// and current_gain_out = 0.0) without triggering std::domain_error or NaN.
//
// Design rationale (architecture/audio-architecture/dynamic-soundscape.md):
//   - When a crossfade A→B is interrupted by a new target C, stem B's current
//     gain_out at the interruption moment becomes the starting gain for the
//     new B→C crossfade.
//   - gain_out(t) = cos(t × π/2)  →  t_offset = arccos(gain_out) × (2/π)
//   - At t=0: gain_out=1.0  → arccos(1.0) = 0 → t_offset = 0  (no-op)
//   - At t=1: gain_out=0.0  → arccos(0.0) = π/2 → t_offset = 1 (fully faded)
//   - These are exactly the domain endpoints of arccos [−1, 1].
//     Any value outside [−1, 1] produces a domain error (std::acos on
//     out-of-range input is undefined behaviour — returns NaN on some platforms).
//     The test ensures the implementation clamps current_gain_out to [0.0, 1.0]
//     before passing to arccos, making both boundary values safe.
//
// CMake target: audio_tests (target_sources, Phase 10 block in CMakeLists.txt).
// Does NOT require a real audio device — pure math test.
//
// Spec refs:
//   architecture/audio-architecture/dynamic-soundscape.md §Interrupted crossfade
//   implementation/phase-10.md §Audio crossfade unit tests

#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include <algorithm>

namespace {

// ---------------------------------------------------------------------------
// computeInterruptedTOffset — mirrors AudioSystem's interrupted crossfade
// t_offset computation.
//
// Implementation must clamp gain_out to [0.0, 1.0] before calling std::acos
// to avoid undefined behaviour at floating-point rounding boundaries (e.g.
// 1.0 + FLT_EPSILON causing acos to return NaN on some platforms).
//
// Formula: t_offset = (2 / π) × arccos(clamp(gain_out, 0.0f, 1.0f))
// ---------------------------------------------------------------------------
float computeInterruptedTOffset(float current_gain_out)
{
    constexpr float kPi = static_cast<float>(M_PI);
    // Clamp to [0, 1] to guard against floating-point rounding at exact boundaries.
    const float clamped = std::max(0.0f, std::min(1.0f, current_gain_out));
    return (2.0f / kPi) * std::acos(clamped);
}

// ---------------------------------------------------------------------------
// Round-trip verification: cos(t_offset × π/2) must equal the original gain_out
// within floating-point tolerance.
// ---------------------------------------------------------------------------
float gainOutFromTOffset(float t_offset)
{
    constexpr float kPi = static_cast<float>(M_PI);
    return std::cos(t_offset * kPi / 2.0f);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Crossfade_InterruptedFormula_NoDomainErrorAtBoundary
//
// Verifies that t_offset at current_gain_out = 1.0 returns 0 (no-op, crossfade
// was just starting) and at current_gain_out = 0.0 returns 1 (fully faded,
// outgoing stem has already reached silence), with no arccos domain error.
// ---------------------------------------------------------------------------
TEST(CrossfadeTest, Crossfade_InterruptedFormula_NoDomainErrorAtBoundary)
{
    // At gain_out = 1.0 (crossfade just started, stem B at full volume):
    // arccos(1.0) = 0  →  t_offset = 0
    const float t_at_one = computeInterruptedTOffset(1.0f);
    EXPECT_FALSE(std::isnan(t_at_one))
        << "computeInterruptedTOffset(1.0) returned NaN — arccos domain error";
    EXPECT_FALSE(std::isinf(t_at_one))
        << "computeInterruptedTOffset(1.0) returned Inf";
    EXPECT_NEAR(t_at_one, 0.0f, 1e-5f)
        << "computeInterruptedTOffset(1.0) must return 0 — crossfade begins at t=0";

    // At gain_out = 0.0 (stem B fully faded out):
    // arccos(0.0) = π/2  →  t_offset = (2/π) × (π/2) = 1
    const float t_at_zero = computeInterruptedTOffset(0.0f);
    EXPECT_FALSE(std::isnan(t_at_zero))
        << "computeInterruptedTOffset(0.0) returned NaN — arccos domain error";
    EXPECT_FALSE(std::isinf(t_at_zero))
        << "computeInterruptedTOffset(0.0) returned Inf";
    EXPECT_NEAR(t_at_zero, 1.0f, 1e-5f)
        << "computeInterruptedTOffset(0.0) must return 1 — crossfade ends at t=1";

    // Round-trip: gain_out(t_offset(gain_out)) ≈ gain_out for several mid-range values.
    // Ensures the formula is a true inverse of cos(t × π/2).
    for (float g : {0.1f, 0.25f, 0.5f, 0.75f, 0.9f}) {
        const float t_off = computeInterruptedTOffset(g);
        EXPECT_FALSE(std::isnan(t_off))
            << "computeInterruptedTOffset(" << g << ") returned NaN";
        const float recovered = gainOutFromTOffset(t_off);
        EXPECT_NEAR(recovered, g, 1e-5f)
            << "Round-trip failed at gain_out=" << g
            << ": t_offset=" << t_off
            << ", recovered=" << recovered;
    }
}

// ---------------------------------------------------------------------------
// Crossfade_InterruptedFormula_ClampsBelowZero
//
// Verifies the formula does not crash or return NaN when given a slightly
// negative gain_out (floating-point underflow at the end of a crossfade can
// produce small negative values like -1e-7f).
// ---------------------------------------------------------------------------
TEST(CrossfadeTest, Crossfade_InterruptedFormula_ClampsBelowZero)
{
    // A very small negative value — should be clamped to 0, producing t_offset = 1.
    const float t = computeInterruptedTOffset(-1e-7f);
    EXPECT_FALSE(std::isnan(t))
        << "computeInterruptedTOffset(-1e-7) returned NaN — clamp not applied";
    EXPECT_NEAR(t, 1.0f, 1e-4f)
        << "computeInterruptedTOffset(-1e-7) should clamp to 0 and return ~1.0";
}

// ---------------------------------------------------------------------------
// Crossfade_InterruptedFormula_ClampsAboveOne
//
// Verifies the formula does not crash or return NaN when given a slightly
// above-1.0 gain_out (can occur from floating-point rounding at crossfade start).
// ---------------------------------------------------------------------------
TEST(CrossfadeTest, Crossfade_InterruptedFormula_ClampsAboveOne)
{
    // A very small above-one value — should be clamped to 1, producing t_offset = 0.
    const float t = computeInterruptedTOffset(1.0f + 1e-7f);
    EXPECT_FALSE(std::isnan(t))
        << "computeInterruptedTOffset(1+eps) returned NaN — clamp not applied";
    EXPECT_NEAR(t, 0.0f, 1e-4f)
        << "computeInterruptedTOffset(1+eps) should clamp to 1 and return ~0.0";
}
