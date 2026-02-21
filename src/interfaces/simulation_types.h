#pragma once

// Canonical definitions for shared simulation-domain types that appear in
// multiple interface headers. Both ICitySimulation.h and IAudioSystem.h
// may #include this file.

enum class ZoneType {
    Residential,
    Commercial,
    Industrial
};

enum class Difficulty {
    Easy,
    Normal,
    Hard
};

// SpeedMultiplier is the canonical enum.
// SimSpeed is a type alias that resolves the name mismatch between
// ICitySimulation.h (uses SpeedMultiplier) and IAudioSystem.h (uses SimSpeed).
enum class SpeedMultiplier {
    Paused = 0,
    x1     = 1,
    x3     = 2,
    x10    = 3
};

using SimSpeed = SpeedMultiplier;

// DensityUnlockState — snapshot of all density-unlock counters and flags.
// Referenced by ICitySimulation::getDensityUnlockState().
// Phase 1 stub returns a default-constructed DensityUnlockState{}.
// Phase 3 fills in real implementation.
// Density tiers 0-5 (6 total): one counter + one flag per tier.
struct DensityUnlockState {
    int  consecutive_months_above_threshold[6]{};  // 0-2 range; one counter per density tier
    bool unlock_flags[6]{};                        // true if the corresponding tier is unlocked
};
