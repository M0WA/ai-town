#pragma once
// AudioSystem.h — Phase 7 full implementation header.
//
// irr::ILogger forward-declared below — do NOT include <irrlicht.h> here.
// The zero-AL-includes contract extends to zero-Irrlicht-includes as well.
//
// ZERO OpenAL includes in this header.  All AL/ALC types that appear as member
// types are either forward-declared below (ALCdevice, ALCcontext) or represented
// via local type aliases (FnSetThreadCtx) or unsigned int (ALuint = unsigned int).
// This preserves the headless-CI guarantee: test TUs that #include this header
// compile without an AL device or AL headers.
//
// FROZEN MEMBER NAMES (locked Phase 3 — do NOT rename without updating comment):
//   m_clock, m_lastDuckWakeTime, m_useThreadLocalCtx, m_fnSetThreadCtx,
//   m_occlusionGainTarget

#include "src/interfaces/IAudioSystem.h"
#include "src/interfaces/IClock.h"
#include "src/interfaces/audio_types.h"
#include "src/audio/audio_command_queue.h"
#include "src/interfaces/IAlcFunctions.h"
#include "src/audio/AudioSourcePool.h"   // for AudioSourcePool (vehicle pair pool management)

#include <atomic>
#include <array>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// Forward declarations — avoids pulling <AL/alc.h> into this header.
// The concrete types are defined in <AL/alc.h> which is included ONLY in
// AudioSystem.cpp.  ALuint / ALint / ALfloat are unsigned/signed int —
// they are aliased below to avoid the OpenAL header dependency.
//
// Guard: if <AL/alc.h> has already been included (e.g. in AudioSystem.cpp
// which includes both this header and <AL/alc.h>), skip the forward
// declarations because alc.h uses "typedef struct ALCdevice ALCdevice" style
// (not struct ALCdevice_struct) and redefining via our using-declaration
// would produce a conflicting declaration error.
// ---------------------------------------------------------------------------
#ifndef AL_ALC_H
struct ALCdevice_struct;
using  ALCdevice  = ALCdevice_struct;
struct ALCcontext_struct;
using  ALCcontext = ALCcontext_struct;
#endif // AL_ALC_H

// Forward-declare irr::ILogger — avoids pulling <irrlicht.h> into this header.
// AudioSystem.cpp includes <irrlicht.h> directly.
namespace irr { class ILogger; }

// Local type alias for alcSetThreadContext — matches the real signature exactly.
// Using PFNALCSETTHREADCONTEXTPROC would require <AL/alext.h> and break the
// zero-AL-includes contract.
using FnSetThreadCtx = int(*)(ALCcontext*);

// EFX function-pointer typedefs that appear as member types.
// Defined identically to the typedefs in <efx.h> but repeated here so the
// header compiles without <efx.h>.  Phase 7 checks these against the real
// typedefs in AudioSystem.cpp via static_assert.
using LPALGENFILTERS_t    = void(*)(int, unsigned int*);
using LPALFILTERI_t       = void(*)(unsigned int, int, int);
using LPALFILTERF_t       = void(*)(unsigned int, int, float);
using LPALDELETEFILTERS_t = void(*)(int, const unsigned int*);

// ---------------------------------------------------------------------------
// AudioStream — per-stream state for one streaming OGG source.
// Defined here (not in a separate header) because AudioSystem holds an
// inline array of kStreamSourceCount AudioStream objects.
// ---------------------------------------------------------------------------
struct OggVorbis_File;  // forward-declared; full type in <vorbis/vorbisfile.h>

struct AudioStream {
    // AL source handle backing this stream slot (sourced from m_sources[]).
    unsigned int sourceHandle{0};  // ALuint

    // AL buffer pool for this stream (8 × 64 KB buffers).
    static constexpr int kNumBuffers = 8;
    unsigned int buffers[kNumBuffers]{};  // ALuint[]

    // Software sample counter — incremented by frame count decoded each update.
    // Used for bar-boundary crossfade calculation (NOT AL_SAMPLE_OFFSET).
    // "Samples" = PCM frames (per-channel samples at one point in time).
    uint64_t m_samplesQueued{0};

