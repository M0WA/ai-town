# AudioSystem (RAII)

## IAudioSystem Interface

`IAudioSystem` is the pure-virtual interface injected into `CitySimulation`, `UIManager`, and other subsystems so that unit tests can substitute `MockAudioSystem` without initialising OpenAL hardware.

**Important**: `IAudioSystem` has no OpenAL include dependencies. It uses only game-domain types (`SoundId`, `MusicTrackId`, `StingerType`, `SimSpeed`, `CameraState`, `vec3`, `TimeOfDay`, `SoundPriority`). Never expose `ALuint`, `ALfloat`, or any `AL_*` constant through this interface.

```cpp
// audio_types.h MUST #include <cstdint> — uint32_t is not guaranteed to be pulled in
// transitively on all compilers (GCC strict include order exposed this; MSVC was silently
// resolving it via other headers).  Never rely on transitive inclusion of <cstdint>.

// Forward declarations — defined in game-domain headers, not in OpenAL headers:
struct vec3;         // 3-component float vector (X, Y, Z)
struct CameraState;  // position (vec3), forward (vec3), up (vec3)
// SimSpeed is NOT a separate enum class — it is a type alias:
//   using SimSpeed = SpeedMultiplier;   (defined in simulation_types.h)
// ZoneType is also defined in simulation_types.h (enum class: Residential, Commercial, Industrial).
// IAudioSystem.h must #include "simulation_types.h" to get both SimSpeed and ZoneType.
// Do NOT forward-declare SimSpeed as "enum class SimSpeed;" — type aliases
// cannot be forward-declared in C++, and this would create a duplicate-type
// compile error when simulation_types.h is included.
enum class StingerType;  // Defined in audio_types.h; V1 values: CRISIS=55, MILESTONE=56.
                         // COUPLING NOTE: The integer values of StingerType are intentionally
                         // equal to their corresponding AL source pool indices (sources[55] and
                         // sources[56]). This coupling is known and deliberate — it avoids an
                         // indirection table while the V1 pool layout is frozen.
                         // triggerStinger(StingerType::X) is the ONLY safe API; game code must
                         // never use the numeric values (55, 56, ...) directly.
                         // Any post-V1 pool restructuring MUST update StingerType enum values
                         // simultaneously. See source-pool.md for the required static_assert
                         // compile-time guard and the 4-step post-V1 promotion sequence.
using SoundId      = uint32_t;
using MusicTrackId = uint32_t;
using SoundHandle  = uint32_t;  // opaque handle returned by playSound / playPositionalSound

// Defined in audio_types.h — reproduced here for interface completeness.
// TimeOfDay represents in-game time periods that drive ambient bed and music intensity selection.
// CitySimulation calls IAudioSystem::setTimeOfDay() whenever the in-game clock crosses a boundary.
// Values: DAY (06:00–20:00), DUSK (20:00–23:00), NIGHT (23:00–05:00), DAWN (05:00–06:00).
// See dynamic-soundscape.md for the time-of-day music intensity override rules.
enum class TimeOfDay { DAY, DUSK, NIGHT, DAWN };

// Defined in audio_types.h — reproduced here for interface completeness.
// Controls priority-based eviction in AudioSourcePool::acquireSFXSource().
// LOW and NORMAL callers may only acquire sources[0..50]; HIGH and CRITICAL may acquire sources[0..54].
// Eviction selects the lowest-priority source with the greatest distance as the tiebreak.
// See source-pool.md for the full eviction and transient-reserve rules.
enum class SoundPriority { LOW = 0, NORMAL = 1, HIGH = 2, CRITICAL = 3 };

// Defined in audio_types.h — reproduced here for interface completeness.
// Music intensity tier driven by live simulation state.  Set by CitySimulation::update()
// via IAudioSystem::setMusicIntensity().  AudioSystem maps each tier to the corresponding
// gameplay stem pair (CALM→calm_01/02, GROWTH→growth_01/02, CRISIS→crisis_01/02).
// Threshold conditions are authoritative in architecture/game-design/economy-model.md:
//   CALM:   budget_surplus_pct >= 0%  (default state)
//   GROWTH: net population change positive (population this tick > population previous tick)
//   CRISIS: consecutive_deficit_months >= 2  (highest priority tier)
// Priority order (highest first): CRISIS > GROWTH > CALM.
// Added in Phase 10 (see implementation/phase-10.md — Music intensity interface deliverable).
enum class MusicIntensity { CALM, GROWTH, CRISIS };

// IAudioSystem — 18 pure-virtual methods.
// Phase history: Phase 7 (base 14 methods) → Phase 10 (+setMusicIntensity = 15) →
// Phase 11d (+acquireVehicleEnginePair, +releaseVehicleEnginePair, +updateVehicleAudio = 18).
// Authoritative source for testability-architecture.md method-count comment.
class IAudioSystem {
public:
    virtual ~IAudioSystem() = default;

    // Play a non-positional (2D) one-shot sound.  gain is a linear multiplier [0.0, 1.0].
    // priority controls eviction behaviour in the SFX pool (see source-pool.md).
    // Returns an opaque SoundHandle that can be passed to stopSound().
    virtual SoundHandle playSound(SoundId id, SoundPriority priority, float gain = 1.0f) = 0;

    // Play a world-positioned (3D) one-shot sound at pos.
    // priority controls eviction behaviour in the SFX pool (see source-pool.md).
    // Returns an opaque SoundHandle that can be passed to stopSound().
    virtual SoundHandle playPositionalSound(SoundId id, vec3 pos, SoundPriority priority, float gain = 1.0f) = 0;

    // Stop a previously-started sound identified by handle.
    // Silently ignored if the handle is stale (source already finished or recycled).
    virtual void stopSound(SoundHandle handle) = 0;

    // Begin streaming the specified music track (with beat-boundary crossfade from the current track).
    virtual void setMusicTrack(MusicTrackId id) = 0;

    // Notify the audio system of the current simulation speed so that
    // time-of-day audio transitions can be collapsed at high speed.
    virtual void setSpeed(SimSpeed speed) = 0;

    // Fire a one-shot stinger of the given type.  Subject to the 5 s minimum
    // between same-type triggers and the drop-if-already-playing rule.
    virtual void triggerStinger(StingerType type) = 0;

    // Synchronise the OpenAL listener position and orientation to the current camera.
    // Must be called once per frame from the main thread after Irrlicht updates the camera.
    virtual void syncListenerToCamera(const CameraState& cam) = 0;

    // Signal a game-over condition to the audio system.
    //
    // V1 BEHAVIOR (Sandbox mode — no game-over condition exists):
    //   setGameOverState(true) is a NO-OP in V1. Sandbox mode has no game-over
    //   condition; Scenario mode (which introduces bankruptcy/defeat states) is
    //   post-V1. The Phase 7 AudioSystem implementation MUST implement this method
    //   as an early-return no-op in V1, emitting a log warning so the call is
    //   visible in diagnostic output:
    //
    //     void AudioSystem::setGameOverState(bool active) {
    //         // V1: no-op — Sandbox mode has no game-over condition.
    //         // Scenario mode (post-V1) will implement the full fade sequence.
    //         LOG_WARNING("setGameOverState() called in V1 Sandbox mode — no-op");
    //         return;
    //     }
    //
    //   The m_gameOverFade member and the m_gameOverFadeT progress counter
    //   (declared in the AudioSystem private section) are Phase 7 code-path stubs.
    //   In V1 they are declared but never written by any executed code path because
    //   setGameOverState() returns before reaching any fade logic. They MUST be
    //   guarded by the Scenario-mode check when that mode is added.
    //
    //   Do NOT attempt to fade music stems or ambient beds on setGameOverState(true)
    //   in V1. Any stem fade in V1 would be unreachable dead code and risks
    //   introducing audio-thread race conditions around m_gameOverFade that are
    //   invisible during V1 testing (Sandbox playthroughs never trigger game-over).
    //
    // POST-V1 BEHAVIOR (Scenario mode — when added):
    //   setGameOverState(true) triggers the game-over stinger at sources[57]
    //   (StingerType::GAME_OVER, added in the post-V1 promotion sequence described
    //   in source-pool.md) and then fades all active music stems to silence over
    //   2 seconds via the m_gameOverFade / m_gameOverFadeT mechanism on the audio
    //   thread. Ambient beds are NOT faded by this call — only music stems are
    //   affected. setGameOverState(false) resets m_gameOverFade and m_gameOverFadeT
    //   to restore normal music playback (used when the player restarts from a
    //   game-over state without returning to the main menu).
    virtual void setGameOverState(bool active) = 0;

    // Notify the audio system that the in-game clock has crossed a time-of-day boundary.
    // Called by CitySimulation whenever the simulated hour transitions between DAY/DUSK/NIGHT/DAWN.
    // The audio system re-evaluates the active ambient bed and applies forced-Calm music overrides
    // where applicable (see dynamic-soundscape.md — Time-of-Day Music Intensity Override).
    // Also called at game start to establish the initial ambient bed before the first frame.
    virtual void setTimeOfDay(TimeOfDay tod) = 0;

    // Transition from main menu audio to gameplay audio.
    // Called by UIManager when the player starts a new game or loads a saved game.
    // Crossfades main menu music out over 1 s (constant-power curve) on sources[58..59]
    // using m_clock for real-time crossfade duration measurement (not simulation time),
    // then hands those sources to the gameplay music system to begin the first gameplay stem.
    // Also starts the ambient bed layer (sources[60..61]) for the current time-of-day period.
    // Must not be called while gameplay audio is already active (undefined behaviour).
    //
    // Precondition: setTimeOfDay() must be called at least once before transitionToGameplay()
    // is invoked (typically by CitySimulation::start() during the new-game initialization
    // sequence). transitionToGameplay() reads m_currentTimeOfDay to determine which ambient
    // bed to start on sources[60..61]. If transitionToGameplay() is called before any
    // setTimeOfDay() call, the zero-initialized enum value (TimeOfDay::DAY) is used
    // unconditionally — this is acceptable behavior but must be documented. The call sequence
    // in UIManager::transitionToGameplay() MUST be:
    //   (1) m_sim->start()  [which calls m_audio->setTimeOfDay(tod) internally]
    //   (2) m_audio->transitionToGameplay()
    virtual void transitionToGameplay() = 0;

    // Per-frame update called from the main game loop.
    // realDeltaSeconds is wall-clock elapsed time since the previous call.
    // Responsibilities: advance occlusion raycast budget, push time-of-day transitions,
    // and forward any pending crossfade or zone-layer source updates.
    virtual void update(float realDeltaSeconds) = 0;

    // --- Phase 8 Volume Control API ---
    // These three methods are declared here so that UIManager (Settings > Audio sliders)
    // can call them via IAudioSystem* without knowing the concrete AudioSystem type.
    // Phase 8 adds these three methods to IAudioSystem as pure-virtual members, implements
    // them in AudioSystem with the correct member declarations and thread-safety semantics
    // (std::atomic<float> for m_musicVolume and m_sfxVolume), adds them to MockAudioSystem,
    // and adds three SettingsPanel unit tests. Phase 9 does not need to add or modify these
    // methods.
    // All gain values are linear multipliers in the range [0.0, 1.0].
    // Default values: master = 1.0, music = 0.8, SFX = 0.8 (see settings-pause-menu.md).
    // Values are persisted in the settings/config file (separate from save game files)
    // and restored on the next session load.
    virtual void setMasterVolume(float gain) = 0;
    virtual void setMusicVolume(float gain) = 0;
    virtual void setSFXVolume(float gain) = 0;

    // --- Phase 10 Adaptive Music API ---
    // Set the music intensity tier driven by live simulation state.
    // Called by CitySimulation::update() whenever the city's fiscal or population state
    // changes tier.  AudioSystem crossfades the active gameplay stem pair on the next
    // beat boundary to the stem pair matching the new tier
    // (CALM→calm_01/02, GROWTH→growth_01/02, CRISIS→crisis_01/02).
    // Time-of-day forced-Calm override (DUSK/NIGHT/DAWN) takes precedence internally;
    // CitySimulation does NOT need to suppress GROWTH calls during off-hours.
    // Calling setMusicIntensity() with the tier already active is a no-op.
    // Thread-safety: call from the main thread only.
    // AudioSystem implementation MUST store m_currentMusicIntensity as std::atomic<int>
    // (or read under the audio-thread lock) because the audio thread reads it to select
    // the next crossfade target.
    // Threshold conditions that determine which tier to pass are defined in
    // architecture/game-design/economy-model.md §Music Intensity Tiers.
    // MockAudioSystem: add MOCK_METHOD(void, setMusicIntensity, (MusicIntensity), (override));
    virtual void setMusicIntensity(MusicIntensity intensity) = 0;

    // --- Phase 11d Vehicle Engine Pair API ---
    // Acquire an idle+move source pair from the vehicle engine pool for a vehicle agent
    // of the given zone type.  The pair is drawn from the 24-slot Traffic/Vehicle SFX
    // budget (sources[0..50], NORMAL priority).  At most kMaxVehiclePairs (= 12) pairs
    // may be active simultaneously.
    //
    // Returns: {idleIdx, moveIdx} — indices into the internal AL source pool to be used
    //   for sfx_vehicle_engine_idle (idle source) and sfx_vehicle_engine_move (move source).
    //   Returns {-1, -1} if the pool is exhausted (all 12 pairs in use) and no eviction
    //   candidate with lower priority/greater distance exists.
    //
    // Callers MUST check the return value before using either index:
    //   auto [idle, move] = m_audio->acquireVehicleEnginePair(zone);
    //   if (idle == -1) { /* pool full — vehicle drives silently */ return; }
    //
    // Paired acquisition is atomic: either both sources are acquired or neither is
    // (partial acquisition is prohibited — see source-pool.md §Paired acquisition).
    //
    // ZoneType determines base pitch multiplier applied to both idle and move sources:
    //   ZoneType::Residential → 1.0  (car engine)
    //   ZoneType::Commercial  → 0.85 (bus engine)
    //   ZoneType::Industrial  → 0.85 (truck engine)
    // This mapping is implemented in V1 and is not reserved for future use.
    //
    // See architecture/audio-architecture/source-pool.md §Vehicle Engine Source Constraints
    // and §Vehicle Engine Source Pairing — Internal Tracking for the full pool contract.
    //
    // MockAudioSystem: add
    //   MOCK_METHOD((std::pair<int,int>), acquireVehicleEnginePair, (ZoneType), (override));
    virtual std::pair<int,int> acquireVehicleEnginePair(ZoneType zone) = 0;

    // Return the idle+move source pair to the vehicle engine pool.
    // idleIdx and moveIdx must be the values originally returned by acquireVehicleEnginePair().
    // Both sources are stopped (alSourceStop) and their occlusion state is reset before
    // the pool slots are marked free.
    //
    // Releasing only one source of a pair is prohibited.  The LOD-cull path must always
    // call releaseVehicleEnginePair(idleIdx, moveIdx) — never release individual sources
    // via any other pool release path.
    //
    // Passing {-1, -1} (a failed acquisition result) is a no-op and is safe to call.
    //
    // See architecture/audio-architecture/source-pool.md §releaseVehicleEnginePair.
    //
    // MockAudioSystem: add
    //   MOCK_METHOD(void, releaseVehicleEnginePair, (int, int), (override));
    virtual void releaseVehicleEnginePair(int idleIdx, int moveIdx) = 0;

    // Per-frame vehicle engine audio state update.
    // Called by main.cpp once per render frame for each active vehicle, inside the
    // per-frame agent sync loop, AFTER calling getAgentPositions() and BEFORE drawScene().
    // AudioSystem uses these values to:
    //   - Set AL_PITCH on both sources: basePitch × lerp(0.75, 1.35, speedFraction),
    //     where basePitch is 1.0 (car) or 0.85 (bus/truck — determined from zone type at
    //     acquire time and stored internally by AudioSystem).
    //   - Set AL_GAIN crossblend: idle gain = max(0, 1 − (speedFraction − 0.21) / 0.36);
    //     move gain = 1 − idle gain (see dynamic-soundscape.md §Vehicle Engine Audio).
    //     Derivation: speedFraction = currentSpeed / 13.9 m/s (max road speed per traffic-system.md);
    //     0.21 ≈ 3/13.9 (idle-full-on threshold); 0.36 ≈ (8−3)/13.9 (ramp width).
    //   - Set AL_POSITION on both sources to (worldX, 0.0f, worldZ) for 3D spatial rolloff.
    // idleIdx / moveIdx must be the values returned by acquireVehicleEnginePair(); passing
    // {-1, -1} (a failed acquisition) is a no-op.
    //
    // MockAudioSystem: add
    //   MOCK_METHOD(void, updateVehicleAudio, (int, int, float, float, float), (override));
    //
    // Threading model: updateVehicleAudio() is called on the MAIN THREAD every frame.
    // The implementation stores speedFraction, worldX, and worldZ into the per-slot
    // std::atomic<float> fields of VehiclePairSlot (source-pool.md §Vehicle Engine Source
    // Pairing — Internal Tracking).  The audio thread reads these atomics on each wake
    // (same pattern as m_occlusionGainTarget) and applies the AL_PITCH, AL_GAIN, and
    // AL_POSITION calls.  No mutex is required for the main-thread write; std::atomic
    // provides the necessary memory ordering.  AL calls are NEVER made on the main thread
    // from this method.
    virtual void updateVehicleAudio(int idleIdx, int moveIdx,
                                    float speedFraction,
                                    float worldX, float worldZ) = 0;
};
```

