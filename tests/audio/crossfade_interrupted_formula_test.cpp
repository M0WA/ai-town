// crossfade_interrupted_formula_test.cpp
// Phase 10: verify the interrupted crossfade arccos formula produces no NaN or
// domain error at the three critical boundary values of current_gain_out.
//
// Spec reference: architecture/audio-architecture/dynamic-soundscape.md
//   t_offset = (2/pi) * arccos(current_gain_out)
//   where current_gain_out is the incoming stem B's gain at the moment of
//   interruption (stem B becomes the new outgoing in the B→C crossfade).
//
// Risk documented in implementation/phase-10.md:
//   "Interrupted crossfade t_offset formula uses arccos — verify no domain
//    error when current_gain_out is exactly 0.0 or 1.0."
//
// These tests do NOT require a real AudioSystem or OpenAL device.
// They test the math formula in isolation.

#include <gtest/gtest.h>
#include <cmath>
#include <limits>

// ---------------------------------------------------------------------------
// Helpers — mirror the formula from dynamic-soundscape.md exactly so that the
// test verifies the correct formula (not just any monotone mapping of [0,1]).
// ---------------------------------------------------------------------------
namespace {

// Constant-power crossfade gain curves
// gain_in  = sin(t * pi/2)
// gain_out = cos(t * pi/2)
// where t runs [0, 1] over the crossfade duration.
//
// Interrupted crossfade: given the current gain_out value at the moment of
// interruption, recover t_offset so the new crossfade starts from that point:
//   t_offset = (2 / pi) * arccos(current_gain_out)
//
// Expected boundary values:
//   current_gain_out = 1.0  =>  t_offset = (2/pi)*arccos(1.0) = (2/pi)*0 = 0.0
//   current_gain_out = 0.0  =>  t_offset = (2/pi)*arccos(0.0) = (2/pi)*(pi/2) = 1.0
//   current_gain_out = 0.5  =>  t_offset = (2/pi)*arccos(0.5) = (2/pi)*(pi/3) = 2/3

inline float computeTOffset(float current_gain_out)
{
    // arccos domain is [-1, 1].  current_gain_out must be clamped to avoid
    // domain error caused by floating-point values marginally outside [0, 1].
    const float clamped = std::max(0.0f, std::min(1.0f, current_gain_out));
    return (2.0f / static_cast<float>(M_PI)) * std::acos(clamped);
}

} // namespace

// ---------------------------------------------------------------------------
// CrossfadeTest
// ---------------------------------------------------------------------------
TEST(CrossfadeTest, Crossfade_InterruptedFormula_NoDomainErrorAtBoundary)
{
    // --- current_gain_out = 1.0 -------------------------------------------
    // At the very start of a crossfade the outgoing stem is at full volume
    // (gain_out = cos(0) = 1.0).  Interrupted immediately → t_offset = 0.
    const float t_at_1 = computeTOffset(1.0f);
    EXPECT_FALSE(std::isnan(t_at_1))
        << "arccos(1.0) must not produce NaN";
    EXPECT_FALSE(std::isinf(t_at_1))
        << "arccos(1.0) must not produce Inf";
    EXPECT_NEAR(t_at_1, 0.0f, 1e-5f)
        << "t_offset at gain_out=1.0 must be 0.0 (crossfade just started)";

    // --- current_gain_out = 0.0 -------------------------------------------
    // At the very end of a crossfade the outgoing stem has faded out
    // (gain_out = cos(pi/2) = 0.0).  Interrupted at the end → t_offset = 1.
    const float t_at_0 = computeTOffset(0.0f);
    EXPECT_FALSE(std::isnan(t_at_0))
        << "arccos(0.0) must not produce NaN";
    EXPECT_FALSE(std::isinf(t_at_0))
        << "arccos(0.0) must not produce Inf";
    EXPECT_NEAR(t_at_0, 1.0f, 1e-5f)
        << "t_offset at gain_out=0.0 must be 1.0 (crossfade completed)";

    // --- current_gain_out = 0.5 -------------------------------------------
    // Mid-crossfade: gain_out = cos(t*pi/2) = 0.5 → t = arccos(0.5)/(pi/2) = (pi/3)/(pi/2) = 2/3
    const float t_at_half = computeTOffset(0.5f);
    EXPECT_FALSE(std::isnan(t_at_half))
        << "arccos(0.5) must not produce NaN";
    EXPECT_FALSE(std::isinf(t_at_half))
        << "arccos(0.5) must not produce Inf";
    EXPECT_NEAR(t_at_half, 2.0f / 3.0f, 1e-5f)
        << "t_offset at gain_out=0.5 must be 2/3 (mid-crossfade)";

    // --- Monotone ordering check ------------------------------------------
    // t_offset must decrease as current_gain_out increases (higher gain_out
    // means the crossfade started more recently → smaller t_offset restart).
    EXPECT_GT(t_at_0, t_at_half)
        << "t_offset must be larger when gain_out is closer to 0 (later in crossfade)";
    EXPECT_GT(t_at_half, t_at_1)
        << "t_offset must be larger when gain_out is closer to 0 (later in crossfade)";

    // --- Verify gain_in complement is consistent --------------------------
    // At the recovered t_offset, gain_in = sin(t_offset * pi/2) must equal
    // sqrt(1 - gain_out^2) (Pythagorean identity for constant-power crossfade).
    auto gain_in_from_t = [](float t) {
        return std::sin(t * static_cast<float>(M_PI) / 2.0f);
    };
    // For gain_out = 0.5: gain_in = sin(2/3 * pi/2) = sin(pi/3) = sqrt(3)/2 ≈ 0.866
    const float expected_gain_in_half = std::sqrt(1.0f - 0.5f * 0.5f);  // sqrt(0.75)
    EXPECT_NEAR(gain_in_from_t(t_at_half), expected_gain_in_half, 1e-5f)
        << "gain_in at recovered t_offset must satisfy constant-power constraint";
}