    // Absolute sample-frame index of the next bar boundary.
    // Initialized to 0; bootstrap branch fires once after first decode.
    // Guard: m_nextBarBoundary > 0 prevents false crossfade at stream start.
    uint64_t m_nextBarBoundary{0};

    // kSamplesPerBuffer: 64 KB / (2 channels × 2 bytes/sample) = 16384 frames.
    static constexpr uint32_t kSamplesPerBuffer = 64u * 1024u / (2u * 2u);

    // OggVorbis file handle — persistent member opened at stream start,
    // closed via ov_clear() in the destructor/stop path.
    // Raw pointer managed manually — libvorbisfile owns the struct memory.
    OggVorbis_File* vf{nullptr};

    // Sidecar metadata (music stems only — ambient beds have no sidecar).
    float bpm{90.0f};
    int   beatsPerBar{4};
    bool  isMusicStem{false};   // true = bar-boundary crossfade; false = ambient (real-time)
    bool  isOpen{false};        // true when vf is valid and ready to decode
    bool  m_intentionallyStopped{false};  // set+alSourceStop must be in same m_streamMutex scope

    // Gain computed by the crossfade curve [0..1]; applied by the audio thread.
    float crossfadeGain{1.0f};

    // Computed samples-played estimate helper (same formula as spec).
    static uint64_t computeSamplesPlayed(uint64_t samplesQueued, int buffersQueued) {
        uint64_t queued = static_cast<uint64_t>(buffersQueued) * kSamplesPerBuffer;
        return (samplesQueued > queued) ? samplesQueued - queued : 0u;
    }

    AudioStream() = default;
    AudioStream(const AudioStream&) = delete;
    AudioStream& operator=(const AudioStream&) = delete;
};

// ---------------------------------------------------------------------------
// AudioSystem — full RAII implementation of IAudioSystem.
//
// PARTIAL-CONSTRUCTION SAFETY: The destructor guards each cleanup step with a
// dedicated boolean flag (m_deviceCreated, m_contextCreated, etc.) so that a
// constructor that fails mid-sequence does not attempt to destroy resources that
// were never created.
//
// THREAD MODEL:
//   Main thread  — alcMakeContextCurrent (permanent process-wide binding);
//                  syncListenerToCamera() listener AL calls each frame;
//                  update() queues commands; occlusion raycast pass writes
//                  m_occlusionGainTarget[] atomically.
//   Audio thread — alcSetThreadContext (thread-local binding only, does not
//                  displace the main-thread process-wide binding);
//                  ALL alSourcef(AL_GAIN) on streaming sources;
//                  updateStreams(), updateOcclusion(), updateDuckState().
// ---------------------------------------------------------------------------
class AudioSystem : public IAudioSystem {
public:
    // Primary constructor (production use).
    // logger    — Irrlicht ILogger* for diagnostic output; nullptr falls back to stderr.
    // clock     — IClock injection for deterministic timing in tests.
    // alcFunctions — IAlcFunctions injection seam; nullptr uses DefaultAlcFunctions (real ALC).
    explicit AudioSystem(irr::ILogger* logger, IClock* clock, IAlcFunctions* alcFunctions = nullptr);
    ~AudioSystem() override;

    AudioSystem(const AudioSystem&)            = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    // IAudioSystem interface — all implemented in AudioSystem.cpp.
    SoundHandle playSound(SoundId id, SoundPriority priority, float gain = 1.0f) override;
    SoundHandle playPositionalSound(SoundId id, vec3 pos, SoundPriority priority, float gain = 1.0f) override;
    void stopSound(SoundHandle handle) override;
    void setMusicTrack(MusicTrackId id) override;
    void setSpeed(SimSpeed speed) override;
    void triggerStinger(StingerType type) override;
    void syncListenerToCamera(const CameraState& cam) override;
    void setGameOverState(bool active) override;
    void setTimeOfDay(TimeOfDay tod) override;
    void transitionToGameplay() override;
    void update(float realDeltaSeconds) override;
    void setMasterVolume(float gain) override;
    void setMusicVolume(float gain) override;
    void setSFXVolume(float gain) override;
    // Phase 10: adaptive music intensity.
    // Implementation queues a crossfade command to the audio thread; the thread
    // selects the stem pair for the new intensity and begins a beat-boundary crossfade.
    // Time-of-day forced-Calm override (DUSK/NIGHT/DAWN) is enforced internally.
    // Calling with the tier already active is a no-op.
    // Thread-safety: call from the main thread only.
    void setMusicIntensity(MusicIntensity intensity) override;