`MockAudioSystem` in `tests/simulation/mock_audio_system.h` provides GMock implementations of all eighteen methods above (using `MOCK_METHOD` macros). Test files that need audio isolation include `mock_audio_system.h` and inject `MockAudioSystem` via the `IAudioSystem*` constructor parameter of `CitySimulation`.

**MockAudioSystem atomicity rule**: `MockAudioSystem` must declare all 18 `MOCK_METHOD` entries in exact sync with the `IAudioSystem` interface. Any commit that adds or removes a method on `IAudioSystem` must update `MockAudioSystem` in the same commit. Failure to do so causes `simulation_tests` and `ui_tests` targets to fail to compile (pure-virtual override missing). This constraint is especially critical before Phase 11d test authoring begins, when all three vehicle-audio methods (`acquireVehicleEnginePair`, `releaseVehicleEnginePair`, `updateVehicleAudio`) must already be present in `MockAudioSystem`.

---

```cpp
class AudioSystem : public IAudioSystem {
public:
    explicit AudioSystem(IClock* clock);   // alcOpenDevice + alcCreateContext (with HRTF attrs).
                                  // alcOpenDevice failure: logs warning, sets m_deviceLost=true, returns early
                                  // (silent mode — all IAudioSystem calls become no-ops). Does NOT throw.
                                  // alcCreateContext / alcMakeContextCurrent / ALC_EXT_thread_local_context
                                  // failures still throw std::runtime_error.
                                  // clock: injectable for deterministic timing in tests (crossfade duck timer,
                                  // m_lastDuckWakeTime). Production passes WallClock; tests pass ManualClock.
    ~AudioSystem();  // MUST follow the audio thread shutdown sequence below
    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;
private:
    // In `src/audio/audio_system.h`, `ALCdevice*` and `ALCcontext*` MUST be forward-declared
    // using the OpenAL Soft internal typedef pattern rather than including `<AL/alc.h>`:
    //
    //   // Forward declarations — do NOT include <AL/alc.h> in audio_system.h
    //   struct ALCdevice_struct;
    //   using ALCdevice = ALCdevice_struct;
    //   struct ALCcontext_struct;
    //   using ALCcontext = ALCcontext_struct;
    //
    // The implementation file `audio_system.cpp` includes `<AL/alc.h>` normally.
    // This pattern allows pointer-type member declarations in the header without
    // pulling in OpenAL Soft headers. `audio_system.h` MUST include zero OpenAL
    // headers so that unit tests remain fully headless (no AL hardware required to
    // compile or link test binaries that include this header).
    ALCdevice*                m_device{nullptr};
    ALCcontext*               m_context{nullptr};
    std::atomic<bool>         m_stopThread{false};
    std::thread               m_audioThread;
    std::mutex                m_streamMutex;       // Crossfade command queue mutex — see "Two-Mutex Design" section below
    std::mutex                m_occlusionMutex;   // Protects EFX filter writes in onSourceRecycled() and updateOcclusion() — see audio-occlusion.md
    std::condition_variable   m_streamCV;
    // Audio thread init synchronization (constructor waits on m_initCV before returning):
    std::mutex                m_initMutex;
    std::condition_variable   m_initCV;
    bool                      m_initError{false};
    bool                      m_initDone{false};
    // EFX state:
    bool                      m_efxAvailable{false};         // true only if ALL kEvictableSFXCount filters were fully allocated; guards runtime occlusion paths
    bool                      m_efxAllocationAttempted{false}; // true if filter allocation loop was entered; guards shutdown cleanup loop to avoid leaking partial allocations
    ALuint                    m_occlusionFilter[kEvictableSFXCount]{};  // Stinger and stream sources are not positional; no occlusion filter needed
    // EFX function pointers — stored as members so audio thread can use them without re-querying:
    LPALGENFILTERS            m_fnGenFilters{nullptr};
    LPALFILTERI               m_fnFilteri{nullptr};
    LPALFILTERF               m_fnFilterf{nullptr};
    LPALDELETEFILTERS         m_fnDeleteFilters{nullptr};
    // Thread-local context:
    // **Phase 3 stub**: only `IClock* m_clock` is declared in the Phase 3 stub header.
    // All other private members listed in this section are Phase 7 additions.
    // The SHUTDOWN CONTRACT comment in the Phase 3 stub header references
    // `m_useThreadLocalCtx` by name — this member name is therefore frozen as of
    // Phase 3 and MUST NOT be renamed in Phase 7 without updating the Phase 3 stub
    // comment simultaneously.
    bool                      m_useThreadLocalCtx{false};
    // FnSetThreadCtx is defined as a local function-pointer alias in audio_system.h —
    // NOT as PFNALCSETTHREADCONTEXTPROC from <AL/alext.h> — to preserve the zero-AL-includes
    // contract of the header.  The alias matches the alcSetThreadContext signature exactly:
    //
    //   // In audio_system.h (no AL header included):
    //   using FnSetThreadCtx = int(*)(ALCcontext*);
    //   // ALCcontext is forward-declared above via:
    //   //   struct ALCcontext_struct; using ALCcontext = ALCcontext_struct;
    //
    // In audio_system.cpp the address is loaded and cast to this type:
    //   m_fnSetThreadCtx = reinterpret_cast<FnSetThreadCtx>(
    //       alcGetProcAddress(m_device, "alcSetThreadContext"));
    //
    // Never write PFNALCSETTHREADCONTEXTPROC in audio_system.h — that type requires
    // <AL/alext.h>, which would break headless test compilation.
    FnSetThreadCtx            m_fnSetThreadCtx{nullptr};
    // Volume control — cross-thread members (written by main thread, read by audio thread):
    // m_musicVolume and m_sfxVolume use std::atomic<float> because setMusicVolume() /
    // setSFXVolume() are called from the main thread while the audio thread reads them
    // during per-frame source gain updates — a plain float would be a C++ data race (UB).
    // m_masterVolume uses plain float because setMasterVolume() calls
    // alListenerf(AL_GAIN, gain) directly on the calling (main) thread and the raw value
    // is never stored for later audio-thread reads; no cross-thread access occurs.
    std::atomic<float>        m_musicVolume{0.8f};   // music source gain — written by main thread via setMusicVolume(), read by audio thread during source gain updates
    std::atomic<float>        m_sfxVolume{0.8f};     // SFX source gain — written by main thread via setSFXVolume(), read by audio thread during source gain updates
    float                     m_masterVolume{1.0f};  // master AL listener gain — applied immediately by setMasterVolume() via alListenerf(AL_GAIN, gain); no cross-thread read
    // Music ducking state machine (audio thread only for gain writes; main thread reads atomically):
    enum class DuckState { IDLE, DUCKING, DUCKED, RELEASING };
    std::atomic<DuckState>    m_duckState{DuckState::IDLE};
    std::atomic<float>        m_musicDuckGain{1.0f};
    float                     m_duckTimer{0.0f};    // seconds elapsed in current duck phase (audio thread only)
    float                     m_duckStartGain{1.0f}; // gain at transition INTO DUCKING state; enables correct ramp from current gain (not 1.0) on RELEASING→DUCKING re-entry (audio thread only)
    IClock*                   m_clock{nullptr};         // injectable clock for deterministic timing (crossfade duck timer, m_lastDuckWakeTime);
                                                       // production: WallClock; tests: ManualClock
    double                    m_lastDuckWakeTime{0.0}; // wall-clock timestamp (seconds) of the previous audio thread wake;
                                                       // initialized to m_clock->nowSeconds() AFTER alcSetThreadContext succeeds
                                                       // and BEFORE the first condition_variable::wait_for; see dynamic-soundscape.md
    // Occlusion gain tracking per evictable SFX source (audio-occlusion.md):
    float                     m_occlusionGainCurrent[kEvictableSFXCount]{};  // current applied occlusion gain per source (audio thread only)
    std::atomic<float>        m_occlusionGainTarget[kEvictableSFXCount];     // target occlusion gain: written by main-thread occlusion raycast pass each frame,
                                                                              // read by audio thread in updateOcclusion() every 10 ms.
                                                                              // std::atomic<float> is mandatory — concurrent read+write to a plain float[] is a C++ data race (UB).
                                                                              // MANDATORY: Initialize all elements to 1.0f in the AudioSystem constructor body
                                                                              // BEFORE launching m_audioThread. The initialization loop must precede the
                                                                              // std::thread(...) constructor call — the audio thread reads m_occlusionGainTarget
                                                                              // on its very first updateOcclusion() call (10 ms after thread start). If the thread
                                                                              // launches before initialization completes, the audio thread reads uninitialized
                                                                              // std::atomic<float> values, which is undefined behavior.
                                                                              // Required initialization (in AudioSystem constructor, before thread launch):
                                                                              //   for (auto& t : m_occlusionGainTarget) t.store(1.0f, std::memory_order_relaxed);
                                                                              //   m_audioThread = std::thread(&AudioSystem::audioThreadFunc, this);  // launch AFTER init
    // Music crossfade progress (separate from ambient bed crossfade):
    std::atomic<float>        m_musicCrossfadeT{0.0f};  // 0→1 over crossfade duration; music stems
    std::atomic<float>        m_ambientCrossfadeT{0.0f}; // 0→1 over crossfade duration; ambient beds
    // Game-over fade state (post-V1, Scenario mode only):
    bool                      m_gameOverFade{false};    // set to true by setGameOverState(); triggers 2 s stem fade on audio thread
    float                     m_gameOverFadeT{0.0f};   // seconds elapsed in game-over fade (0.0→2.0); advanced by audio thread dt each wake; used to compute per-stem gain during fade
};
```

