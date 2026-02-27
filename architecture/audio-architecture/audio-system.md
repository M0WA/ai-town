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

The file `src/interfaces/audio_system.h` must include the following headers:

```cpp
#include "simulation_types.h"    // Required: SimSpeed (type alias for SpeedMultiplier)
#include "audio_types.h"         // Required: SoundId, MusicTrackId, SoundPriority, StingerType, TimeOfDay, SoundHandle
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
// Step 1: open device and create context (throws on failure — done before thread launch).
// alcMakeContextCurrent(m_context) establishes the MANDATORY, PERMANENT process-wide
// context binding for the main thread. This binding must remain active for the entire
// application lifetime — syncListenerToCamera() issues AL listener calls on the main
// thread every frame and requires a current context on that thread.
// The audio thread will ADDITIONALLY call alcSetThreadContext(m_context) for its own
// thread-local binding; this does not displace the process-wide binding.
m_device  = alcOpenDevice(nullptr);
if (!m_device) throw std::runtime_error("alcOpenDevice failed");
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

1. **Constructor sequence**: `alcOpenDevice` → HRTF attrs context → `alcMakeContextCurrent` →
   `alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED)` → `alGenSources(62)` → pre-load queue populate →
   `alcGetProcAddress("alcSetThreadContext")` (throws `std::runtime_error` if null) → EFX filter
   allocation → `m_occlusionGainTarget[]` init to 1.0f → `m_useThreadLocalCtx = true` → thread
   launch → `m_initCV.wait_for` 5 s. VERIFIED by code inspection of
   `src/audio/AudioSystem.cpp`.

2. **Audio thread init order**: `m_fnSetThreadCtx(m_context)` FIRST action; pre-load queue drain;
   `m_lastDuckWakeTime = m_clock->nowSeconds()` BEFORE `notify_one` (epoch-dt prevention);
   `m_initDone = true; notify_one()`. VERIFIED — `DuckStateMachine_FirstWake_DtIsNotEpochSized`
   test confirms epoch-dt prevention; test passes at 16/16.

3. **Shutdown Step 3.5**: `if (m_context && m_useThreadLocalCtx) { alcMakeContextCurrent(m_context); }`
   executed after `join()` before any AL cleanup. `AL_BUFFERS_QUEUED` query used (never hardcoded
   count). `m_efxAllocationAttempted` (not `m_efxAvailable`) guards destructor filter loop.
   VERIFIED by code inspection of destructor sequence.

4. **`IAlcFunctions` seam**: `ialc_functions.h` defines interface; `DefaultAlcFunctions` wraps
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
