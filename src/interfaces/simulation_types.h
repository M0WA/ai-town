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