## IAudioSystem Header Include Requirements

The file `src/interfaces/IAudioSystem.h` must include the following headers:

```cpp
#include "simulation_types.h"    // Required: SimSpeed (type alias for SpeedMultiplier), ZoneType (enum class; Phase 11d)
#include "audio_types.h"         // Required: SoundId, MusicTrackId, SoundPriority, StingerType, TimeOfDay, SoundHandle, MusicIntensity
#include "camera_state.h"        // Required: CameraState (used in syncListenerToCamera)
#include "vec3.h"                // Required: vec3 (used in playPositionalSound and syncListenerToCamera)
```

**Critical dependency**: `SimSpeed` is a type alias (`using SimSpeed = SpeedMultiplier`) defined in `simulation_types.h`. IAudioSystem method signatures (line 74: `virtual void setSpeed(SimSpeed speed)`) use `SimSpeed` directly. **Forward-declaring `SimSpeed` as `enum class SimSpeed;` is prohibited** — type aliases cannot be forward-declared in C++, and attempting this will produce a duplicate-type compile error when `simulation_types.h` is included. IAudioSystem must include the full `simulation_types.h` header, not a forward declaration.

---

- Owned by the application root; no other subsystem creates AL contexts
- The `AudioSystem` constructor must launch `m_audioThread` and then wait on `m_initCV` (with a timeout of 5 seconds) before returning, so that thread-local context initialization failures are surfaced before the first audio call. The constructor wait must hold `m_initMutex` via `std::unique_lock` and use the predicate form of `wait_for` (calling `wait_for` without holding the mutex is undefined behavior):

  ```cpp
  {
      std::unique_lock<std::mutex> lk(m_initMutex);
      bool notified = m_initCV.wait_for(lk, std::chrono::seconds(5),
          [this]{ return m_initDone; });
      if (!notified || m_initError) throw std::runtime_error("AudioSystem init failed");
  }
  ```

  The audio thread signals completion (success or failure) via `m_initDone`. The **success path** (after `alcSetThreadContext` succeeds) must initialize `m_lastDuckWakeTime` BEFORE calling `notify_one()`, then signal before entering the streaming loop. Initializing after `notify_one()` is a data race: the constructor unblocks immediately on `notify_one()` and the main thread may call `triggerStinger()` before `m_lastDuckWakeTime` is written, producing an epoch-sized `dt` on the first audio thread wake:

  ```cpp
  // Audio thread success path (after alcSetThreadContext succeeds):
  // IMPORTANT: initialize m_lastDuckWakeTime BEFORE notify_one() — see dynamic-soundscape.md
  m_lastDuckWakeTime = m_clock->nowSeconds();
  { std::lock_guard<std::mutex> lk(m_initMutex); m_initDone = true; }
  m_initCV.notify_one();
  // Now enter streaming loop...
  ```

  The **error path** must also set `m_initDone` so the constructor does not hang until timeout:

  ```cpp
  { std::lock_guard<std::mutex> lk(m_initMutex); m_initError = true; m_initDone = true; }
  m_initCV.notify_one();
  return;
  ```