    // Phase 11d — Vehicle engine audio pair API (stubs; full impl in Deliverable 3a).
    std::pair<int,int> acquireVehicleEnginePair(ZoneType zone) override;
    void releaseVehicleEnginePair(int idleIdx, int moveIdx) override;
    void updateVehicleAudio(int idleIdx, int moveIdx, float speedFraction,
                            float worldX, float worldZ) override;

    // Called from AudioSourcePool on slot recycle (and from vehicle pair release).
    // Resets occlusion gain to 1.0f immediately, applying the filter reset.
    // MUST NOT be called while m_occlusionMutex is held by the caller.
    void onSourceRecycled(int sourceIdx);

private:
    // -----------------------------------------------------------------------
    // Partial-construction guard flags — checked in destructor before each
    // cleanup step to make construction exception-safe.
    // -----------------------------------------------------------------------
    bool m_deviceCreated{false};
    bool m_contextCreated{false};
    bool m_contextMadeCurrent{false};
    bool m_sourcesGenerated{false};

    // -----------------------------------------------------------------------
    // OpenAL device / context (forward-declared types, zero AL includes here).
    // -----------------------------------------------------------------------
    ALCdevice*  m_device{nullptr};
    ALCcontext* m_context{nullptr};

    // -----------------------------------------------------------------------
    // Thread-local context extension — ALC_EXT_thread_local_context.
    // m_fnSetThreadCtx must be non-null before audio thread launch;
    // if alcGetProcAddress returns null the constructor throws.
    // m_useThreadLocalCtx is set true only after the proc address is confirmed.
    // Used in shutdown step 3.5 to decide whether to re-bind context.
    // -----------------------------------------------------------------------
    bool          m_useThreadLocalCtx{false};
    FnSetThreadCtx m_fnSetThreadCtx{nullptr};

    // -----------------------------------------------------------------------
    // IAlcFunctions injection — allows tests to mock ALC calls without hardware.
    // Owned externally; AudioSystem never deletes it.
    // -----------------------------------------------------------------------
    IAlcFunctions* m_alcFunctions{nullptr};  // non-owning

    // -----------------------------------------------------------------------
    // Audio thread lifecycle.
    // -----------------------------------------------------------------------
    std::atomic<bool>         m_stopThread{false};
    // Set by the audio thread when the AL backend fails (e.g. broken pipe).
    // All main-thread AL-calling paths guard on this flag before any AL call
    // so that a backend disconnection degrades to silent audio rather than
    // propagating an uncaught exception that would call std::terminate.
    std::atomic<bool>         m_deviceLost{false};
    std::thread               m_audioThread;

    // m_streamMutex — crossfade command queue mutex (also guards streaming AL calls).
    // m_occlusionMutex — protects EFX filter writes in onSourceRecycled / updateOcclusion.
    // Two-mutex design: intentional and complete for V1.
    std::mutex                m_streamMutex;
    std::mutex                m_occlusionMutex;
    std::condition_variable   m_streamCV;

    // Init synchronization — used only during construction; not operational.
    std::mutex                m_initMutex;
    std::condition_variable   m_initCV;
    bool                      m_initDone{false};
    bool                      m_initError{false};

