#pragma once

#include "audio_types.h"
#include "simulation_types.h"

// Canonical IAudioSystem — 14 methods.
// Uses only game-domain types (SoundId, SoundHandle, MusicTrackId, StingerType,
// SimSpeed, SoundPriority, TimeOfDay, vec3, CameraState).
// Never expose ALuint, ALfloat, or AL_* constants through this interface.
//
// Source location: src/interfaces/ (not src/audio/) so that MockAudioSystem in
// tests/simulation/mock_audio_system.h can #include it without pulling in OpenAL headers.
class IAudioSystem {
public:
    virtual ~IAudioSystem() = default;

    // Play a non-positional (2D) one-shot sound. priority controls SFX pool eviction.
    // gain is a linear multiplier [0.0, 1.0].
    // Returns an opaque SoundHandle that can be passed to stopSound().
    virtual SoundHandle playSound(SoundId id, SoundPriority priority, float gain = 1.0f) = 0;

    // Play a world-positioned (3D) one-shot sound at pos. priority controls SFX pool eviction.
    // Returns an opaque SoundHandle that can be passed to stopSound().
    virtual SoundHandle playPositionalSound(SoundId id, vec3 pos, SoundPriority priority, float gain = 1.0f) = 0;

    // Stop a previously-started sound identified by handle.
    // Silently ignored if the handle is stale (source already finished or recycled).
    virtual void stopSound(SoundHandle handle) = 0;

    // Begin streaming the specified music track (with beat-boundary crossfade from current track).
    virtual void setMusicTrack(MusicTrackId id) = 0;

    // Notify the audio system of the current simulation speed so that
    // time-of-day audio transitions can be collapsed at high speed.
    virtual void setSpeed(SimSpeed speed) = 0;

    // Fire a one-shot stinger of the given type. Subject to the 5 s minimum
    // between same-type triggers and the drop-if-already-playing rule.
    virtual void triggerStinger(StingerType type) = 0;

    // Synchronise the OpenAL listener position and orientation to the current camera.
    // Must be called once per frame from the main thread after Irrlicht updates the camera.
    virtual void syncListenerToCamera(const CameraState& cam) = 0;

    // Activate game-over audio fade sequence (post-V1 Scenario mode only).
    // Sets m_gameOverFade = true; audio thread fades all stems over 2 s then stops them.
    virtual void setGameOverState(bool active) = 0;

    // Notify the audio system that the in-game clock has crossed a time-of-day boundary.
    // Called by CitySimulation whenever the simulated hour transitions between DAY/DUSK/NIGHT/DAWN.
    // Also called at game start to establish the initial ambient bed before the first frame.
    virtual void setTimeOfDay(TimeOfDay tod) = 0;

    // Transition from main menu audio to gameplay audio.
    // Called by UIManager when the player starts a new game or loads a saved game.
    // Must not be called while gameplay audio is already active (undefined behaviour).
    virtual void transitionToGameplay() = 0;

    // Per-frame update called from the main game loop.
    // realDeltaSeconds is wall-clock elapsed time since the previous call.
    // Responsibilities: advance occlusion raycast budget, push time-of-day transitions,
    // and forward any pending crossfade or zone-layer source updates.
    virtual void update(float realDeltaSeconds) = 0;

    // Set the master volume (applied via alListenerf(AL_GAIN) on the calling thread).
    // gain is a linear multiplier [0.0, 1.0].
    virtual void setMasterVolume(float gain) = 0;

    // Set the music volume (applied to music stream sources at next audio thread wake).
    // gain is a linear multiplier [0.0, 1.0].
    virtual void setMusicVolume(float gain) = 0;

    // Set the SFX volume (applied to SFX sources at next audio thread wake).
    // gain is a linear multiplier [0.0, 1.0].
    virtual void setSFXVolume(float gain) = 0;
};