### ALC_EXT_thread_local_context Requirement

**HARD REQUIREMENT**: If `ALC_EXT_thread_local_context` is absent at `AudioSystem` construction, the constructor MUST throw `std::runtime_error` (or equivalent non-recoverable failure). There is NO fallback that replaces the thread-local binding on the audio thread — the `alcSetThreadContext` extension is mandatory for safe multi-threaded AL call dispatch. The check sequence:

1. Load extension via `alcGetProcAddress(m_device, "alcSetThreadContext")` — store as `m_fnSetThreadCtx`.
2. If `m_fnSetThreadCtx == nullptr` (extension absent): throw `std::runtime_error("ALC_EXT_thread_local_context required")`.
3. Only proceed to audio thread launch if extension is confirmed present. (`alGenSources` and the EFX filter allocation loop run in Steps 1.5 and 1.6 of the constructor sequence below, on the main thread after `alcMakeContextCurrent` succeeds and before the extension check — see "Constructor sequence" below for the full ordering.)

This must be a Phase 3 locked behavioral contract — Phase 7 implementation MUST NOT deviate from this by using any fallback path.

**Main-thread `alcMakeContextCurrent` is mandatory and permanent**: Step 1 of the constructor sequence calls `alcMakeContextCurrent(m_context)` to establish the process-wide current context. This call is **mandatory and must remain bound for the entire application lifetime** (until the destructor teardown sequence). `syncListenerToCamera()` is called from the main thread every frame; it issues AL calls (`alListener3f`, `alListenerfv`) that require a current context on the calling thread. The process-wide binding from `alcMakeContextCurrent` satisfies this requirement for the main thread.

