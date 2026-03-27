#pragma once

#include "audio_types.h"
#include "simulation_types.h"
#include <utility>

// Canonical IAudioSystem — 19 methods.
// Uses only game-domain types (SoundId, SoundHandle, MusicTrackId, StingerType,
// SimSpeed, SoundPriority, TimeOfDay, MusicIntensity, vec3, CameraState).
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

    // Transition from gameplay audio back to main menu audio.
    // Stops all active gameplay music stems and ambient beds on sources[58..61],
    // then crossfades in main menu music on sources[58..59] over 1 s (constant-power
    // curve, wall-clock time only — NOT bar-boundary synchronized).
    // Main menu music loops indefinitely via OGG EOF→seek pattern.
    // Safe to call if gameplay audio is not currently active — stops all sources[58..61]
    // unconditionally via alSourceStop (no-op for non-playing sources); no guard required.
    // Called by UIManager::transitionToMainMenu() as the FIRST operation.
    // (Phase 11m)
    virtual void transitionToMainMenu() = 0;

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

    // Set the adaptive music intensity tier driven by live simulation state.
    // Called by CitySimulation::update() whenever the city's fiscal or population
    // state changes tier. AudioSystem crossfades the active gameplay stem pair on
    // the next beat boundary to the stem pair matching the new tier.
    // Time-of-day forced-Calm override (DUSK/NIGHT/DAWN) is applied internally;
    // CitySimulation does NOT need to suppress GROWTH/CRISIS calls during off-hours.
    // Calling with the tier already active is a no-op.
    // Thread-safety: call from the main thread only.
    // Threshold conditions: see architecture/game-design/economy-model.md
    //   §Music Intensity Tiers.
    virtual void setMusicIntensity(MusicIntensity intensity) = 0;

    // -----------------------------------------------------------------------
    // Phase 11d — Vehicle engine audio pair API
    //
    // These three methods wire the traffic agent system (Deliverable 3a) to
    // the SFX source pool.  All three are main-thread entry points; AL state
    // changes are dispatched to the audio thread internally per the two-mutex
    // design (see architecture/audio-architecture/audio-system.md §Two-Mutex Design).
    //
    // Indices returned by acquireVehicleEnginePair are OPAQUE SFX pool source
    // indices — never ALuint.  IAudioSystem must not expose OpenAL types.
    // (ref: architecture/audio-architecture/dynamic-soundscape.md §Vehicle Engine Audio)
    // -----------------------------------------------------------------------

    // acquireVehicleEnginePair — reserve an idle+move source pair from the
    // vehicle engine pool for the given zone type (zone affects pitch/gain profile).
    // Returns {idleIdx, moveIdx} on success; {-1, -1} if the pool is exhausted.
    // Callers MUST check for {-1,-1} before calling updateVehicleAudio.
    virtual std::pair<int,int> acquireVehicleEnginePair(ZoneType zone) = 0;

    // releaseVehicleEnginePair — stop and return both sources to the pool.
    // Passing {-1,-1} is a safe no-op.
    virtual void releaseVehicleEnginePair(int idleIdx, int moveIdx) = 0;

    // updateVehicleAudio — push per-frame speed and world position to the
    // audio thread for AL_PITCH / AL_GAIN crossblend and AL_POSITION update.
    // Called once per active agent per render frame, after getAgentPositions()
    // and before drawScene().
    // speedFraction: 0.0 = stopped (idle source dominant), 1.0 = free-flow (move source dominant).
    // worldX / worldZ: world-space position in metres for AL_POSITION.
    virtual void updateVehicleAudio(int idleIdx, int moveIdx,
                                    float speedFraction,
                                    float worldX, float worldZ) = 0;
};
