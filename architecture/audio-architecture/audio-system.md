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
// IAudioSystem.h must #include "simulation_types.h" to get this alias.
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
};
```

`MockAudioSystem` in `tests/simulation/mock_audio_system.h` provides GMock implementations of all eleven methods above (using `MOCK_METHOD` macros). Test files that need audio isolation include `mock_audio_system.h` and inject `MockAudioSystem` via the `IAudioSystem*` constructor parameter of `CitySimulation`.

---

```cpp
class AudioSystem : public IAudioSystem {
public:
    explicit AudioSystem(IClock* clock);   // alcOpenDevice + alcCreateContext (with HRTF attrs); throws on failure
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
    std::mutex                m_streamMutex;
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

**HARD REQUIREMENT**: If `ALC_EXT_thread_local_context` is absent at `AudioSystem` construction, the constructor MUST throw `std::runtime_error` (or equivalent non-recoverable failure). There is NO fallback to using `alcMakeContextCurrent` on the main thread — that approach is incorrect for multi-threaded OpenAL and is prohibited. The check sequence:

1. Load extension via `alcGetProcAddress(m_device, "alcSetThreadContext")` — store as `m_fnSetThreadCtx`.
2. If `m_fnSetThreadCtx == nullptr` (extension absent): throw `std::runtime_error("ALC_EXT_thread_local_context required")`.
3. Only proceed to `alGenSources` and audio thread launch if extension is confirmed present.

This must be a Phase 3 locked behavioral contract — Phase 7 implementation MUST NOT deviate from this by using any fallback path.

**Why no fallback is permitted**: Using `alcMakeContextCurrent` to bind the context on the main thread and then calling AL functions from the audio thread is a data race — the OpenAL specification requires that each thread operating on a context must make it current on that thread (either via `alcMakeContextCurrent` before any threading, which is single-threaded only, or via the thread-local extension). Without `ALC_EXT_thread_local_context`, there is no safe way to call AL functions from a background thread while the main thread also makes AL calls (`syncListenerToCamera`, `playSound`, `stopSound`). Failing hard at construction is the only correct behaviour.

**Constructor sequence (within `AudioSystem::AudioSystem(IClock*)`):**

```cpp
// Step 1: open device and create context (throws on failure — done before thread launch)
m_device  = alcOpenDevice(nullptr);
if (!m_device) throw std::runtime_error("alcOpenDevice failed");
// ... build HRTF attrs array (see hrtf-initialization.md) ...
m_context = alcCreateContext(m_device, attribs);
if (!m_context || alcMakeContextCurrent(m_context) == ALC_FALSE)
    throw std::runtime_error("alcCreateContext failed");

// Step 2: load ALC_EXT_thread_local_context — HARD REQUIREMENT
// Cast to FnSetThreadCtx (defined in header as int(*)(ALCcontext*)) — NOT to
// PFNALCSETTHREADCONTEXTPROC, which requires <AL/alext.h> and would break the
// zero-AL-includes contract.  The cast is performed here in the .cpp file where
// <AL/alc.h> and <AL/alext.h> may be included freely.
m_fnSetThreadCtx = reinterpret_cast<FnSetThreadCtx>(
    alcGetProcAddress(m_device, "alcSetThreadContext"));
if (!m_fnSetThreadCtx)
    throw std::runtime_error("ALC_EXT_thread_local_context required");

// Step 3: initialize m_occlusionGainTarget[] before thread launch (see member comment)
// IMPORTANT: alcMakeContextCurrent(nullptr) MUST NOT be called here or at any point
// after alcMakeContextCurrent(m_context) above. Calling alcMakeContextCurrent(nullptr)
// would clear the process-wide context on the main thread, breaking syncListenerToCamera()
// which relies on that context being current. The audio thread uses alcSetThreadContext()
// for its own thread-local context; this is per-thread only and does NOT affect the
// process-wide context held by the main thread. The main thread retains the process-wide
// context (set in Step 1) for the entire application lifetime.
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

## Zone Loop Asset Validation

`AudioSystem::loadSound()` MUST enforce an authored hard cap on zone loop asset duration at load time.

### Enforcement Point

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
