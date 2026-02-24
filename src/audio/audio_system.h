#pragma once
#include "src/interfaces/IAudioSystem.h"
#include "src/interfaces/IClock.h"

// BEHAVIORAL CONTRACT (Phase 7 implementation required):
// AudioSystem is the concrete OpenAL Soft implementation of IAudioSystem.
// It owns the ALCdevice and ALCcontext, manages a pool of kTotalSources (62)
// AL sources, streams music and ambient beds on a dedicated audio thread, and
// handles SFX eviction, stinger cooldowns, and music crossfades at bar boundaries.
//
// Key behavioral constraints:
// - All AL calls must be wrapped in alCheckError(); all ALC calls in alcCheckError().
// - alcSetThreadContext requires ALC_EXT_thread_local_context — load via
//   alcGetProcAddress at construction; never call directly (null dereference if absent).
// - Music crossfade uses a constant-power curve queued to the next bar boundary
//   (90 BPM). Bar boundaries are tracked via m_samplesQueued (software counter) —
//   never AL_SAMPLE_OFFSET (unreliable on buffer-queue sources).
// - Crossfade timing uses real-time delta (not simulation delta). Minimum hold =
//   1 crossfade duration before a new crossfade may begin.
// - Stingers: music ducks to kMusicDuckGain (0.4) on playback; ambient beds are
//   NOT ducked. Drop if same type already in-progress. Min kStingerCooldownSeconds
//   (5 s) between triggers of same type.
// - Vehicle engine sources: cull at > kVehicleEngineCullDistance (150 m). Idle
//   and move sources must be acquired and released as an atomic pair.
// - IClock is injected at construction for deterministic timing in tests.

// SHUTDOWN CONTRACT (Phase 7 implementation required):
// All shutdown steps must occur AFTER m_audioThread.join() and BEFORE alcDestroyContext.
//
// Streaming sources (music + ambient, sources[58..61]):
//   1. alSourceStop(src)
//   2. Query AL_BUFFERS_QUEUED (never hardcode buffer count)
//   3. alSourceUnqueueBuffers(src, count, buf_array)
//   4. alDeleteBuffers(count, buf_array)
//
// SFX pool sources (sources[0..57]):
//   1. alSourceStop(src)
//   2. alSourcei(src, AL_BUFFER, 0)   — detach static buffer
//   3. alDeleteBuffers(count, buf_array)

class AudioSystem : public IAudioSystem {
public:
    explicit AudioSystem(IClock* clock) : m_clock(clock) {}
    ~AudioSystem() override = default;

    // Play a non-positional (2D) one-shot sound. priority controls SFX pool eviction.
    // gain is a linear multiplier [0.0, 1.0].
    // Returns an opaque SoundHandle that can be passed to stopSound().
    SoundHandle playSound(SoundId id, SoundPriority priority, float gain = 1.0f) override {
        (void)id; (void)priority; (void)gain;
        return 0;
    }

    // Play a world-positioned (3D) one-shot sound at pos.
    // Returns an opaque SoundHandle that can be passed to stopSound().
    SoundHandle playPositionalSound(SoundId id, vec3 pos, SoundPriority priority, float gain = 1.0f) override {
        (void)id; (void)pos; (void)priority; (void)gain;
        return 0;
    }

    // Stop a previously-started sound identified by handle.
    // Silently ignored if the handle is stale (source already finished or recycled).
    void stopSound(SoundHandle handle) override {
        (void)handle;
    }

    // Begin streaming the specified music track (with beat-boundary crossfade from current track).
    void setMusicTrack(MusicTrackId id) override {
        (void)id;
    }

    // Notify the audio system of the current simulation speed.
    void setSpeed(SimSpeed speed) override {
        (void)speed;
    }

    // Fire a one-shot stinger of the given type.
    void triggerStinger(StingerType type) override {
        (void)type;
    }

    // Synchronise the OpenAL listener position and orientation to the current camera.
    void syncListenerToCamera(const CameraState& cam) override {
        (void)cam;
    }

    // Activate game-over audio fade sequence (post-V1 Scenario mode only).
    void setGameOverState(bool active) override {
        (void)active;
    }

    // Notify the audio system that the in-game clock has crossed a time-of-day boundary.
    void setTimeOfDay(TimeOfDay tod) override {
        (void)tod;
    }

    // Transition from main menu audio to gameplay audio.
    void transitionToGameplay() override {}

    // Per-frame update called from the main game loop.
    // realDeltaSeconds is wall-clock elapsed time since the previous call.
    void update(float realDeltaSeconds) override {
        (void)realDeltaSeconds;
    }

private:
    IClock* m_clock{nullptr};
    double  m_lastDuckWakeTime{0.0};

    // FROZEN MEMBER NAMES (locked in Phase 3; do NOT rename in Phase 7 without updating this comment):
    //   m_clock, m_lastDuckWakeTime
    // NOTE: All remaining member variables (m_device, m_context, m_stopThread, m_audioThread,
    // m_useThreadLocalCtx, m_fnSetThreadCtx, m_duckState, etc.) are Phase 7 additions.
    // See architecture/audio-architecture/audio-system.md for the full canonical member list.
    // Do NOT add AL-typed members here — audio_system.h must include ZERO OpenAL headers.

    // FROZEN MEMBER NAMES for Phase 7 (commented-out; declare here to lock naming):
    // bool m_useThreadLocalCtx{false};
    // ALCcontext* m_context{nullptr};
    // using FnSetThreadCtx = int(*)(ALCcontext*);  // LOCAL alias — do NOT use PFNALCSETTHREADCONTEXTPROC
    // FnSetThreadCtx m_fnSetThreadCtx{nullptr};
    //
    // SA-3 — m_occlusionGainTarget FROZEN (locked Phase 3):
    // std::atomic<float> m_occlusionGainTarget[kEvictableSFXCount];  // Phase 7
    // MANDATORY: std::atomic<float> is REQUIRED — main thread writes, audio thread reads.
    // A plain float[] would be a C++ data race (UB). Initialize all elements to 1.0f before thread launch.
};