The audio thread then **additionally** calls `alcSetThreadContext(m_context)` via `m_fnSetThreadCtx` at thread startup. This establishes a *thread-local* context binding for the audio thread only, layered on top of the process-wide binding without displacing it. The main thread retains its process-wide context through the lifetime of the audio thread. Both bindings coexist: the main thread uses the process-wide binding (established by `alcMakeContextCurrent`); the audio thread uses its thread-local binding (established by `alcSetThreadContext`). Neither displaces the other.

**Why no audio-thread fallback is permitted**: The audio thread cannot safely call AL functions by relying on the process-wide `alcMakeContextCurrent` binding while the main thread also issues AL calls. The OpenAL specification requires that each thread making AL calls must have a current context on *that thread*. Without the thread-local extension, the audio thread has no mechanism to claim a per-thread context, making concurrent AL calls from both threads a data race. Failing hard at construction is the only correct behaviour.

**What "no fallback" means in practice**: Phase 7 MUST NOT introduce a code path where `alcSetThreadContext` is absent and the audio thread instead relies on the process-wide `alcMakeContextCurrent` binding established in Step 1 to make its AL calls. That specific fallback is the prohibited pattern — it is not safe when the main thread also makes AL calls. The Step 1 `alcMakeContextCurrent` call itself is never the fallback; it is a separate, permanent, unconditional requirement.

**Constructor sequence (within `AudioSystem::AudioSystem(IClock*)`):**