    // -----------------------------------------------------------------------
    // AL source pool — 62 sources pre-allocated in constructor.
    // Layout:  [0..54]  evictable SFX (kEvictableSFXCount = 55)
    //          [55..56] stingers (kStingerCount = 2; CRISIS=55, MILESTONE=56)
    //          [57]     idle post-V1 game-over slot (never acquired in V1)
    //          [58..61] streams (kStreamSourceCount = 4)
    // -----------------------------------------------------------------------
    unsigned int m_sources[kTotalSources]{};  // ALuint[]

    // Preloaded static buffers: SoundId -> ALuint buffer handle.
    std::unordered_map<SoundId, unsigned int> m_preloadedBuffers;

    // -----------------------------------------------------------------------
    // Pre-load command queue — populated by constructor before thread launch;
    // drained by audio thread before signaling m_initCV.
    // -----------------------------------------------------------------------
    AudioPreloadQueue m_preloadQueue;

    // -----------------------------------------------------------------------
    // Streaming state — 4 AudioStream slots (2 music + 2 ambient beds).
    // streams[0..1] = music stems (sources[58..59])
    // streams[2..3] = ambient beds (sources[60..61])
    // -----------------------------------------------------------------------
    AudioStream m_streams[kStreamSourceCount];

    // Music crossfade progress: 0→1 over crossfade duration.
    std::atomic<float> m_musicCrossfadeT{0.0f};
    std::atomic<float> m_ambientCrossfadeT{0.0f};

    // Index of incoming (new) music stream slot (0 or 1; -1 = no crossfade active).
    int m_musicIncomingSlot{-1};
    // Index of the currently-active (outgoing) music stem slot (0 or 1).
    // Starts at 0; swaps to 1 after first crossfade completes, then back to 0, etc.
    // Audio-thread only — no mutex needed.
    int m_musicActiveSlot{0};
    // Duration of the current crossfade in seconds.
    float m_musicCrossfadeDuration{0.0f};

    // Phase 10: adaptive intensity — audio-thread-local copy of the last intensity
    // it started streaming.  Compared against m_currentMusicIntensity each wake to
    // detect a tier change that requires a new crossfade.  Audio thread only.
    int m_audioThreadIntensity{static_cast<int>(MusicIntensity::CALM)};

    // Phase 10: ambient-bed crossfade state (audio thread only).
    // m_ambientIncomingSlot: stream slot index (2 or 3) for the incoming ambient bed
    //   (-1 = no ambient crossfade active).
    // m_ambientCrossfadeDuration: real-time duration (seconds) of the ambient crossfade.
    int   m_ambientIncomingSlot{-1};
    float m_ambientCrossfadeDuration{0.0f};

    // -----------------------------------------------------------------------
    // EFX occlusion state.
    // m_efxAllocationAttempted — set true before first m_fnGenFilters call;
    //                            guards destructor cleanup regardless of success.
    // m_efxAvailable          — set true ONLY after ALL kEvictableSFXCount
    //                            filters fully allocated; guards runtime paths.
    // -----------------------------------------------------------------------
    bool         m_efxAllocationAttempted{false};
    bool         m_efxAvailable{false};
    unsigned int m_occlusionFilter[kEvictableSFXCount]{};  // ALuint[]

    // EFX function pointers stored as members (safe for audio thread reuse).
    LPALGENFILTERS_t    m_fnGenFilters{nullptr};
    LPALFILTERI_t       m_fnFilteri{nullptr};
    LPALFILTERF_t       m_fnFilterf{nullptr};
    LPALDELETEFILTERS_t m_fnDeleteFilters{nullptr};

    // Per-source occlusion gain smoothing.
    // m_occlusionGainCurrent — audio thread only (no atomic needed).
    // m_occlusionGainTarget  — written by main thread, read by audio thread:
    //                          MUST be std::atomic<float> (C++ data race if plain float).
    //                          Initialized to 1.0f before thread launch (see constructor).
    float              m_occlusionGainCurrent[kEvictableSFXCount]{};
    std::atomic<float> m_occlusionGainTarget[kEvictableSFXCount];

    // Occlusion raycast budget tracking.
    int m_raycastFrameCounter[kEvictableSFXCount]{};  // frames since last raycast per source

