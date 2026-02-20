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
enum class SimSpeed;
enum class StingerType;
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

    // Activate game-over audio fade sequence (post-V1 Scenario mode only).
    // Sets m_gameOverFade = true; audio thread fades all stems over 2 s then stops them.
    virtual void setGameOverState(bool active) = 0;

    // Notify the audio system that the in-game clock has crossed a time-of-day boundary.
    // Called by CitySimulation whenever the simulated hour transitions between DAY/DUSK/NIGHT/DAWN.
    // The audio system re-evaluates the active ambient bed and applies forced-Calm music overrides
    // where applicable (see dynamic-soundscape.md — Time-of-Day Music Intensity Override).
    // Also called at game start to establish the initial ambient bed before the first frame.
    virtual void setTimeOfDay(TimeOfDay tod) = 0;

    // Transition from main menu audio to gameplay audio.
    // Called by UIManager when the player starts a new game or loads a saved game.
    // Crossfades main menu music out over 1 s (constant-power curve) on sources[58..59],
    // then hands those sources to the gameplay music system to begin the first gameplay stem.
    // Also starts the ambient bed layer (sources[60..61]) for the current time-of-day period.
    // Must not be called while gameplay audio is already active (undefined behaviour).
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
    AudioSystem();   // alcOpenDevice + alcCreateContext (with HRTF attrs); throws on failure
    ~AudioSystem();  // MUST follow the audio thread shutdown sequence below
    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;
private:
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
    bool                      m_useThreadLocalCtx{false};
    PFNALCSETTHREADCONTEXTPROC m_fnSetThreadCtx{nullptr};  // type from <AL/alext.h>; LPALC... prefix is for AL (not ALC) extensions
    // Music ducking state machine (audio thread only for gain writes; main thread reads atomically):
    enum class DuckState { IDLE, DUCKING, DUCKED, RELEASING };
    std::atomic<DuckState>    m_duckState{DuckState::IDLE};
    std::atomic<float>        m_musicDuckGain{1.0f};
    float                     m_duckTimer{0.0f};    // seconds elapsed in current duck phase (audio thread only)
    float                     m_duckStartGain{1.0f}; // gain at transition INTO DUCKING state; enables correct ramp from current gain (not 1.0) on RELEASING→DUCKING re-entry (audio thread only)
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

  The audio thread signals completion (success or failure) via `m_initDone`. The **success path** (after `alcSetThreadContext` succeeds) must signal before entering the streaming loop:

  ```cpp
  // Audio thread success path (after alcSetThreadContext succeeds):
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