```cpp
// Step 1: open device and create context.
// alcMakeContextCurrent(m_context) establishes the MANDATORY, PERMANENT process-wide
// context binding for the main thread. This binding must remain active for the entire
// application lifetime — syncListenerToCamera() issues AL listener calls on the main
// thread every frame and requires a current context on that thread.
// The audio thread will ADDITIONALLY call alcSetThreadContext(m_context) for its own
// thread-local binding; this does not displace the process-wide binding.
m_device = alcOpenDevice(nullptr);
if (!m_device) {
    // No audio device — degrade to silent mode rather than aborting the game.
    // m_deviceLost=true causes all IAudioSystem methods to return early (no AL calls).
    // All partial-construction guards (m_deviceCreated, m_contextCreated, etc.) remain
    // false; the destructor skips all AL/ALC cleanup safely. The audio thread is never
    // launched; m_audioThread remains default-constructed (not joinable).
    logWarning("alcOpenDevice failed — no audio device available; running in silent mode");
    m_deviceLost.store(true);
    return;
}
// ... build HRTF attrs array (see hrtf-initialization.md) ...
m_context = alcCreateContext(m_device, attribs);
if (!m_context || alcMakeContextCurrent(m_context) == ALC_FALSE)
    throw std::runtime_error("alcCreateContext failed");

// Step 1.5: generate all AL source handles on the main thread (BEFORE EFX setup).
// alcMakeContextCurrent(m_context) succeeded in Step 1, so this thread has a valid
// current context for AL calls. m_sources[i] must be valid before the EFX filter
// allocation loop (which calls alSourcei to bind each filter to its source handle).
// See audio-occlusion.md — "alGenSources placement requirement" — for full rationale.
alGenSources(kTotalSources, m_sources);
alCheckError("alGenSources");

// Step 1.6: enqueue pre-load commands for all "Looping game SFX" tier assets.
// These commands will be drained by the audio thread BEFORE it signals m_initDone,
// ensuring all pre-loaded OGG buffers are resident when the constructor returns.
// No lock needed: the audio thread has not been launched yet; std::thread constructor
// acts as a happens-before barrier for all writes performed in this constructor.
// See streaming-architecture.md — "PRE-LOAD PHASE" — for the drain protocol.
for (const auto& asset : kPreloadManifest) {
    m_preloadQueue.push(PreloadCommand{ asset.soundId, asset.filePath, asset.looping });
}

// Step 2: load ALC_EXT_thread_local_context — HARD REQUIREMENT
// Cast to FnSetThreadCtx (defined in header as int(*)(ALCcontext*)) — NOT to
// PFNALCSETTHREADCONTEXTPROC, which requires <AL/alext.h> and would break the
// zero-AL-includes contract.  The cast is performed here in the .cpp file where
// <AL/alc.h> and <AL/alext.h> may be included freely.
m_fnSetThreadCtx = reinterpret_cast<FnSetThreadCtx>(
    alcGetProcAddress(m_device, "alcSetThreadContext"));
if (!m_fnSetThreadCtx)
    throw std::runtime_error("ALC_EXT_thread_local_context required");

// Step 2.5: EFX extension check, function pointer loading, and per-source filter allocation.
// All three operations MUST run on the main thread BEFORE thread launch so that the
// EFX function pointers and filter objects are fully written before the audio thread
// reads them (std::thread constructor is the happens-before barrier).
//
// Sequence:
//   a) alcIsExtensionPresent(m_device, "ALC_EXT_EFX") — check extension presence.
//   b) Load m_fnGenFilters, m_fnFilteri, m_fnFilterf, m_fnDeleteFilters via alGetProcAddress.
//   c) Run the m_efxAllocationAttempted / per-source filter allocation loop (kEvictableSFXCount
//      iterations), binding each filter to its corresponding source handle from m_sources[].
//
// See audio-occlusion.md — "Per-source EFX filter allocation" — for the full loop body,
// including the AL_FILTER_NULL failure check, partial-allocation cleanup pattern, and
// the two-boolean flag protocol (m_efxAllocationAttempted vs m_efxAvailable).
// This cross-reference mirrors the pattern of Step 1.5 above, which delegates
// alGenSources placement rationale to audio-occlusion.md.

// Step 3: initialize m_occlusionGainTarget[] before thread launch (see member comment)
// IMPORTANT: alcMakeContextCurrent(nullptr) MUST NOT be called during application runtime
// (i.e., between this constructor call and the destructor teardown sequence). The audio
// thread uses alcSetThreadContext() for its thread-local context binding; this does not
// affect the process-wide main-thread context.
//
// Scope clarification — destructor teardown IS permitted and mandatory:
// The prohibition applies ONLY to runtime calls while the game loop is executing and
// syncListenerToCamera() is being called from the main thread. The destructor teardown
// sequence (audio-thread-shutdown.md) makes TWO alcMakeContextCurrent calls that are
// both correct and mandatory:
//   - Step 3.5: alcMakeContextCurrent(m_context)  — re-binds context to main thread
//               BEFORE AL resource cleanup (required because thread-local context no
//               longer applies after the audio thread has joined).
//   - Step 6:   alcMakeContextCurrent(nullptr)     — clears the context as part of
//               ordered AL/ALC teardown.
// Phase 7 MUST NOT omit the Step 3.5 re-bind under the mistaken belief that it
// violates this runtime prohibition. Omitting Step 3.5 leaves the main thread with no
// current context, making all subsequent AL resource deletion calls (steps 4–5) operate
// on a null context — this is undefined behavior and will silently corrupt or crash.
for (auto& t : m_occlusionGainTarget) t.store(1.0f, std::memory_order_relaxed);

// Step 4: launch audio thread — only reached if extension is confirmed present
m_useThreadLocalCtx = true;
m_audioThread = std::thread(&AudioSystem::audioThreadFunc, this);

// Step 5: wait for audio thread to signal init complete (5 s timeout)
{
    std::unique_lock<std::mutex> lk(m_initMutex);
    bool notified = m_initCV.wait_for(lk, std::chrono::seconds(5),
        [this]{ return m_initDone; });
    if (!notified || m_initError) throw std::runtime_error("AudioSystem init failed");
}
```

---

## Two-Mutex Design

`AudioSystem` uses exactly two mutexes for its thread-safety model. This design is intentional and complete for V1.

### `m_streamMutex` — Crossfade Command Queue Mutex

`m_streamMutex` is the **crossfade command queue mutex**. It protects the queue of pending crossfade commands that the main thread enqueues and the audio thread dequeues. This is the ONLY mutex used for crossfade coordination — there is no separate crossfade mutex. Phase 7 MUST NOT introduce a third mutex for crossfade commands; use `m_streamMutex` exclusively. The two-mutex design (`m_streamMutex` for stream/crossfade commands, `m_occlusionMutex` for per-source GAINHF writes) is intentional and complete for V1.

`m_streamCV` is paired with `m_streamMutex` and is used to wake the audio thread when new crossfade or stream commands are enqueued.

### `m_occlusionMutex` — EFX Filter Write Mutex

`m_occlusionMutex` protects EFX lowpass filter parameter writes (`alFilterf` calls that set `AL_LOWPASS_GAINHF`) in `onSourceRecycled()` and `updateOcclusion()`. It is separate from `m_streamMutex` to avoid blocking crossfade command delivery while occlusion updates are in progress. See `audio-occlusion.md` for the full occlusion filter protocol.

### Prohibited Additions