    // -----------------------------------------------------------------------
    // Music duck state machine (audio thread for gain writes; main reads atomic).
    // -----------------------------------------------------------------------
    enum class DuckState { IDLE, DUCKING, DUCKED, RELEASING };
    std::atomic<DuckState> m_duckState{DuckState::IDLE};
    std::atomic<float>     m_musicDuckGain{1.0f};
    float                  m_duckTimer{0.0f};      // seconds elapsed in current duck phase
    float                  m_duckStartGain{1.0f};  // gain at transition into DUCKING

    // -----------------------------------------------------------------------
    // IClock injection — deterministic timing for tests.
    // m_lastDuckWakeTime initialized in audio thread (NOT constructor) to
    // prevent epoch-sized dt on first wake.
    // -----------------------------------------------------------------------
    IClock* m_clock{nullptr};
    double  m_lastDuckWakeTime{0.0};  // FROZEN NAME (Phase 3)

    // -----------------------------------------------------------------------
    // ILogger injection (Phase 11l Deliverable 9).
    // m_logger is non-owning; Irrlicht retains ownership.
    // m_logMutex serialises concurrent log() calls from the audio thread and
    // the main thread — irr::ILogger is not documented as thread-safe.
    // -----------------------------------------------------------------------
    irr::ILogger* m_logger{nullptr};
    std::mutex    m_logMutex;

    // -----------------------------------------------------------------------
    // Game-over fade state (post-V1 Scenario mode only — stubs in V1).
    // m_gameOverFade is NEVER set in any V1 code path (setGameOverState is a no-op).
    // Declared as stubs so post-V1 Scenario mode can add logic without class restructure.
    // -----------------------------------------------------------------------
    bool  m_gameOverFade{false};
    float m_gameOverFadeT{0.0f};

    // -----------------------------------------------------------------------
    // Stinger cooldown tracking: time of last trigger per stinger type.
    // Indexed by static_cast<int>(StingerType) - kEvictableSFXCount.
    // -----------------------------------------------------------------------
    double m_stingerLastTriggerTime[kStingerCount]{};

    // -----------------------------------------------------------------------
    // Simulation speed (for time-of-day collapse logic).
    // -----------------------------------------------------------------------
    SimSpeed m_currentSpeed{SimSpeed::x1};

    // -----------------------------------------------------------------------
    // Volume controls (Phase 8).
    // m_masterVolume is plain float — applied via alListenerf(AL_GAIN) on
    // the calling thread (main thread only).
    // m_musicVolume and m_sfxVolume are std::atomic<float> — written from
    // main thread, read from audio thread at next wake.
    // -----------------------------------------------------------------------
    float              m_masterVolume{1.0f};
    std::atomic<float> m_musicVolume{0.8f};
    std::atomic<float> m_sfxVolume{0.8f};

    // -----------------------------------------------------------------------
    // Time-of-day state.
    // m_currentTimeOfDay: written from main thread, read from audio thread.
    //   Use atomic<int> to eliminate the data race (TimeOfDay is int-backed).
    //   Cast: static_cast<TimeOfDay>(m_currentTimeOfDay.load()).
    // m_pendingAmbientTod: set to the new TimeOfDay value (as int) when the
    //   main thread calls setTimeOfDay() with a new value. The audio thread
    //   reads this once per wake and, if != -1, begins an ambient crossfade
    //   then resets it to -1.  -1 means no pending change.
    //   Written by main thread, read by audio thread — must be atomic<int>.
    // -----------------------------------------------------------------------
    std::atomic<int> m_currentTimeOfDay{static_cast<int>(TimeOfDay::DAY)};
    bool             m_timeOfDaySet{false};
    std::atomic<int> m_pendingAmbientTod{-1};  // -1 = no pending ambient crossfade

