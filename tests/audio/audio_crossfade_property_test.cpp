// audio_crossfade_property_test.cpp
// Phase 10: RapidCheck property tests for crossfade math invariants.
//
// Policy (architecture/testing/procedural-generation-seeds.md):
//   On RapidCheck failure, RapidCheck prints the failing seed.  Before closing
//   any failing finding, add a fixed-seed regression test using that seed.
//   Seed format in code comments: // Reproduce with seed: 0x<hex>
//
// These tests do NOT require a real AudioSystem or OpenAL device.
// They exercise the pure math helpers in isolation.

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <cmath>
#include <limits>

namespace {

// Use a portable constexpr pi instead of M_PI (not guaranteed by C++ standard).
#ifndef M_PI
constexpr double M_PI = 3.14159265358979323846;
#endif
constexpr float kPi = static_cast<float>(M_PI);

// Constant-power crossfade gain curves (dynamic-soundscape.md):
//   gain_in  = sin(t * pi/2)
//   gain_out = cos(t * pi/2)
//   for t in [0.0, 1.0]
inline float crossfadeGainIn(float t)  { return std::sin(t * kPi / 2.0f); }
inline float crossfadeGainOut(float t) { return std::cos(t * kPi / 2.0f); }

// Interrupted crossfade t_offset recovery (dynamic-soundscape.md):
//   t_offset = (2/pi) * arccos(current_gain_out)
// current_gain_out clamped to [0,1] to guard against float rounding.
inline float computeTOffset(float current_gain_out)
{
    const float clamped = std::max(0.0f, std::min(1.0f, current_gain_out));
    return (2.0f / kPi) * std::acos(clamped);
}

} // namespace

// ---------------------------------------------------------------------------
// Property: constant-power invariant holds for all t in [0, 1].
//
// For any valid crossfade position t, the sum of squared gains must equal 1:
//   gain_in(t)^2 + gain_out(t)^2 == 1
// This is the Pythagorean identity: sin^2 + cos^2 = 1.
// Violation would mean the crossfade either amplifies or attenuates total power.
// ---------------------------------------------------------------------------
TEST(CrossfadePropertyTest, ConstantPowerInvariant_HoldsForAllT)
{
    rc::check(
        "Constant-power invariant: gain_in^2 + gain_out^2 == 1 for any t in [0,1]",
        []() {
            // Generate t uniformly in [0, 1] as an integer-scaled value to avoid
            // floating-point generation corner cases.
            const int   tScaled = *rc::gen::inRange(0, 10001); // [0, 10000]
            const float t       = static_cast<float>(tScaled) / 10000.0f;

            const float g_in  = crossfadeGainIn(t);
            const float g_out = crossfadeGainOut(t);

            // Both gains must be in [0, 1].
            RC_ASSERT(g_in  >= 0.0f);
            RC_ASSERT(g_in  <= 1.0f + 1e-5f);
            RC_ASSERT(g_out >= 0.0f);
            RC_ASSERT(g_out <= 1.0f + 1e-5f);

            // Pythagorean identity: sin^2 + cos^2 == 1 (within float precision).
            const float power_sum = g_in * g_in + g_out * g_out;
            RC_ASSERT(std::abs(power_sum - 1.0f) < 2e-5f);
        });
}

// ---------------------------------------------------------------------------
// Property: t_offset is always finite and in [0, 1] for any gain_out in [0, 1].
//
// For any outgoing-stem gain at the moment of interruption, the recovered
// t_offset must be a valid crossfade restart point — never NaN, never Inf,
// never outside [0, 1].
// ---------------------------------------------------------------------------
TEST(CrossfadePropertyTest, TOffsetRoundtrip_FiniteAndInBounds)
{
    rc::check(
        "computeTOffset returns finite value in [0,1] for any gain_out in [0,1]",
        []() {
            // Generate gain_out as integer-scaled value in [0, 10000] → [0.0, 1.0].
            const int   goScaled  = *rc::gen::inRange(0, 10001);
            const float gain_out  = static_cast<float>(goScaled) / 10000.0f;

            const float t_offset = computeTOffset(gain_out);

            // Must be finite.
            RC_ASSERT(!std::isnan(t_offset));
            RC_ASSERT(!std::isinf(t_offset));

            // Must be in [0, 1].
            RC_ASSERT(t_offset >= 0.0f);
            RC_ASSERT(t_offset <= 1.0f + 1e-5f);
        });
}

// ---------------------------------------------------------------------------
// Property: t_offset is monotone-decreasing in gain_out.
//
// A higher gain_out means the outgoing stem is earlier in the crossfade
// (closer to full volume), so t_offset (the restart point) must be smaller.
// Formally: gain_out_a < gain_out_b  =>  t_offset(a) > t_offset(b).
// ---------------------------------------------------------------------------
TEST(CrossfadePropertyTest, TOffset_MonotoneDecreasing_InGainOut)
{
    rc::check(
        "computeTOffset is monotone-decreasing: higher gain_out => smaller t_offset",
        []() {
            // Generate two distinct integer-scaled gains in [0, 10000].
            const int aScaled = *rc::gen::inRange(0, 10000);
            const int bScaled = *rc::gen::inRange(aScaled + 1, 10001); // b > a

            const float gain_out_a = static_cast<float>(aScaled) / 10000.0f;
            const float gain_out_b = static_cast<float>(bScaled) / 10000.0f;

            RC_PRE(gain_out_b > gain_out_a); // guard (always true by construction)

            const float t_a = computeTOffset(gain_out_a);
            const float t_b = computeTOffset(gain_out_b);

            // arccos is monotone-decreasing: gain_out_a < gain_out_b =>
            // arccos(gain_out_a) > arccos(gain_out_b) => t_offset_a > t_offset_b.
            RC_ASSERT(t_a >= t_b);
        });
}