Do NOT add a third mutex for any V1 audio subsystem. Specifically:

- No separate `m_crossfadeMutex` — crossfade commands use `m_streamMutex`.
- No separate `m_duckMutex` — music duck state uses `std::atomic<DuckState> m_duckState` and `std::atomic<float> m_musicDuckGain` (lock-free).
- No separate `m_stingerMutex` — stinger commands are enqueued through `m_streamMutex` along with crossfade commands.

The `m_initMutex` / `m_initCV` pair is a construction-time synchronization mechanism only. It is not a runtime operational mutex and does not count against the two-mutex runtime design. It is unused after `AudioSystem` construction completes.

---

## Zone Loop Asset Validation

`AudioSystem::loadSound()` MUST enforce an authored hard cap on zone loop asset duration at load time.

### Enforcement Point

**This cap applies ONLY when `loadSound()` is called with a `SoundId` in the range [17, 19]. All other SoundIds are exempt from this duration check.**

The check applies when `loadSound()` is called with a `SoundId` in the zone loop range:

- `sfx_zone_residential` (ID 17)
- `sfx_zone_commercial` (ID 18)
- `sfx_zone_industrial` (ID 19)

These IDs are defined in `v1-audio-asset-manifest.md` (the Zone Ambient Loops section). The range is IDs 17–19 inclusive.

### Duration Check

After decoding the OGG file header to determine the clip duration, `loadSound()` MUST apply the following guard:

```cpp
if (decodedDurationSeconds > kZoneLoopMaxPreloadDurationSeconds) {
    LOG_ERROR("Zone loop asset exceeds max pre-load duration ({}s > {}s): {}",
              decodedDurationSeconds, kZoneLoopMaxPreloadDurationSeconds, path);
    return kInvalidSoundHandle;  // do NOT load the asset
}
```

`kZoneLoopMaxPreloadDurationSeconds` is declared in `audio_types.h`:

```cpp
constexpr float kZoneLoopMaxPreloadDurationSeconds = 18.0f;
```

The function returns a null/invalid handle and logs an error. The asset is NOT partially loaded.

### Threshold Distinction

The pre-load tier boundary used when classifying sounds into the pre-load vs. streaming partition is 20 s (sounds longer than 20 s are streamed, not pre-loaded). The authored hard cap enforced at load time for zone loops is **18 s** (`kZoneLoopMaxPreloadDurationSeconds`), providing a 2 s guard band below the tier boundary. The load-time check uses 18 s, not 20 s.

### Cross-References

- `constexpr float kZoneLoopMaxPreloadDurationSeconds = 18.0f;` declared in `audio_types.h`
- Zone loop SoundId assignments (IDs 17–19): `architecture/audio-architecture/v1-audio-asset-manifest.md`
- Pre-load vs. streaming tier boundary (20 s): `architecture/audio-architecture/streaming-architecture.md`

---

## Linux SIGKILL Prevention — rtkit and PipeWire

On Linux, two separate mechanisms can deliver an **uncatchable SIGKILL** to the process
during heavy road/zone placement operations (terrain flushes, mesh rebuilds):

### 1. rtkit RLIMIT_RTTIME (primary)

When OpenAL Soft contacts **rtkit-daemon** via the system D-Bus to request `SCHED_RR`
scheduling for its mixing thread, rtkit also calls `setrlimit(RLIMIT_RTTIME, 200ms)`,
which applies **process-wide** (all threads). If the mixing thread then accumulates
more than 200 ms of continuous RT CPU time without blocking — for example, catching up
on a large backlog of samples after a long frame — the kernel delivers SIGKILL
unconditionally (no signal handler fires).

**Fix**: `rt-prio = 0` in the `[general]` section of the ALSOFT config disables rtkit
integration entirely. The mixing thread runs as `SCHED_OTHER`; no `RLIMIT_RTTIME` is
set on any thread.

**Verified by**: monitoring `/proc/<pid>/task/*/limits` — with the fix all threads show
`Max realtime timeout: unlimited`; without it the mixing thread shows `SCHED_RR` and
all threads show `Max realtime timeout: 200000 µs`.

### 2. PipeWire ALSA plugin kill() (secondary)

The PipeWire ALSA plugin (`libasound_module_pcm_pipewire.so`) calls
`kill(getpid(), SIGKILL)` when its stream is destroyed after repeated underruns.
The PulseAudio ALSA plugin (`libasound_module_pcm_pulse.so`) does not have this
behaviour.

**Fix**: `device = pulse` in the `[alsa]` section routes OpenAL through the PulseAudio
ALSA plugin instead of PipeWire's native ALSA plugin.

### ALSOFT Config Written by AudioSystem

`AudioSystem::AudioSystem()` writes `/tmp/aitown_alsoft.conf` (if `ALSOFT_CONF` is not
already set) with both fixes before the first `alcOpenDevice()` call:

```ini
[general]
rt-prio = 0
period_size = 4096
periods = 8

[alsa]
device = pulse
```

`period_size=4096, periods=8` gives a ~744 ms buffer at 44100 Hz as additional headroom
against any remaining frame spikes. `ALSOFT_CONF` is set with `overwrite=0` so a user
can override with their own config.

### 3. Batch Placement Sound Flooding (contributing factor)

Zone and road drag operations release all queued tiles at once on LMB-up.
`UIManager` loops `doTerrainPlacement(tx, tz)` for every tile in the rectangle/line;
each call reaches `CitySimulation::placeZone()` / `placeRoad()`, which previously fired
`SFX_EARTHWORKS + SFX_BUILD_PLACE` (or `SFX_ROAD_BUILD`) unconditionally — 2 × N
`playPositionalSound` calls in a single frame (e.g. 200 calls for a 10×10 zone).

This floods the SFX source pool with identical concurrent positional sources and
dramatically increases the HRTF mixing load per period, making the ALSA catch-up loop
more likely to hit the 200 ms RLIMIT\_RTTIME limit described above.

**Fix**: `CitySimulation` gates both `placeZone()` and `placeRoad()` behind a shared
100 ms cooldown stored in `m_lastPlacementSoundTime` (a `double` member initialised to
`-1.0`). The cooldown uses the injected `IClock*` (`m_clock->nowSeconds()`) for
determinism in tests.

```cpp
// In placeZone() and placeRoad():
if (m_audio && m_clock) {
    const double now = m_clock->nowSeconds();
    if (now - m_lastPlacementSoundTime >= 0.1) {
        m_lastPlacementSoundTime = now;
        const vec3 pos{static_cast<float>(tileX), 0.0f,
                       static_cast<float>(tileZ)};
        if (earthworksCostOverride > 0)
            m_audio->playPositionalSound(SFX_EARTHWORKS, pos,
                                         SoundPriority::NORMAL, 1.0f);
        m_audio->playPositionalSound(SFX_BUILD_PLACE /*or SFX_ROAD_BUILD*/, pos,
                                     SoundPriority::NORMAL, 1.0f);
    }
}
```