    // -----------------------------------------------------------------------
    // Adaptive music intensity state (Phase 10).
    // Written from main thread via setMusicIntensity(); read by audio thread
    // to select the crossfade target stem pair.  MUST be std::atomic<int>
    // (not std::atomic<MusicIntensity>) because std::atomic requires a trivially
    // copyable type — enums satisfy this, but the spec mandates atomic<int> to
    // make the threading contract explicit and avoid misuse as a direct enum load.
    // Callers cast: static_cast<MusicIntensity>(m_currentMusicIntensity.load()).
    // Initialized to CALM (0) — no crossfade triggered before first update().
    // -----------------------------------------------------------------------
    std::atomic<int> m_currentMusicIntensity{static_cast<int>(MusicIntensity::CALM)};

    // -----------------------------------------------------------------------
    // Vehicle audio slot — per-active-vehicle cross-thread state.
    //
    // Main thread writes idleSourceIdx/moveSourceIdx/basePitch/pendingInit
    // at acquire time; writes speedFraction/worldX/worldZ per-frame via
    // updateVehicleAudio(); writes pendingRelease at release time.
    // Audio thread reads all fields each wake in updateVehicleEngines().
    //
    // All fields are std::atomic<> to satisfy the C++ data-race-free contract
    // (main thread and audio thread access concurrently with no mutex).
    // pendingInit / pendingRelease use relaxed loads/stores — the audio thread
    // wake interval (~10 ms) provides sufficient ordering; no ABA risk because
    // acquire and release are serialized by the single-threaded main loop.
    // -----------------------------------------------------------------------
    struct VehicleAudioSlot {
        // Indices used by both main thread (acquire/release) and audio thread.
        // idleSourceIdx == -1 means the slot is free.
        std::atomic<int>   idleSourceIdx{-1};
        std::atomic<int>   moveSourceIdx{-1};

        // Set at acquire time; read by audio thread for pitch computation.
        std::atomic<float> basePitch{1.0f};    // 1.0 (car) or 0.85 (bus/truck)

        // pendingInit: main thread sets true at acquire; audio thread clears after
        // binding buffers, setting AL_VELOCITY=0, and calling alSourcePlay.
        std::atomic<bool>  pendingInit{false};

        // pendingRelease: main thread sets true at release (and stores the source
        // indices into pendingReleaseIdle/Move before clearing idleSourceIdx).
        // Audio thread calls alSourceStop on those indices, then clears this flag.
        std::atomic<bool>  pendingRelease{false};

        // Source indices preserved for pendingRelease processing.
        // Written by main thread before setting pendingRelease=true;
        // read by audio thread when pendingRelease=true.
        std::atomic<int>   pendingReleaseIdle{-1};
        std::atomic<int>   pendingReleaseMove{-1};

        // Per-frame state written by main thread in updateVehicleAudio();
        // read by audio thread each wake.
        std::atomic<float> speedFraction{0.f};
        std::atomic<float> worldX{0.f};
        std::atomic<float> worldZ{0.f};
    };
    VehicleAudioSlot m_vehicleAudio[kMaxVehiclePairs];

    // Called on audio thread each wake to process pending vehicle init/release
    // commands and apply per-frame AL_PITCH / AL_GAIN / AL_POSITION updates.
    void updateVehicleEngines();

    // Reclaim finished (non-looping) SFX sources — called once per audio thread wake.
    // Must run on the audio thread so AL state queries and buffer detaches do not
    // race with audio-thread AL calls and bleed spurious errors into alCheckError_real.
    // Vehicle engine sources (SFX_VEHICLE_ENGINE_IDLE/MOVE) are skipped; they are
    // managed exclusively by updateVehicleEngines().
    void cleanupFinishedSFX();

    // SFX pool "in-use" tracking: source index -> SoundHandle.
    // SoundHandle 0 is invalid; generated handles are sequential > 0.
    struct SFXSlot {
        SoundId  soundId{0};
        unsigned int buffer{0};   // ALuint — static buffer bound to source
        SoundHandle handle{0};
        SoundPriority priority{SoundPriority::LOW};
        float listenerDistanceSq{0.f};
        bool  occupied{false};
    };
    SFXSlot m_sfxSlots[kEvictableSFXCount];
    std::atomic<SoundHandle> m_nextHandle{1};  // monotonic handle counter