The result: any batch operation that completes within 100 ms plays exactly **one**
earthworks cue and one placement cue, regardless of how many tiles were modified.
The cooldown is shared between zone and road placement so interleaved calls are also
gated correctly.

---

## Phase 1 sound-dev-opensoftal Sign-Off

**Date**: 2026-02-21
**Role**: sound-dev-opensoftal

The following Phase 1 items have been verified by code inspection:

1. **OAL-2 frame-loop comment covers all 3 update() responsibilities**: Code inspection of `src/main.cpp` confirms the step 4b OAL-2 stub comment explicitly names all three main-thread responsibilities of `AudioSystem::update()`: (1) advance occlusion raycast budget + per-source distance cull checks (depends on listener position committed by step 4a), with the note that per-source GAINHF state-change writes MUST hold `m_occlusionMutex`; (2) process queued time-of-day transition consequences posted by `CitySimulation`; (3) queue crossfade commands to audio thread via mutex-protected command queue — MUST NOT call `alSourcef(AL_GAIN)` directly on streaming sources from the main thread. All three responsibilities are present and correctly described. VERIFIED.

2. **syncListenerToCamera Z-negation documented**: Code inspection of `src/main.cpp` confirms the step 4a stub comment explicitly documents that the Z component of BOTH `cam.forward` AND `cam.up` MUST be negated when constructing the `AL_ORIENTATION` 6-float array (`{ cam.forward.x, cam.forward.y, -cam.forward.z, cam.up.x, cam.up.y, -cam.up.z }`), with the rationale that Irrlicht uses a LEFT-HANDED coordinate system (Z forward into screen) and OpenAL uses a RIGHT-HANDED coordinate system (Z backward out of screen). Reference to `architecture/audio-architecture/spatial-audio.md` is present. VERIFIED.

3. **Thread-context safety note present**: Code inspection of `src/main.cpp` confirms the step 4b OAL-2 stub comment includes the TODO Phase 7 forward-reference verifying that `alcSetThreadContext(m_context)` on the audio thread does NOT displace the process-wide context set by `alcMakeContextCurrent(m_context)` on the main thread, with cross-reference to `architecture/audio-architecture/audio-system.md`. VERIFIED.

4. **getCameraState() live-path uses getUpVector()**: Code inspection of `src/ui/CameraController.cpp` confirms that the live-camera path (when `camera != nullptr`) assigns `state.up = toVec3(m_camera->getUpVector())` with an explicit comment `// MUST use getUpVector(), NOT (0,1,0)`. The hardcoded `(0,1,0)` form is absent from the live path. VERIFIED.

5. **onSourceRecycled() / m_occlusionMutex contract present**: Code inspection of `architecture/audio-architecture/audio-occlusion.md` confirms that the `onSourceRecycled()` implementation block explicitly shows `std::lock_guard<std::mutex> lk(m_occlusionMutex)` as the first statement before any EFX filter writes, with documentation that this is called from the main thread at SFX pool acquisition/eviction time, that the audio thread's `updateOcclusion()` holds the same mutex during EFX filter writes, and that acquiring `m_occlusionMutex` in `onSourceRecycled()` is correct and carries no deadlock risk because `m_streamMutex` and `m_occlusionMutex` are never held simultaneously by the same thread. VERIFIED.

---

## Phase 7 sound-dev-opensoftal Sign-Off

**Date**: 2026-02-27
**Role**: sound-dev-opensoftal

Phase 7 full `AudioSystem` RAII implementation delivered. The following items are verified:

1. **Constructor sequence**: `alcOpenDevice` (failure → silent mode: `m_deviceLost=true`, `return`)
   → HRTF attrs context → `alcMakeContextCurrent` →
   `alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED)` → `alGenSources(62)` → pre-load queue populate →
   `alcGetProcAddress("alcSetThreadContext")` (throws `std::runtime_error` if null) → EFX filter
   allocation → `m_occlusionGainTarget[]` init to 1.0f → `m_useThreadLocalCtx = true` → thread
   launch → `m_initCV.wait_for` 5 s. VERIFIED by code inspection of
   `src/audio/AudioSystem.cpp`. Silent-mode early-return verified: all `m_deviceLost` guards
   confirmed on `setMusicTrack`, `triggerStinger`, `syncListenerToCamera`, `transitionToGameplay`,
   `update`, `setMasterVolume`, `playSound`, `playPositionalSound`, `stopSound`.

2. **Audio thread init order**: `m_fnSetThreadCtx(m_context)` FIRST action; pre-load queue drain;
   `m_lastDuckWakeTime = m_clock->nowSeconds()` BEFORE `notify_one` (epoch-dt prevention);
   `m_initDone = true; notify_one()`. VERIFIED — `DuckStateMachine_FirstWake_DtIsNotEpochSized`
   test confirms epoch-dt prevention; test passes at 16/16.

3. **Shutdown Step 3.5**: `if (m_context && m_useThreadLocalCtx) { alcMakeContextCurrent(m_context); }`
   executed after `join()` before any AL cleanup. `AL_BUFFERS_QUEUED` query used (never hardcoded
   count). `m_efxAllocationAttempted` (not `m_efxAvailable`) guards destructor filter loop.
   VERIFIED by code inspection of destructor sequence.

4. **`IAlcFunctions` seam**: `src/interfaces/IAlcFunctions.h` defines interface (moved from
   `src/audio/ialc_functions.h` in Phase 10b Feature 3); `DefaultAlcFunctions` wraps
   real ALC in `AudioSystem.cpp`; `MockAlcFunctions` in `audio_thread_test.cpp` returns null for
   all `getProcAddress` calls. `AudioThread_AbsentThreadLocalContext_ConstructorThrows` now active
   (GTEST\_SKIP removed) and passing. VERIFIED — 16/16 tests pass.

5. **Source pool layout**: `VehiclePairSlot` defined in `AudioSourcePool.h` (complete type required
   for `std::array<VehiclePairSlot, kMaxVehiclePairs>`). `kTransientReserveStart=51` for LOW/NORMAL
   SFX; `kEvictableSFXCount=55` for HIGH/CRITICAL. Atomic pair acquisition in
   `acquireVehicleEnginePair` — partial acquisition prohibited. VERIFIED.

6. **Bar-boundary tracking**: `m_samplesQueued` software counter incremented by decoded frames.
   `AL_SAMPLE_OFFSET` never used. Bootstrap branch fires once when
   `m_nextBarBoundary == 0 && m_samplesQueued > 0`. VERIFIED by code inspection.

7. **Test results**: `ALSOFT_DRIVERS=null AITOWN_HEADLESS=1 ./build/audio_tests` → 16/16 PASSED,
   0 FAILED. Full project build: 282/282 unit tests pass.