    // Per-slot atomic flag: true while the slot is reserved for a vehicle engine pair.
    // Set to true (with memory_order_release) BEFORE m_sfxSlots[i] is populated in
    // acquireVehicleEnginePair, and cleared to false BEFORE the slot is released in
    // releaseVehicleEnginePair / eviction path.
    // Used by cleanupFinishedSFX (audio thread) to skip vehicle engine sources without
    // reading the non-atomic m_sfxSlots[i].soundId field (which would be a data race).
    std::atomic<bool> m_sfxVehicleReserved[kEvictableSFXCount]{};

    // -----------------------------------------------------------------------
    // Internal helpers.
    // -----------------------------------------------------------------------

    // Audio thread entry point.
    void audioThreadFunc();

    // Streaming update — called once per audio thread wake.
    // dt is the real-time elapsed seconds since the previous wake (from IClock).
    // Used to advance music and ambient crossfade timers with accurate real-time
    // deltas instead of a hardcoded nominal interval.
    void updateStreams(float dt);

    // Occlusion gain smoothing — called once per audio thread wake.
    void updateOcclusion();

    // Duck state machine update — called once per audio thread wake with dt.
    void updateDuckState(float dt);

    // Process one pre-load command (decode OGG into AL buffer; called on audio thread).
    void processPreloadCommand(const PreloadCommand& cmd);

    // Load a WAV file into an AL buffer; returns buffer handle or 0 on failure.
    unsigned int loadWAV(const std::string& path);

    // Open streaming OGG for the given stream slot (allocates m_streams[slot].vf).
    bool openStreamOGG(int streamSlot, const std::string& path, bool isMusicStem);

    // Close a stream slot, stopping the source and releasing the OGG handle.
    void closeStream(int streamSlot);

    // Refill processed buffers for one stream slot; returns buffers re-queued.
    int refillStream(int streamSlot);

    // Begin a music crossfade from current active stem to new music track.
    void beginMusicCrossfade(MusicTrackId targetId);

    // Phase 10: begin a music crossfade to the stem pair matching the given
    // intensity tier. Respects the time-of-day forced-Calm override internally.
    // Called on the audio thread inside updateStreams() when intensity changes.
    // Selects one of the two stems for the target tier (alternates for variety).
    void beginIntensityCrossfade(MusicIntensity intensity);

    // Phase 10: begin an ambient-bed crossfade to the bed for the given TimeOfDay.
    // Called on the audio thread inside updateStreams() when time-of-day changes.
    void beginAmbientCrossfade(TimeOfDay tod);

    // Phase 10: apply constant-power crossfade gains to music slots 0 and 1.
    // outSlot = outgoing (fading out); inSlot = incoming (fading in).
    // t in [0..1]: 0 = crossfade just started (out at full), 1 = finished (in at full).
    static void applyCrossfadeGains(AudioStream& outStream, AudioStream& inStream, float t);

    // Resolve asset path: AITOWN_ASSETS_DIR/audio/<filename>
    static std::string assetPath(const std::string& filename);

    // Resolve music track filename from MusicTrackId.
    static std::string musicTrackFilename(MusicTrackId id);

    // Resolve ambient bed filename from TimeOfDay.
    static std::string ambientBedFilename(TimeOfDay tod);

    // Load JSON sidecar for a music stem; fills bpm/beatsPerBar.
    bool loadMusicSidecar(const std::string& stemPath, float& bpmOut, int& beatsPerBarOut);

    // Setup stinger source attributes (called at construction for each stinger slot).
    void setupStingerSource(int sourceIdx);

    // Setup stream source attributes (non-positional, SOURCE_RELATIVE=AL_TRUE).
    void setupStreamSource(int sourceIdx);

    // EFX filter allocation loop (runs in constructor on main thread).
    void allocateEFXFilters();

    // Log helpers — route through irr::ILogger when available, else stderr.
    // Instance methods (not static) so they can access m_logger / m_logMutex.
    void logWarning(const std::string& msg);
    void logError(const std::string& msg);
    void logInfo(const std::string& msg);
};
