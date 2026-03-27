// AudioSystem.cpp — Phase 7 full implementation.
//
// This is the only translation unit that includes OpenAL headers.  All other
// files that include AudioSystem.h compile without AL hardware.
//
// INCLUDE ORDER — CRITICAL: OpenAL headers must be included BEFORE AudioSystem.h.
// AudioSystem.h forward-declares ALCdevice/ALCcontext with a #ifndef AL_ALC_H guard
// to avoid conflicting with <AL/alc.h>'s "typedef struct ALCdevice ALCdevice" style.
// Placing <AL/alc.h> first defines AL_ALC_H, so the guard in AudioSystem.h skips
// the forward declarations and the real typedefs from alc.h take precedence.

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <efx.h>

#include "src/audio/AudioSystem.h"
#include "src/audio/al_check.h"
#include "src/audio/AudioStream.h"
#include "src/interfaces/sound_ids.h"
#include "src/audio/audio_constants.h"

#include <vorbis/vorbisfile.h>

#include <irrlicht.h>  // irr::ILogger — only AudioSystem.cpp includes this; AudioSystem.h forward-declares only

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstdlib>   // getenv, setenv (setenv is POSIX/Linux only — used inside #ifndef _WIN32)

// ---------------------------------------------------------------------------
// Compile-time check that the local EFX function-pointer typedefs match <efx.h>.
// These are the types stored as AudioSystem member function pointers.
// ---------------------------------------------------------------------------
static_assert(sizeof(LPALGENFILTERS_t) == sizeof(LPALGENFILTERS),
              "LPALGENFILTERS_t size mismatch with <efx.h>");
static_assert(sizeof(LPALFILTERI_t)    == sizeof(LPALFILTERI),
              "LPALFILTERI_t size mismatch with <efx.h>");
static_assert(sizeof(LPALFILTERF_t)    == sizeof(LPALFILTERF),
              "LPALFILTERF_t size mismatch with <efx.h>");
static_assert(sizeof(LPALDELETEFILTERS_t) == sizeof(LPALDELETEFILTERS),
              "LPALDELETEFILTERS_t size mismatch with <efx.h>");

// ---------------------------------------------------------------------------
// al_check.cpp implementation (defined inline here per error-checking.md:
// "Phase 7 creates src/audio/al_check.cpp which is the only file that includes
// <AL/alc.h>".  We implement both wrappers here in the same TU.)
// The header declares them inline no-ops in Phase 3 — Phase 7 provides real
// implementations by linking al_check.cpp.  We define them here as non-inline
// so that if al_check.h's inline stubs are still visible they take precedence
// for the other TUs while this TU gets the real implementation.
// ---------------------------------------------------------------------------

// Real alCheckError — called after every AL function.
void alCheckError_real(const char* op) {
    ALenum err = alGetError();
    if (err != AL_NO_ERROR) {
        const char* msg = alGetString(err);
        std::string s("AL error in '");
        s += op;
        s += "': ";
        s += msg ? msg : "unknown";
        throw std::runtime_error(s);
    }
}

// Real alcCheckError — called after every ALC function.
// Parameter is void* matching the frozen Phase 3 header signature.
void alcCheckError_real(void* device, const char* op) {
    ALenum err = alcGetError(reinterpret_cast<ALCdevice*>(device));
    if (err != ALC_NO_ERROR) {
        std::string s("ALC error in '");
        s += op;
        s += "': ";
        s += std::to_string(static_cast<int>(err));
        throw std::runtime_error(s);
    }
}

// ---------------------------------------------------------------------------
// DefaultAlcFunctions — production implementation of IAlcFunctions.
// Delegates to real ALC calls using the stored device pointer.
// ---------------------------------------------------------------------------
struct DefaultAlcFunctions : public IAlcFunctions {
    explicit DefaultAlcFunctions(ALCdevice* device) : m_device(device) {}

    bool isExtensionPresent(const char* extName) override {
        return alcIsExtensionPresent(m_device, extName) == ALC_TRUE;
    }

    void* getProcAddress(const char* funcName) override {
        return reinterpret_cast<void*>(alcGetProcAddress(m_device, funcName));
    }

    ALCdevice* m_device;
};

// ---------------------------------------------------------------------------
// Instance logging helpers — route through irr::ILogger when available.
// m_logMutex serialises concurrent calls from audio thread and main thread
// (irr::ILogger is not documented as thread-safe).
// ---------------------------------------------------------------------------
void AudioSystem::logWarning(const std::string& msg) {
    if (m_logger) {
        std::lock_guard<std::mutex> lk(m_logMutex);
        m_logger->log(msg.c_str(), irr::ELL_WARNING);
    } else {
        std::fprintf(stderr, "[AudioSystem WARNING] (no ILogger) %s\n", msg.c_str());
    }
}
void AudioSystem::logError(const std::string& msg) {
    if (m_logger) {
        std::lock_guard<std::mutex> lk(m_logMutex);
        m_logger->log(msg.c_str(), irr::ELL_ERROR);
    } else {
        std::fprintf(stderr, "[AudioSystem ERROR] (no ILogger) %s\n", msg.c_str());
    }
}
void AudioSystem::logInfo(const std::string& msg) {
    if (m_logger) {
        std::lock_guard<std::mutex> lk(m_logMutex);
        m_logger->log(msg.c_str(), irr::ELL_INFORMATION);
    } else {
        std::fprintf(stderr, "[AudioSystem INFO] (no ILogger) %s\n", msg.c_str());
    }
}

// ---------------------------------------------------------------------------
// Asset path helpers.
// ---------------------------------------------------------------------------
std::string AudioSystem::assetPath(const std::string& filename) {
    return std::string(AITOWN_ASSETS_DIR) + "/audio/" + filename;
}

std::string AudioSystem::musicTrackFilename(MusicTrackId id) {
    switch (id) {
        case MUSIC_MAIN_MENU_01: return "music_main_menu_01.ogg";
        case MUSIC_MAIN_MENU_02: return "music_main_menu_02.ogg";
        case MUSIC_CALM_01:      return "music_calm_01.ogg";
        case MUSIC_CALM_02:      return "music_calm_02.ogg";
        case MUSIC_GROWTH_01:    return "music_growth_01.ogg";
        case MUSIC_GROWTH_02:    return "music_growth_02.ogg";
        case MUSIC_CRISIS_01:    return "music_crisis_01.ogg";
        case MUSIC_CRISIS_02:    return "music_crisis_02.ogg";
        default:                 return "";
    }
}

std::string AudioSystem::ambientBedFilename(TimeOfDay tod) {
    switch (tod) {
        case TimeOfDay::DAY:   return "ambient_day.ogg";
        case TimeOfDay::DUSK:  return "ambient_dusk.ogg";
        case TimeOfDay::NIGHT: return "ambient_night.ogg";
        case TimeOfDay::DAWN:  return "ambient_dawn.ogg";
        default:               return "ambient_day.ogg";
    }
}

// ---------------------------------------------------------------------------
// Sidecar loader (music stems only).
// ---------------------------------------------------------------------------
bool AudioSystem::loadMusicSidecar(const std::string& stemPath,
                                    float& bpmOut, int& beatsPerBarOut) {
    // Replace ".ogg" with ".json".
    std::string jsonPath = stemPath;
    auto ext = jsonPath.rfind(".ogg");
    if (ext != std::string::npos) {
        jsonPath.replace(ext, 4, ".json");
    } else {
        return false;
    }

    std::ifstream f(jsonPath);
    if (!f.is_open()) {
        logError("Missing music sidecar: " + jsonPath);
        return false;
    }

    // Minimal JSON parse: look for "bpm" and "beats_per_bar".
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    f.close();

    auto extractFloat = [&](const std::string& key, float& out) -> bool {
        auto pos = content.find("\"" + key + "\"");
        if (pos == std::string::npos) return false;
        auto colon = content.find(':', pos);
        if (colon == std::string::npos) return false;
        try { out = std::stof(content.substr(colon + 1)); return true; }
        catch (...) { return false; }
    };
    auto extractInt = [&](const std::string& key, int& out) -> bool {
        auto pos = content.find("\"" + key + "\"");
        if (pos == std::string::npos) return false;
        auto colon = content.find(':', pos);
        if (colon == std::string::npos) return false;
        try { out = std::stoi(content.substr(colon + 1)); return true; }
        catch (...) { return false; }
    };

    bool ok = extractFloat("bpm", bpmOut) && extractInt("beats_per_bar", beatsPerBarOut);
    if (!ok) {
        logError("Malformed music sidecar (missing bpm/beats_per_bar): " + jsonPath);
    }
    return ok;
}

// ---------------------------------------------------------------------------
// EFX filter allocation (runs on main thread before audio thread launch).
// ---------------------------------------------------------------------------
void AudioSystem::allocateEFXFilters() {
    // Check ALC_EXT_EFX.
    if (!alcIsExtensionPresent(m_device, "ALC_EXT_EFX")) {
        logWarning("ALC_EXT_EFX not available — occlusion disabled");
        m_efxAvailable = false;
        return;
    }

    // Load EFX entry points.
    m_fnGenFilters    = reinterpret_cast<LPALGENFILTERS_t>(
                            alGetProcAddress("alGenFilters"));
    m_fnFilteri       = reinterpret_cast<LPALFILTERI_t>(
                            alGetProcAddress("alFilteri"));
    m_fnFilterf       = reinterpret_cast<LPALFILTERF_t>(
                            alGetProcAddress("alFilterf"));
    m_fnDeleteFilters = reinterpret_cast<LPALDELETEFILTERS_t>(
                            alGetProcAddress("alDeleteFilters"));

    if (!m_fnGenFilters || !m_fnFilteri || !m_fnFilterf || !m_fnDeleteFilters) {
        logWarning("EFX entry points unavailable — occlusion disabled");
        m_efxAvailable = false;
        return;
    }

    // Allocate kEvictableSFXCount filters (sources[0..54] only).
    m_efxAllocationAttempted = true;
    for (int i = 0; i < kEvictableSFXCount; ++i) {
        m_fnGenFilters(1, &m_occlusionFilter[i]);

        if (m_occlusionFilter[i] == AL_FILTER_NULL) {
            logWarning("EFX filter allocation failed at index " +
                       std::to_string(i) + " — occlusion disabled");
            m_efxAvailable = false;
            return;
        }

        // Set filter type to lowpass.
        alGetError();  // clear before checking
        m_fnFilteri(m_occlusionFilter[i], AL_FILTER_TYPE, AL_FILTER_LOWPASS);
        if (alGetError() != AL_NO_ERROR) {
            m_fnDeleteFilters(1, &m_occlusionFilter[i]);
            m_occlusionFilter[i] = AL_FILTER_NULL;
            m_efxAvailable = false;
            return;
        }

        // Fully open default gains.
        m_fnFilterf(m_occlusionFilter[i], AL_LOWPASS_GAIN,   1.0f);
        m_fnFilterf(m_occlusionFilter[i], AL_LOWPASS_GAINHF, 1.0f);

        // Bind filter to source.
        alSourcei(m_sources[i], AL_DIRECT_FILTER,
                  static_cast<ALint>(m_occlusionFilter[i]));

        // Initialize per-source gain state.
        m_occlusionGainCurrent[i] = 1.0f;
        m_occlusionGainTarget[i].store(1.0f, std::memory_order_relaxed);
    }

    m_efxAvailable = true;
    logInfo("EFX lowpass filters allocated for " +
            std::to_string(kEvictableSFXCount) + " sources");
}

// ---------------------------------------------------------------------------
// Stinger source setup (called at construction for each stinger slot).
// ---------------------------------------------------------------------------
void AudioSystem::setupStingerSource(int sourceIdx) {
    // Non-positional — no distance attenuation, position at origin.
    alSourcei(m_sources[sourceIdx], AL_SOURCE_RELATIVE, AL_TRUE);
    alSource3f(m_sources[sourceIdx], AL_POSITION,     0.f, 0.f, 0.f);
    alSourcef(m_sources[sourceIdx],  AL_ROLLOFF_FACTOR, 0.f);
    alSource3f(m_sources[sourceIdx], AL_VELOCITY,     0.f, 0.f, 0.f);
}

// ---------------------------------------------------------------------------
// Stream source setup (called at construction for each stream slot).
// ---------------------------------------------------------------------------
void AudioSystem::setupStreamSource(int sourceIdx) {
    alSourcei(m_sources[sourceIdx],  AL_SOURCE_RELATIVE, AL_TRUE);
    alSource3f(m_sources[sourceIdx], AL_POSITION,       0.f, 0.f, 0.f);
    alSourcef(m_sources[sourceIdx],  AL_ROLLOFF_FACTOR,  0.f);
    alSource3f(m_sources[sourceIdx], AL_VELOCITY,       0.f, 0.f, 0.f);
}

// ---------------------------------------------------------------------------
// AudioSystem constructor — mandatory construction order per spec.
// ---------------------------------------------------------------------------
AudioSystem::AudioSystem(irr::ILogger* logger, IClock* clock, IAlcFunctions* alcFunctions)
    : m_clock(clock)
    , m_logger(logger)
{
    // -----------------------------------------------------------------------
    // Step 1: Open device.
    //
    // On Linux, two separate mechanisms can deliver an uncatchable SIGKILL:
    //
    //   1. rtkit (primary): When OpenAL Soft contacts rtkit via D-Bus to
    //      request SCHED_RR for its mixing thread, rtkit also sets
    //      RLIMIT_RTTIME=200ms on ALL threads in the process.  During heavy
    //      road/zone placement the mixing thread can accumulate >200ms of
    //      continuous RT CPU time, and the kernel fires SIGKILL.
    //      Fix: rt-prio=0 in the [general] section — disables rtkit entirely.
    //
    //   2. PipeWire ALSA plugin (secondary): libasound_module_pcm_pipewire.so
    //      calls kill(getpid(), SIGKILL) when its stream is destroyed after
    //      repeated underruns.  The PulseAudio ALSA plugin does not do this.
    //      Fix: device=pulse in [alsa] — routes through PulseAudio instead.
    //
    // Write a minimal alsoft.conf to /tmp with both fixes, then point
    // ALSOFT_CONF at it BEFORE the first alcOpenDevice call (OpenAL Soft
    // reads the config on that call).
    // Only do this if ALSOFT_CONF is not already set by the user/environment.
    // Both issues (rtkit, PipeWire) are Linux-only; the block is excluded on Windows.
    // -----------------------------------------------------------------------
#ifndef _WIN32
    if (!getenv("ALSOFT_CONF")) {
        const char* tmpConf = "/tmp/aitown_alsoft.conf";
        FILE* cf = fopen(tmpConf, "w");
        if (cf) {
            // rt-prio = 0: prevents rtkit from granting SCHED_RR to the
            //   OpenAL mixing thread.  When rtkit grants SCHED_RR it also
            //   sets RLIMIT_RTTIME=200ms on both the soft and hard limit
            //   process-wide.  A non-privileged process cannot raise the
            //   hard limit back (setrlimit fails with EPERM), so the only
            //   way to avoid the kernel SIGKILL is to prevent rtkit from
            //   being contacted in the first place.
            // device=pulse: routes through PulseAudio ALSA plugin instead of
            //   PipeWire's native ALSA plugin.  The PipeWire ALSA plugin calls
            //   kill(getpid(), SIGKILL) when its stream is destroyed after
            //   repeated underruns; PulseAudio does not.
            // period_size=4096, periods=8: ~744ms buffer at 44100Hz for
            //   headroom against frame spikes during heavy road/zone placement.
            fprintf(cf,
                "[general]\n"
                "rt-prio = 0\n"
                "period_size = 4096\n"
                "periods = 8\n"
                "[alsa]\n"
                "device = pulse\n");
            fclose(cf);
            setenv("ALSOFT_CONF", tmpConf, /*overwrite=*/0);
            logInfo("AudioSystem: configured ALSA backend: rt-prio=0 "
                    "(prevents rtkit RLIMIT_RTTIME=200ms SIGKILL), "
                    "device=pulse, period_size=4096, periods=8 (~744ms buffer)");
        }
    }
#endif // !_WIN32

    m_device = alcOpenDevice(nullptr);
    if (!m_device) {
        logWarning("alcOpenDevice failed — no audio device available; running in silent mode");
        m_deviceLost.store(true);
        return;
    }
    m_deviceCreated = true;

    // Wrap device in DefaultAlcFunctions if no injection provided.
    DefaultAlcFunctions defaultAlc(m_device);
    m_alcFunctions = alcFunctions ? alcFunctions : &defaultAlc;

    // Build HRTF attribute list.
    ALCint attrs[] = {
        ALC_HRTF_SOFT, ALC_TRUE,
        0
    };
    m_context = alcCreateContext(m_device, attrs);
    if (!m_context) {
        alcCheckError_real(m_device, "alcCreateContext");
        throw std::runtime_error("alcCreateContext failed");
    }
    m_contextCreated = true;

    // MANDATORY: make context current on main thread BEFORE any AL call.
    if (alcMakeContextCurrent(m_context) == ALC_FALSE) {
        alcCheckError_real(m_device, "alcMakeContextCurrent");
        throw std::runtime_error("alcMakeContextCurrent failed");
    }
    m_contextMadeCurrent = true;

    // Verify HRTF status (optional — failure is a warning, not abort).
    if (alcIsExtensionPresent(m_device, "ALC_SOFT_HRTF")) {
        ALCint hrtfStatus = 0;
        alcGetIntegerv(m_device, ALC_HRTF_STATUS_SOFT, 1, &hrtfStatus);
        if (hrtfStatus != ALC_HRTF_ENABLED_SOFT) {
            logWarning("HRTF requested but not enabled. "
                       "Deploy default.mhr alongside the binary.");
        } else {
            logInfo("HRTF enabled.");
        }
    }

    // Log OpenAL Soft version for diagnostic.
    {
        const char* ver = alGetString(AL_VERSION);
        logInfo(std::string("OpenAL version: ") + (ver ? ver : "unknown"));
    }

    // MANDATORY: set distance model before any source creation.
    alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);
    alCheckError_real("alDistanceModel");

    // -----------------------------------------------------------------------
    // Step 1.5: Generate ALL 62 source handles on the main thread.
    // Must happen BEFORE EFX filter allocation (which calls alSourcei to bind
    // filters) and BEFORE thread launch.
    // -----------------------------------------------------------------------
    alGenSources(kTotalSources, reinterpret_cast<ALuint*>(m_sources));
    alCheckError_real("alGenSources");
    m_sourcesGenerated = true;

    // -----------------------------------------------------------------------
    // Setup stinger sources [55..56] — non-positional at construction.
    // sources[57] is idle in V1 (never acquired by any code path).
    // -----------------------------------------------------------------------
    for (int s : {static_cast<int>(StingerType::CRISIS),
                  static_cast<int>(StingerType::MILESTONE)}) {
        setupStingerSource(s);
    }

    // -----------------------------------------------------------------------
    // Setup stream sources [58..61] — non-positional.
    // -----------------------------------------------------------------------
    for (int i = kSFXPoolSize; i < kTotalSources; ++i) {
        setupStreamSource(i);
    }

    // Assign stream sources to AudioStream slots and generate AL buffers.
    for (int i = 0; i < kStreamSourceCount; ++i) {
        m_streams[i].sourceHandle = m_sources[kSFXPoolSize + i];
        // Generate 8 AL buffers per stream.
        alGenBuffers(AudioStream::kNumBuffers,
                     reinterpret_cast<ALuint*>(m_streams[i].buffers));
        alCheckError_real("alGenBuffers (stream)");
    }

    // -----------------------------------------------------------------------
    // Step 1.6: Enqueue pre-load commands (Pattern B: push_back to vector).
    // Audio thread will drain before signaling m_initDone.
    // -----------------------------------------------------------------------
    // Pre-load zone loop SFX (OGG, mono, 12–18 s).
    m_preloadQueue.push_back({SFX_ZONE_RESIDENTIAL,
                              assetPath("sfx_zone_residential.ogg"), true});
    m_preloadQueue.push_back({SFX_ZONE_COMMERCIAL,
                              assetPath("sfx_zone_commercial.ogg"),  true});
    m_preloadQueue.push_back({SFX_ZONE_INDUSTRIAL,
                              assetPath("sfx_zone_industrial.ogg"),  true});
    // Pre-load vehicle engine loops (OGG, mono, min 6 s).
    m_preloadQueue.push_back({SFX_VEHICLE_ENGINE_IDLE,
                              assetPath("sfx_vehicle_engine_idle.ogg"), true});
    m_preloadQueue.push_back({SFX_VEHICLE_ENGINE_MOVE,
                              assetPath("sfx_vehicle_engine_move.ogg"), true});
    // Pre-load stinger WAVs.
    m_preloadQueue.push_back({SFX_STINGER_CRISIS,
                              assetPath("stinger_crisis.wav"), false});
    m_preloadQueue.push_back({SFX_STINGER_MILESTONE,
                              assetPath("stinger_milestone.wav"), false});

    // Phase 10: Pre-load construction / zone-placement WAV SFX.
    m_preloadQueue.push_back({SFX_BUILD_PLACE,
                              assetPath("sfx_build_place.wav"), false});
    m_preloadQueue.push_back({SFX_BUILD_DEMOLISH,
                              assetPath("sfx_build_demolish.wav"), false});
    m_preloadQueue.push_back({SFX_ROAD_BUILD,
                              assetPath("sfx_road_build.wav"), false});
    m_preloadQueue.push_back({SFX_EARTHWORKS,
                              assetPath("sfx_earthworks.wav"), false});
    m_preloadQueue.push_back({SFX_ZONE_UPGRADE,
                              assetPath("sfx_zone_upgrade.wav"), false});
    m_preloadQueue.push_back({SFX_SERVICE_DEGRADE,
                              assetPath("sfx_service_degrade.wav"), false});

    // Phase 10: Pre-load budget / economy WAV SFX.
    m_preloadQueue.push_back({SFX_BUDGET_WARN,
                              assetPath("sfx_budget_warn.wav"), false});
    m_preloadQueue.push_back({SFX_LOAN_ISSUED,
                              assetPath("sfx_loan_issued.wav"), false});

    // Phase 10: Pre-load utility / service alert WAV SFX.
    // SFX_POWER_OUT and SFX_WATER_OUT are non-positional (AL_SOURCE_RELATIVE=AL_TRUE).
    // SFX_FIRE_ALERT and SFX_POLICE_ALERT are positional (AL_SOURCE_RELATIVE=AL_FALSE)
    // — they benefit from EFX occlusion and must NOT be added to the EFX bypass list.
    m_preloadQueue.push_back({SFX_POWER_OUT,
                              assetPath("sfx_power_out.wav"), false});
    m_preloadQueue.push_back({SFX_WATER_OUT,
                              assetPath("sfx_water_out.wav"), false});
    m_preloadQueue.push_back({SFX_FIRE_ALERT,
                              assetPath("sfx_fire_alert.wav"), false});
    m_preloadQueue.push_back({SFX_POLICE_ALERT,
                              assetPath("sfx_police_alert.wav"), false});

    // Phase 10: Pre-load vehicle horn and intersection tick WAV SFX.
    m_preloadQueue.push_back({SFX_VEHICLE_HORN,
                              assetPath("sfx_vehicle_horn.wav"), false});
    m_preloadQueue.push_back({SFX_INTERSECTION_TICK,
                              assetPath("sfx_intersection_tick.wav"), false});

    // Phase 10: Pre-load UI WAV sounds (non-positional, AL_SOURCE_RELATIVE=AL_TRUE,
    // EFX bypass required — AL_DIRECT_FILTER=AL_FILTER_NULL at playback).
    m_preloadQueue.push_back({UI_CLICK,
                              assetPath("ui_click.wav"), false});
    m_preloadQueue.push_back({UI_TOAST,
                              assetPath("ui_toast.wav"), false});
    m_preloadQueue.push_back({UI_MENU_OPEN,
                              assetPath("ui_menu_open.wav"), false});
    m_preloadQueue.push_back({UI_MENU_CLOSE,
                              assetPath("ui_menu_close.wav"), false});

    // -----------------------------------------------------------------------
    // Step 2: Load ALC_EXT_thread_local_context — HARD REQUIREMENT.
    // Use IAlcFunctions seam (real or mocked via injection).
    // -----------------------------------------------------------------------
    m_fnSetThreadCtx = reinterpret_cast<FnSetThreadCtx>(
        m_alcFunctions->getProcAddress("alcSetThreadContext"));
    if (!m_fnSetThreadCtx) {
        throw std::runtime_error(
            "ALC_EXT_thread_local_context required — "
            "alcGetProcAddress(\"alcSetThreadContext\") returned null. "
            "Upgrade OpenAL Soft to ≥ 1.14.");
    }

    // -----------------------------------------------------------------------
    // Step 2.5: EFX extension check + filter allocation (main thread, pre-launch).
    // -----------------------------------------------------------------------
    allocateEFXFilters();

    // -----------------------------------------------------------------------
    // Step 3: Initialize m_occlusionGainTarget[] to 1.0f BEFORE thread launch.
    // The audio thread reads these on its very first updateOcclusion() call.
    // -----------------------------------------------------------------------
    for (auto& t : m_occlusionGainTarget) {
        t.store(1.0f, std::memory_order_relaxed);
    }

    // -----------------------------------------------------------------------
    // Step 4: Set m_useThreadLocalCtx = true and launch audio thread.
    // Extension confirmed non-null above; thread launch is safe.
    // -----------------------------------------------------------------------
    m_useThreadLocalCtx = true;
    m_audioThread = std::thread(&AudioSystem::audioThreadFunc, this);

    // -----------------------------------------------------------------------
    // Step 5: Wait for audio thread to signal init complete (5 s timeout).
    // Predicate form — unique_lock must be held before wait_for.
    // -----------------------------------------------------------------------
    {
        std::unique_lock<std::mutex> lk(m_initMutex);
        bool notified = m_initCV.wait_for(lk, std::chrono::seconds(5),
                                          [this]{ return m_initDone; });
        if (!notified || m_initError) {
            // Thread failed to init — mark stop so it exits, then join before throw.
            m_stopThread.store(true);
            m_streamCV.notify_all();
            if (m_audioThread.joinable()) m_audioThread.join();
            throw std::runtime_error("AudioSystem audio thread initialization failed");
        }
    }
}

// ---------------------------------------------------------------------------
// AudioSystem destructor — mandatory shutdown order per audio-thread-shutdown.md
// ---------------------------------------------------------------------------
AudioSystem::~AudioSystem() {
    // Step 1: signal audio thread to stop.
    m_stopThread.store(true);

    // Step 2: wake streaming thread immediately.
    m_streamCV.notify_all();

    // Step 3: join audio thread (guard with joinable()).
    if (m_audioThread.joinable()) {
        m_audioThread.join();
    }

    // Step 3.5: Re-bind context to main thread BEFORE any AL cleanup.
    // After the audio thread has joined, its thread-local context no longer applies.
    // The main thread needs a current context for all AL resource deletion below.
    if (m_context && m_useThreadLocalCtx) {
        alcMakeContextCurrent(m_context);
    }

    // Guard all AL cleanup behind m_contextMadeCurrent — if the context was never
    // made current we have no valid AL state to clean up.
    if (!m_contextMadeCurrent) goto cleanup_alc;

    // -----------------------------------------------------------------------
    // Step 4: Streaming source stop + query + unqueue.
    // Never hardcode buffer count — always query AL_BUFFERS_QUEUED.
    // -----------------------------------------------------------------------
    if (m_sourcesGenerated) {
        for (int i = 0; i < kStreamSourceCount; ++i) {
            ALuint src = static_cast<ALuint>(m_streams[i].sourceHandle);
            alSourceStop(src);
            ALint queued = 0;
            alGetSourcei(src, AL_BUFFERS_QUEUED, &queued);
            if (queued > 0) {
                std::vector<ALuint> tmp(static_cast<size_t>(queued));
                alSourceUnqueueBuffers(src, queued, tmp.data());
            }
        }

        // Delete stream AL buffers.
        for (int i = 0; i < kStreamSourceCount; ++i) {
            alDeleteBuffers(AudioStream::kNumBuffers,
                            reinterpret_cast<ALuint*>(m_streams[i].buffers));
        }

        // Close OGG stream handles.
        for (int i = 0; i < kStreamSourceCount; ++i) {
            if (m_streams[i].vf && m_streams[i].isOpen) {
                AudioStreamUtils::closeOGG(m_streams[i].vf);
                m_streams[i].isOpen = false;
            }
            delete m_streams[i].vf;
            m_streams[i].vf = nullptr;
        }

        // -----------------------------------------------------------------------
        // Step 4a: SFX pool source stop + detach static buffer.
        // Covers sources[0..kSFXPoolSize-1] = [0..57].
        // -----------------------------------------------------------------------
        for (int i = 0; i < kSFXPoolSize; ++i) {
            ALuint src = static_cast<ALuint>(m_sources[i]);
            alSourceStop(src);
            alSourcei(src, AL_BUFFER, 0);
        }

        // Delete pre-loaded static buffers.
        for (auto& kv : m_preloadedBuffers) {
            if (kv.second != 0) {
                ALuint buf = static_cast<ALuint>(kv.second);
                alDeleteBuffers(1, &buf);
            }
        }
        m_preloadedBuffers.clear();
    }

    // -----------------------------------------------------------------------
    // Step 4b: EFX filter cleanup.
    // Guard: m_efxAllocationAttempted (not m_efxAvailable — see spec).
    // Loop bound: kEvictableSFXCount (55) only — stinger/stream slots have no filters.
    // -----------------------------------------------------------------------
    if (m_efxAllocationAttempted && m_fnDeleteFilters) {
        for (int i = 0; i < kEvictableSFXCount; ++i) {
            if (m_occlusionFilter[i] != AL_FILTER_NULL) {
                m_fnDeleteFilters(1, &m_occlusionFilter[i]);
                m_occlusionFilter[i] = AL_FILTER_NULL;
            }
        }
    }

    // -----------------------------------------------------------------------
    // Step 5: Delete all AL source handles.
    // -----------------------------------------------------------------------
    if (m_sourcesGenerated) {
        alDeleteSources(kTotalSources, reinterpret_cast<ALuint*>(m_sources));
    }

cleanup_alc:
    // -----------------------------------------------------------------------
    // Steps 6–8: Context + device teardown.
    // -----------------------------------------------------------------------
    if (m_contextMadeCurrent || m_contextCreated) {
        alcMakeContextCurrent(nullptr);
    }
    if (m_contextCreated && m_context) {
        alcDestroyContext(m_context);
        m_context = nullptr;
    }
    if (m_deviceCreated && m_device) {
        alcCloseDevice(m_device);
        m_device = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Audio thread entry point.
// ---------------------------------------------------------------------------
void AudioSystem::audioThreadFunc() {
    // Step 1: Set thread-local context — FIRST action before any AL call.
    // m_fnSetThreadCtx is guaranteed non-null (constructor threw if null).
    if (m_fnSetThreadCtx(m_context) == ALC_FALSE) {
        std::lock_guard<std::mutex> lk(m_initMutex);
        m_initError = true;
        m_initDone  = true;
        m_initCV.notify_one();
        return;
    }

    // PRE-LOAD PHASE — drain preload queue before signaling init complete.
    // Pattern B: range-for over the vector (no tryPop needed).
    for (auto& cmd : m_preloadQueue) {
        processPreloadCommand(cmd);
    }

    // MANDATORY: initialize m_lastDuckWakeTime BEFORE notify_one().
    // If initialized after, the constructor unblocks and main thread may call
    // triggerStinger() before m_lastDuckWakeTime is written — epoch-sized dt.
    m_lastDuckWakeTime = m_clock->nowSeconds();

    // Signal constructor that initialization succeeded.
    {
        std::lock_guard<std::mutex> lk(m_initMutex);
        m_initDone = true;
    }
    m_initCV.notify_one();

    // Streaming loop — wake every 10 ms.
    while (true) {
        {
            std::unique_lock<std::mutex> lock(m_streamMutex);
            m_streamCV.wait_for(lock, std::chrono::milliseconds(10),
                                [this]{ return m_stopThread.load(); });
        }

        // CRITICAL: check m_stopThread FIRST before any AL call.
        if (m_stopThread.load()) break;

        // Check ALC_EXT_disconnect: if the device was lost (e.g. PulseAudio
        // broken pipe), stop the audio thread immediately to prevent flooding
        // the dead backend and provoking an external SIGKILL.
        {
            ALCint connected = 1;
            alcGetIntegerv(m_device, ALC_CONNECTED, 1, &connected);
            if (!connected) {
                logInfo("AudioSystem: device disconnected — stopping audio thread");
                m_deviceLost.store(true);
                m_stopThread.store(true);
                break;
            }
        }

        // Compute real-time dt using IClock (not fixed 0.01f).
        // dt is passed to both updateStreams() and updateDuckState() so crossfade
        // timers and duck state machine use the same real-time measurement.
        double now = m_clock->nowSeconds();
        float  dt  = static_cast<float>(now - m_lastDuckWakeTime);
        m_lastDuckWakeTime = now;

        // Catch OpenAL runtime errors (e.g. "Broken pipe" when the audio
        // backend disconnects) so the audio thread degrades gracefully instead
        // of propagating an uncaught exception which would call std::terminate
        // and kill the entire process.
        try {
            updateStreams(dt);
            updateOcclusion();
            updateDuckState(dt);
            updateVehicleEngines();
            cleanupFinishedSFX();
        } catch (const std::exception& e) {
            logError(std::string("AudioSystem: audio thread error, disabling audio: ") + e.what());
            m_deviceLost.store(true);
            m_stopThread.store(true);
            break;
        }
    }

    // Clear the thread-local OpenAL context before the thread exits.
    // Omitting this causes OpenAL Soft to log:
    //   "[ALSOFT] (EE) Context current for thread being destroyed!"
    m_fnSetThreadCtx(nullptr);
}

// ---------------------------------------------------------------------------
// processPreloadCommand — called on audio thread during pre-load phase.
// ---------------------------------------------------------------------------
void AudioSystem::processPreloadCommand(const PreloadCommand& cmd) {
    if (cmd.soundId == SFX_INVALID) return;

    const std::string& path = cmd.filePath;
    bool isOGG = (path.size() >= 4 &&
                  path.substr(path.size() - 4) == ".ogg");
    bool isWAV = (path.size() >= 4 &&
                  path.substr(path.size() - 4) == ".wav");

    if (!isOGG && !isWAV) {
        logError("processPreloadCommand: unsupported format: " + path);
        return;
    }

    ALuint buf = 0;

    if (isWAV) {
        // WAV: Short SFX / UI / stinger (PCM).
        buf = static_cast<ALuint>(loadWAV(path));
        if (buf == 0) {
            logError("Failed to load WAV: " + path);
            return;
        }
        m_preloadedBuffers[cmd.soundId] = static_cast<unsigned int>(buf);
        return;
    }

    // OGG Looping game SFX (zone loops / vehicle engine).
    OggVorbis_File vf;
    memset(&vf, 0, sizeof(vf));
    if (!AudioStreamUtils::openOGG(path, &vf)) {
        logError("Failed to open OGG: " + path);
        return;
    }

    int sr = 0, channels = 0;
    AudioStreamUtils::getInfo(&vf, sr, channels);

    // OGG header validation.
    bool isZoneLoop    = (cmd.soundId >= SFX_ZONE_RESIDENTIAL &&
                          cmd.soundId <= SFX_ZONE_INDUSTRIAL);
    bool isVehicleLoop = (cmd.soundId == SFX_VEHICLE_ENGINE_IDLE ||
                          cmd.soundId == SFX_VEHICLE_ENGINE_MOVE);

    if (isZoneLoop || isVehicleLoop) {
        // Must be 44100 Hz mono.
        if (sr != 44100 || channels != 1) {
            logError("Zone loop / vehicle OGG header mismatch "
                     "(expected 44100 Hz mono): " + path);
            AudioStreamUtils::closeOGG(&vf);
            // return kInvalidSoundHandle equivalent — do NOT throw.
            return;
        }
    }

    // Zone loop duration cap (IDs 17–19): <= 18 s.
    if (isZoneLoop) {
        int64_t totalFrames = AudioStreamUtils::getTotalFrames(&vf);
        float dur = (sr > 0) ? static_cast<float>(totalFrames) / static_cast<float>(sr)
                              : 0.f;
        if (dur > kZoneLoopMaxPreloadDurationSeconds) {
            logError("Zone loop exceeds max pre-load duration (" +
                     std::to_string(dur) + "s > " +
                     std::to_string(kZoneLoopMaxPreloadDurationSeconds) +
                     "s): " + path);
            AudioStreamUtils::closeOGG(&vf);
            return;
        }
    }

    // Fully decode OGG into a PCM buffer.
    int64_t totalFrames = AudioStreamUtils::getTotalFrames(&vf);
    if (totalFrames <= 0) {
        logError("OGG has no frames: " + path);
        AudioStreamUtils::closeOGG(&vf);
        return;
    }

    std::vector<int16_t> pcm(static_cast<size_t>(totalFrames) *
                              static_cast<size_t>(channels));

    int decoded = AudioStreamUtils::decodeFrames(
        &vf, pcm.data(), static_cast<int>(totalFrames), channels);

    AudioStreamUtils::closeOGG(&vf);

    if (decoded <= 0) {
        logError("OGG decode failed: " + path);
        return;
    }

    // Choose AL format.
    ALenum format = (channels == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;

    alGenBuffers(1, &buf);
    alCheckError_real("alGenBuffers (preload)");

    ALsizei byteCount = static_cast<ALsizei>(decoded) *
                        static_cast<ALsizei>(channels) *
                        static_cast<ALsizei>(sizeof(int16_t));
    alBufferData(buf, format, pcm.data(), byteCount, static_cast<ALsizei>(sr));
    alCheckError_real("alBufferData (preload)");

    // Set AL_SOFT_loop_points if available and looping.
    if (cmd.looping && alIsExtensionPresent("AL_SOFT_loop_points")) {
        auto alBufferiv = reinterpret_cast<LPALBUFFERIV>(
                              alGetProcAddress("alBufferiv"));
        if (alBufferiv) {
            ALint loopPts[2] = {0, static_cast<ALint>(decoded)};
            alBufferiv(buf, AL_LOOP_POINTS_SOFT, loopPts);
        }
    }

    m_preloadedBuffers[cmd.soundId] = static_cast<unsigned int>(buf);
}

// ---------------------------------------------------------------------------
// WAV loader (simple PCM reader; supports 16-bit mono/stereo).
// ---------------------------------------------------------------------------
unsigned int AudioSystem::loadWAV(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        logError("loadWAV: cannot open: " + path);
        return 0;
    }

    // Minimal RIFF/WAV parser.
    char   riff[4]; uint32_t fileSize; char wave[4];
    if (fread(riff,    1, 4, f) != 4 || memcmp(riff, "RIFF", 4) != 0) { fclose(f); return 0; }
    if (fread(&fileSize, 4, 1, f) != 1) { fclose(f); return 0; }
    if (fread(wave,    1, 4, f) != 4 || memcmp(wave, "WAVE", 4) != 0) { fclose(f); return 0; }

    uint16_t audioFmt = 0, numChannels = 0, blockAlign = 0, bitsPerSample = 0;
    uint32_t sampleRate = 0;
    std::vector<uint8_t> pcmData;

    while (true) {
        char   chunkId[4];
        uint32_t chunkSize = 0;
        if (fread(chunkId,    1, 4, f) != 4) break;
        if (fread(&chunkSize, 4, 1, f) != 1) break;

        if (memcmp(chunkId, "fmt ", 4) == 0) {
            uint32_t byteRate = 0;
            fread(&audioFmt,     2, 1, f);
            fread(&numChannels,  2, 1, f);
            fread(&sampleRate,   4, 1, f);
            fread(&byteRate,     4, 1, f);
            fread(&blockAlign,   2, 1, f);
            fread(&bitsPerSample,2, 1, f);
            // Skip any extra fmt bytes.
            if (chunkSize > 16) {
                fseek(f, static_cast<long>(chunkSize - 16), SEEK_CUR);
            }
        } else if (memcmp(chunkId, "data", 4) == 0) {
            pcmData.resize(chunkSize);
            size_t read = fread(pcmData.data(), 1, chunkSize, f);
            if (read != chunkSize) pcmData.resize(read);
            break;
        } else {
            fseek(f, static_cast<long>(chunkSize), SEEK_CUR);
        }
    }
    fclose(f);

    if (pcmData.empty() || audioFmt != 1 || bitsPerSample != 16) {
        logError("loadWAV: unsupported format in: " + path);
        return 0;
    }

    ALenum format = (numChannels == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;

    ALuint buf = 0;
    alGenBuffers(1, &buf);
    alCheckError_real("alGenBuffers (WAV)");
    alBufferData(buf, format, pcmData.data(),
                 static_cast<ALsizei>(pcmData.size()),
                 static_cast<ALsizei>(sampleRate));
    alCheckError_real("alBufferData (WAV)");

    return static_cast<unsigned int>(buf);
}

// ---------------------------------------------------------------------------
// openStreamOGG — opens an OGG stream for the given slot.
// ---------------------------------------------------------------------------
bool AudioSystem::openStreamOGG(int streamSlot, const std::string& path,
                                  bool isMusicStem) {
    if (streamSlot < 0 || streamSlot >= kStreamSourceCount) return false;

    AudioStream& s = m_streams[streamSlot];

    // Close any existing stream.
    // IMPORTANT: flush the AL buffer queue BEFORE resetting m_samplesQueued.
    // If this slot has AL buffers attached (queued but not yet processed), and we
    // only reset m_samplesQueued without unqueuing them, refillStream's round-robin
    // index (m_samplesQueued % kNumBuffers) will point to buffers that are still
    // attached — causing repeated AL_INVALID_OPERATION from alBufferData.
    if (s.isOpen && s.vf) {
        AudioStreamUtils::closeOGG(s.vf);
        s.isOpen = false;

        // Flush the AL source: stop it and unqueue any remaining AL buffers so
        // the buffer pool is fully free before the new stream starts filling.
        ALuint src = static_cast<ALuint>(s.sourceHandle);
        alSourceStop(src);
        ALint queued = 0;
        alGetSourcei(src, AL_BUFFERS_QUEUED, &queued);
        if (queued > 0) {
            std::vector<ALuint> tmp(static_cast<size_t>(queued));
            alSourceUnqueueBuffers(src, queued, tmp.data());
        }
        s.m_samplesQueued   = 0;
        s.m_nextBarBoundary = 0;
    }
    if (!s.vf) {
        s.vf = new OggVorbis_File;
        memset(s.vf, 0, sizeof(OggVorbis_File));
    }

    if (!AudioStreamUtils::openOGG(path, s.vf)) {
        logError("openStreamOGG: cannot open: " + path);
        return false;
    }

    int sr = 0, channels = 0;
    AudioStreamUtils::getInfo(s.vf, sr, channels);

    // Validate header (44100 Hz stereo for all streaming sources).
    if (sr != 44100 || channels != 2) {
        logError("Stream OGG header mismatch (expected 44100 Hz stereo): " + path);
        AudioStreamUtils::closeOGG(s.vf);
        return false;
    }

    s.isMusicStem            = isMusicStem;
    s.m_samplesQueued        = 0;
    s.m_nextBarBoundary      = 0;
    s.m_intentionallyStopped = false;
    s.crossfadeGain          = 1.0f;
    // isOpen stays false until pre-fill and sidecar loading succeed.

    if (isMusicStem) {
        if (!loadMusicSidecar(path, s.bpm, s.beatsPerBar)) {
            // Missing/invalid sidecar for music stem is a hard error.
            AudioStreamUtils::closeOGG(s.vf);
            throw std::runtime_error("Missing or invalid sidecar for: " + path);
        }
    }

    // Pre-fill all kNumBuffers AL buffer slots before marking the stream open.
    //
    // Without pre-filling, the source is in AL_STOPPED on the first audio thread
    // wake (no buffers were queued before alSourcePlay was called by the caller).
    // This triggers starvation recovery on every stream at startup — 4 rapid
    // alSourcePlay calls that can overwhelm the PulseAudio backend.
    //
    // The audio thread checks s.isOpen and skips closed slots, so we can write
    // AL buffers here without holding m_streamMutex or racing with the thread.
    {
        ALuint    src = static_cast<ALuint>(s.sourceHandle);
        ALenum    fmt = (channels == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;

        for (int b = 0; b < AudioStream::kNumBuffers; ++b) {
            std::vector<int16_t> pcm(
                static_cast<size_t>(AudioStream::kSamplesPerBuffer) *
                static_cast<size_t>(channels));

            int frames = AudioStreamUtils::decodeFrames(
                s.vf, pcm.data(),
                static_cast<int>(AudioStream::kSamplesPerBuffer), channels);

            if (frames == 0) {
                // EOF on short file — seek to start and retry.
                if (!AudioStreamUtils::seekToStart(s.vf)) break;
                frames = AudioStreamUtils::decodeFrames(
                    s.vf, pcm.data(),
                    static_cast<int>(AudioStream::kSamplesPerBuffer), channels);
            }
            if (frames <= 0) break;

            ALuint  bufHandle = static_cast<ALuint>(s.buffers[b]);
            ALsizei byteCount = static_cast<ALsizei>(frames) *
                                static_cast<ALsizei>(channels) *
                                static_cast<ALsizei>(sizeof(int16_t));

            alGetError();   // clear any stale error
            alBufferData(bufHandle, fmt, pcm.data(), byteCount,
                         static_cast<ALsizei>(sr));
            if (alGetError() != AL_NO_ERROR) break;

            alSourceQueueBuffers(src, 1, &bufHandle);
            alGetError();   // consume — non-fatal if queue fails
            s.m_samplesQueued += static_cast<uint64_t>(frames);
        }
    }

    s.isOpen = true;
    return true;
}

// ---------------------------------------------------------------------------
// closeStream — stops and closes a stream slot.
// MUST be called while m_streamMutex is held by the caller.
// -----------------------------------------------------------------------
void AudioSystem::closeStream(int slot) {
    if (slot < 0 || slot >= kStreamSourceCount) return;
    AudioStream& s = m_streams[slot];
    if (!s.isOpen) return;

    // Set intentionally stopped BEFORE alSourceStop (same lock scope per spec).
    s.m_intentionallyStopped = true;
    alSourceStop(static_cast<ALuint>(s.sourceHandle));

    // Unqueue any queued buffers.
    ALint queued = 0;
    alGetSourcei(static_cast<ALuint>(s.sourceHandle), AL_BUFFERS_QUEUED, &queued);
    if (queued > 0) {
        std::vector<ALuint> tmp(static_cast<size_t>(queued));
        alSourceUnqueueBuffers(static_cast<ALuint>(s.sourceHandle),
                               queued, tmp.data());
    }

    if (s.vf) {
        AudioStreamUtils::closeOGG(s.vf);
        s.isOpen = false;
    }

    s.m_samplesQueued   = 0;
    s.m_nextBarBoundary = 0;
}

// ---------------------------------------------------------------------------
// refillStream — decode PCM and queue buffers for one stream slot.
// Split-lock pattern: AL queue ops under m_streamMutex; decode outside.
// Returns number of buffers queued.
// ---------------------------------------------------------------------------
int AudioSystem::refillStream(int slot) {
    if (slot < 0 || slot >= kStreamSourceCount) return 0;
    AudioStream& s = m_streams[slot];
    if (!s.isOpen || !s.vf) return 0;

    ALuint src    = static_cast<ALuint>(s.sourceHandle);
    int    bufLen = static_cast<int>(AudioStream::kNumBuffers);

    // --- Split-lock step 1: read AL_BUFFERS_QUEUED and unqueue processed ---
    // Read AL_BUFFERS_QUEUED ONCE per wake BEFORE unqueuing (spec requirement).
    ALint buffersQueued  = 0;
    ALint buffersProcessed = 0;
    {
        std::lock_guard<std::mutex> lk(m_streamMutex);
        alGetSourcei(src, AL_BUFFERS_QUEUED,    &buffersQueued);
        alGetSourcei(src, AL_BUFFERS_PROCESSED, &buffersProcessed);

        if (buffersProcessed > 0) {
            std::vector<ALuint> tmp(static_cast<size_t>(buffersProcessed));
            alSourceUnqueueBuffers(src, buffersProcessed, tmp.data());
        }
    }
    // buffersQueued captured before unqueue — used for samplesPlayed estimate.

    // Determine how many buffers we can fill.
    int toFill = bufLen - (buffersQueued - buffersProcessed);
    if (toFill <= 0) return 0;

    // --- Split-lock step 2: decode outside mutex ---
    int   queued = 0;
    int   sr     = 44100;
    int   channels = 2;
    AudioStreamUtils::getInfo(s.vf, sr, channels);

    for (int b = 0; b < toFill && b < bufLen; ++b) {
        std::vector<int16_t> pcmBuf(AudioStream::kSamplesPerBuffer *
                                    static_cast<uint32_t>(channels));

        int framesDecoded = AudioStreamUtils::decodeFrames(
            s.vf, pcmBuf.data(),
            static_cast<int>(AudioStream::kSamplesPerBuffer),
            channels);

        if (framesDecoded < 0) {
            logError("OGG decode error in stream slot " + std::to_string(slot));
            break;
        }

        if (framesDecoded == 0) {
            // EOF — check if this is the main-menu music slot; if so, switch
            // to the alternate variant instead of looping the same file.
            bool isMainMenuSlot = false;
            {
                std::lock_guard<std::mutex> lk(m_streamMutex);
                isMainMenuSlot = (slot == m_musicActiveSlot) && m_mainMenuMusicLooping;
            }

            if (isMainMenuSlot) {
                // Select next variant (random-excluding-repeat).
                int selectedVariant = 1;
                std::string newPath;
                {
                    std::lock_guard<std::mutex> lk(m_streamMutex);
                    std::vector<int> candidates;
                    for (int v : {1, 2}) {
                        if (v != m_lastMainMenuVariant) candidates.push_back(v);
                    }
                    std::uniform_int_distribution<int> dist(
                        0, static_cast<int>(candidates.size()) - 1);
                    selectedVariant = candidates[dist(m_rng)];
                    m_lastMainMenuVariant = selectedVariant;
                }
                MusicTrackId trackId =
                    (selectedVariant == 1) ? MUSIC_MAIN_MENU_01 : MUSIC_MAIN_MENU_02;
                std::string filename = musicTrackFilename(trackId);
                newPath = assetPath(filename);

                // Close current stream, open the new variant, and start playback.
                // All AL calls under m_streamMutex as in transitionToMainMenu().
                std::string pendingLogError2;
                {
                    std::lock_guard<std::mutex> lk(m_streamMutex);
                    closeStream(slot);
                    if (openStreamOGG(slot, newPath, /*isMusicStem=*/true)) {
                        m_streams[slot].crossfadeGain = 1.0f;
                        ALuint src2 = static_cast<ALuint>(m_streams[slot].sourceHandle);
                        alSourcei(src2, AL_LOOPING, AL_FALSE);
                        alCheckError_real("refillStream:mainmenu_eof_looping");
                        alSourcePlay(src2);
                        alCheckError_real("refillStream:mainmenu_eof_play");
                    } else {
                        pendingLogError2 = "refillStream: failed to open main-menu variant " +
                                           std::to_string(selectedVariant);
                    }
                }
                if (!pendingLogError2.empty()) { logError(pendingLogError2); }
                // Return 0 buffers this wake; audio thread will refill on next wake
                // from the newly opened stream.
                break;
            }

            // Not the main-menu slot — generic loop: seek to start and re-decode.
            if (!AudioStreamUtils::seekToStart(s.vf)) {
                logError("OGG seek to start failed in stream slot " +
                         std::to_string(slot));
                break;
            }
            // Decode from the start.
            framesDecoded = AudioStreamUtils::decodeFrames(
                s.vf, pcmBuf.data(),
                static_cast<int>(AudioStream::kSamplesPerBuffer),
                channels);
            if (framesDecoded <= 0) break;
        }

        ALenum format = (channels == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
        ALsizei byteCount = static_cast<ALsizei>(framesDecoded) *
                            static_cast<ALsizei>(channels) *
                            static_cast<ALsizei>(sizeof(int16_t));

        // Pick a buffer handle from the stream's pool (round-robin by queued count).
        // Use a simple index: which of the 8 buffers is next in rotation.
        // We track this by total buffers queued modulo 8.
        int bufIdx = static_cast<int>(
            (s.m_samplesQueued / AudioStream::kSamplesPerBuffer) % AudioStream::kNumBuffers);
        ALuint bufHandle = static_cast<ALuint>(s.buffers[bufIdx]);

        // --- Split-lock step 3: queue under mutex ---
        // Disjoint-use constraint: logError/logWarning acquire m_logMutex internally,
        // so they MUST NOT be called while m_streamMutex is held.  We capture any
        // pending log message into a local std::string inside the lock, then call the
        // log helper after the lock_guard goes out of scope.
        std::string pendingLogError;
        std::string pendingLogWarning;
        {
            std::lock_guard<std::mutex> lk(m_streamMutex);

            // Clear any stale AL error before alBufferData — a buffer that is still
            // attached to the source (queued but not yet unqueued by the previous step)
            // causes AL_INVALID_OPERATION here.  Stale errors must be cleared before
            // the check below so that the error is attributed to alBufferData (the
            // actual failing call), not to a later alSourcef call in updateStreams().
            alGetError();
            alBufferData(bufHandle, format, pcmBuf.data(), byteCount,
                         static_cast<ALsizei>(sr));
            {
                ALenum bufErr = alGetError();
                if (bufErr != AL_NO_ERROR) {
                    // Buffer still attached to source queue — skip this slot.
                    // m_samplesQueued is NOT incremented so the same bufIdx is retried
                    // on the next wake after the buffer has been processed and unqueued.
                    pendingLogError = "refillStream: alBufferData failed slot=" +
                                      std::to_string(slot) + " bufIdx=" +
                                      std::to_string(bufIdx);
                    // Lock released at end of scope; log called below.
                }
            }
            if (pendingLogError.empty()) {
                alSourceQueueBuffers(src, 1, &bufHandle);
                alGetError();  // consume — queue error is non-fatal; starvation recovery handles restart

                s.m_samplesQueued += static_cast<uint64_t>(framesDecoded);
                ++queued;

                // Starvation recovery: if source stopped unintentionally, restart it.
                ALint state = AL_STOPPED;
                alGetSourcei(src, AL_SOURCE_STATE, &state);
                if (state == AL_STOPPED && !s.m_intentionallyStopped) {
                    pendingLogWarning = "Stream starvation recovery — restarting source " +
                                        std::to_string(slot);
                    alSourcePlay(src);
                    // Do NOT reset m_samplesQueued here.  It serves as the round-robin
                    // buffer index: (m_samplesQueued / kSamplesPerBuffer) % kNumBuffers.
                    // Resetting it mid-loop causes the next iteration to compute an index
                    // that collides with the buffer just queued in b==0, making
                    // alBufferData fail (buffer still attached).  Only one buffer ends up
                    // queued (~371 ms), the source starvates again next wake, and the
                    // recovery fires every 10 ms — flooding PulseAudio until SIGKILL.
                    // Reset bar-boundary only so it is recomputed after recovery.
                    s.m_nextBarBoundary = 0;
                }
            }
        }
        // Log outside the lock (disjoint-use constraint).
        if (!pendingLogError.empty())   { logError(pendingLogError);     break; }
        if (!pendingLogWarning.empty()) { logWarning(pendingLogWarning); }
    }

    // --- Bar-boundary tracking ---
    // Read buffersQueued again (after any requeue) for accurate samplesPlayed estimate.
    {
        std::lock_guard<std::mutex> lk(m_streamMutex);
        alGetSourcei(src, AL_BUFFERS_QUEUED, &buffersQueued);
    }

    if (s.isMusicStem) {
        // Bootstrap: compute first bar boundary once after stream start.
        if (s.m_nextBarBoundary == 0 && s.m_samplesQueued > 0) {
            // computeNextBarBoundary uses buffersQueued as captured before unqueue.
            uint64_t spb = static_cast<uint64_t>(
                (44100.0 * 60.0 / s.bpm) * s.beatsPerBar);
            uint64_t sp  = AudioStream::computeSamplesPlayed(
                s.m_samplesQueued, buffersQueued);
            s.m_nextBarBoundary = ((sp / spb) + 1) * spb;
        }

        // Bar boundary tracking — advance m_nextBarBoundary as playback progresses.
        // Phase 10: The intensity crossfade is driven from updateStreams() using the
        // wall-clock wake interval accumulator (kWakeInterval).  The bar boundary
        // counter here ensures the bootstrap completes and keeps m_nextBarBoundary
        // current for future use (e.g. post-V1 beat-synchronized stingers).
        uint64_t sp = AudioStream::computeSamplesPlayed(s.m_samplesQueued, buffersQueued);
        if (sp >= s.m_nextBarBoundary && s.m_nextBarBoundary > 0) {
            // Bar boundary crossed — advance to the NEXT boundary.
            uint64_t spb = static_cast<uint64_t>(
                (44100.0 * 60.0 / s.bpm) * s.beatsPerBar);
            s.m_nextBarBoundary = ((sp / spb) + 1) * spb;
        }
    }

    return queued;
}

// ---------------------------------------------------------------------------
// applyCrossfadeGains — constant-power crossfade (cos/sin curve).
// t = 0: outgoing at cos(0)=1 (full), incoming at sin(0)=0 (silent).
// t = 1: outgoing at cos(π/2)=0 (silent), incoming at sin(π/2)=1 (full).
// ---------------------------------------------------------------------------
/*static*/ void AudioSystem::applyCrossfadeGains(AudioStream& outStream,
                                                   AudioStream& inStream,
                                                   float t) {
    // 3.14159265358979323846f — avoids M_PI which is POSIX-only (not in C++ standard).
    constexpr float kPi = 3.14159265358979323846f;
    // Clamp t to [0, 1] to guard floating-point rounding at boundaries.
    const float tc = std::max(0.0f, std::min(1.0f, t));
    outStream.crossfadeGain = std::cos(tc * kPi * 0.5f);
    inStream.crossfadeGain  = std::sin(tc * kPi * 0.5f);
}

// ---------------------------------------------------------------------------
// beginIntensityCrossfade — start a beat-boundary crossfade to the stem pair
// matching the given intensity tier (called on audio thread).
//
// Time-of-day forced-Calm override: when m_currentTimeOfDay != DAY, the
// effective intensity is forced to CALM regardless of the requested tier.
// This is applied here (audio thread) — CitySimulation does NOT suppress calls.
//
// Minimum hold: if a crossfade completed less than kMusicCrossfadeDurationSeconds
// ago, the new crossfade is deferred until the next bar boundary check fires it.
// In practice we start immediately on the bar boundary; the spec minimum-hold
// is satisfied by the fact that bar boundaries are at ~2.67 s intervals (90BPM,
// 4/4) and the crossfade itself is 4 s — so two crossfades cannot overlap
// provided we complete before queuing another.
// ---------------------------------------------------------------------------
void AudioSystem::beginIntensityCrossfade(MusicIntensity intensity) {
    // Time-of-day forced-Calm override (DUSK/NIGHT/DAWN → CALM only).
    MusicIntensity effective = intensity;
    if (static_cast<TimeOfDay>(m_currentTimeOfDay.load(std::memory_order_relaxed)) != TimeOfDay::DAY) {
        effective = MusicIntensity::CALM;
    }

    // Select the target MusicTrackId.  Alternate between _01 and _02 variants
    // each crossfade by checking which slot was last active.
    // The _01 variant is always used for the very first crossfade in a tier;
    // subsequent crossfades within the same tier do not toggle (tier change only).
    MusicTrackId targetId = MUSIC_INVALID;
    switch (effective) {
        case MusicIntensity::CALM:
            targetId = MUSIC_CALM_01;
            break;
        case MusicIntensity::GROWTH:
            targetId = MUSIC_GROWTH_01;
            break;
        case MusicIntensity::CRISIS:
            targetId = MUSIC_CRISIS_01;
            break;
    }

    if (targetId == MUSIC_INVALID) return;

    // Determine incoming slot (the slot NOT currently active).
    int inSlot  = 1 - m_musicActiveSlot;  // 0→1 or 1→0
    int outSlot = m_musicActiveSlot;

    // Open the incoming stream on the incoming slot.
    std::string path = assetPath(musicTrackFilename(targetId));
    // Disjoint-use constraint: capture error message before calling logError,
    // because logError acquires m_logMutex which must not be held while m_streamMutex is.
    std::string beginIntensityError;
    {
        std::lock_guard<std::mutex> lk(m_streamMutex);

        // If an outgoing crossfade is already active, close the old incoming slot.
        if (m_musicIncomingSlot >= 0 && m_musicIncomingSlot != inSlot) {
            closeStream(m_musicIncomingSlot);
        }

        if (!openStreamOGG(inSlot, path, true)) {
            beginIntensityError = "beginIntensityCrossfade: cannot open: " + path;
        } else {
            // Set incoming stream gain to 0 and start playing immediately.
            m_streams[inSlot].crossfadeGain = 0.0f;
            alSourcei(static_cast<ALuint>(m_streams[inSlot].sourceHandle),
                      AL_LOOPING, AL_FALSE);
            alSourcePlay(static_cast<ALuint>(m_streams[inSlot].sourceHandle));
        }
    }
    if (!beginIntensityError.empty()) {
        logError(beginIntensityError);
        return;
    }

    // Reset crossfade progress and record state.
    m_musicCrossfadeT.store(0.0f, std::memory_order_relaxed);
    m_musicCrossfadeDuration = kMusicCrossfadeDurationSeconds;
    m_musicIncomingSlot      = inSlot;
    m_audioThreadIntensity   = static_cast<int>(effective);

    logInfo("beginIntensityCrossfade: outSlot=" + std::to_string(outSlot) +
            " inSlot=" + std::to_string(inSlot) +
            " intensity=" + std::to_string(static_cast<int>(effective)));
    (void)outSlot;
}

// ---------------------------------------------------------------------------
// beginAmbientCrossfade — start a real-time crossfade to the ambient bed for
// the given time-of-day (called on audio thread when m_currentTimeOfDay changes).
// Ambient beds use stream slots 2 and 3.
// ---------------------------------------------------------------------------
void AudioSystem::beginAmbientCrossfade(TimeOfDay tod) {
    // Determine the inactive ambient slot.
    // m_ambientIncomingSlot tracks the most-recently incoming ambient slot.
    // At steady state the active slot is the one NOT being transitioned to.
    // We use a simple heuristic: if slot 2 is open, use slot 3 as incoming; else 2.
    int inSlot = m_streams[2].isOpen ? 3 : 2;

    std::string path = assetPath(ambientBedFilename(tod));
    // Disjoint-use constraint: capture error message before calling logError,
    // because logError acquires m_logMutex which must not be held while m_streamMutex is.
    std::string beginAmbientError;
    {
        std::lock_guard<std::mutex> lk(m_streamMutex);

        if (!openStreamOGG(inSlot, path, false)) {
            beginAmbientError = "beginAmbientCrossfade: cannot open: " + path;
        } else {
            m_streams[inSlot].crossfadeGain = 0.0f;
            alSourcei(static_cast<ALuint>(m_streams[inSlot].sourceHandle),
                      AL_LOOPING, AL_FALSE);
            alSourcePlay(static_cast<ALuint>(m_streams[inSlot].sourceHandle));
        }
    }
    if (!beginAmbientError.empty()) {
        logError(beginAmbientError);
        return;
    }

    m_ambientCrossfadeT.store(0.0f, std::memory_order_relaxed);
    m_ambientCrossfadeDuration = kMusicCrossfadeDurationSeconds;
    m_ambientIncomingSlot      = inSlot;
}

// ---------------------------------------------------------------------------
// updateStreams — called once per audio thread wake.
// ---------------------------------------------------------------------------
void AudioSystem::updateStreams(float dt) {
    // ------------------------------------------------------------------
    // Phase 10: Consume pending ambient bed crossfade request.
    // setTimeOfDay() stores the new TimeOfDay in m_pendingAmbientTod; we
    // exchange it with -1 here so only one ambient crossfade fires per transition.
    // ------------------------------------------------------------------
    {
        int pendingTod = m_pendingAmbientTod.exchange(-1, std::memory_order_relaxed);
        if (pendingTod >= 0) {
            beginAmbientCrossfade(static_cast<TimeOfDay>(pendingTod));
        }
    }

    // ------------------------------------------------------------------
    // Phase 10: Check if main thread requested a music intensity change.
    // Compare m_currentMusicIntensity (atomic, written by main thread via
    // setMusicIntensity()) against m_audioThreadIntensity (last value this
    // thread started streaming).  A change triggers a beat-boundary crossfade
    // on the next bar boundary of the active stem.
    // Time-of-day forced-Calm override is applied inside beginIntensityCrossfade().
    // ------------------------------------------------------------------
    {
        int requestedInt = m_currentMusicIntensity.load(std::memory_order_relaxed);
        // Apply time-of-day forced-Calm override for comparison:
        // If the current time of day is not DAY, only CALM is valid regardless.
        int effectiveInt = requestedInt;
        if (static_cast<TimeOfDay>(m_currentTimeOfDay.load(std::memory_order_relaxed)) != TimeOfDay::DAY) {
            effectiveInt = static_cast<int>(MusicIntensity::CALM);
        }

        if (effectiveInt != m_audioThreadIntensity && m_musicIncomingSlot == -1) {
            // No crossfade currently in progress — queue one at the next bar boundary.
            // We mark it pending by temporarily storing the new intensity; the bar
            // boundary check in refillStream fires beginIntensityCrossfade().
            // For simplicity in V1 we begin immediately if no crossfade is active.
            beginIntensityCrossfade(static_cast<MusicIntensity>(requestedInt));
        }
    }

    // ------------------------------------------------------------------
    // Refill all open streams.
    // ------------------------------------------------------------------
    for (int i = 0; i < kStreamSourceCount; ++i) {
        if (!m_streams[i].isOpen) continue;
        refillStream(i);
    }

    // ------------------------------------------------------------------
    // Phase 10: Advance music crossfade progress.
    // dt is the real-time elapsed seconds measured by audioThreadFunc() via
    // IClock::nowSeconds().  Using the actual measured dt (not a hardcoded
    // 0.010 f) ensures crossfade timing is accurate under system scheduling
    // jitter and is deterministic with ManualClock in tests.
    // Guard: clamp dt to 0.0 to prevent negative advancement on the first wake
    // (m_lastDuckWakeTime is initialised at thread start; dt is always >= 0).
    // Guard: if m_musicCrossfadeDuration is 0 (defensive), treat crossfade as
    // instant to avoid division-by-zero.
    // ------------------------------------------------------------------
    if (m_musicIncomingSlot >= 0) {
        float t = m_musicCrossfadeT.load(std::memory_order_relaxed);
        if (m_musicCrossfadeDuration > 0.0f) {
            t += dt / m_musicCrossfadeDuration;
        } else {
            t = 1.0f;  // zero-duration crossfade: instant completion
        }
        t = std::min(t, 1.0f);
        m_musicCrossfadeT.store(t, std::memory_order_relaxed);

        int outSlot = 1 - m_musicIncomingSlot;  // the slot that is fading out
        applyCrossfadeGains(m_streams[outSlot], m_streams[m_musicIncomingSlot], t);

        if (t >= 1.0f) {
            // Crossfade complete: close the outgoing slot.
            {
                std::lock_guard<std::mutex> lk(m_streamMutex);
                closeStream(outSlot);
            }
            m_streams[m_musicIncomingSlot].crossfadeGain = 1.0f;
            m_musicActiveSlot    = m_musicIncomingSlot;
            m_musicIncomingSlot  = -1;
            m_musicCrossfadeT.store(0.0f, std::memory_order_relaxed);
        }
    }

    // Phase 10: Advance ambient bed crossfade progress.
    if (m_ambientIncomingSlot >= 0) {
        float t = m_ambientCrossfadeT.load(std::memory_order_relaxed);
        if (m_ambientCrossfadeDuration > 0.0f) {
            t += dt / m_ambientCrossfadeDuration;
        } else {
            t = 1.0f;  // zero-duration crossfade: instant completion
        }
        t = std::min(t, 1.0f);
        m_ambientCrossfadeT.store(t, std::memory_order_relaxed);

        int ambOutSlot = (m_ambientIncomingSlot == 2) ? 3 : 2;
        applyCrossfadeGains(m_streams[ambOutSlot], m_streams[m_ambientIncomingSlot], t);

        if (t >= 1.0f) {
            {
                std::lock_guard<std::mutex> lk(m_streamMutex);
                closeStream(ambOutSlot);
            }
            m_streams[m_ambientIncomingSlot].crossfadeGain = 1.0f;
            m_ambientIncomingSlot = -1;
            m_ambientCrossfadeT.store(0.0f, std::memory_order_relaxed);
        }
    }

    // ------------------------------------------------------------------
    // Apply final gain to all open streams (music duck multiplicative).
    // ------------------------------------------------------------------
    // Clear any stale AL error left by refillStream / closeStream above.
    // closeStream calls alSourceStop / alSourceUnqueueBuffers without error
    // checks; a failure there leaves AL_INVALID_OPERATION in the error state
    // which would otherwise be falsely attributed to alSourcef(AL_GAIN) below.
    alGetError();

    float duckGain = m_musicDuckGain.load(std::memory_order_relaxed);
    float musicVol = m_musicVolume.load(std::memory_order_relaxed);
    for (int i = 0; i < kStreamSourceCount; ++i) {
        if (!m_streams[i].isOpen) continue;
        if (m_streams[i].sourceHandle == 0) continue;

        float gain = m_streams[i].crossfadeGain;
        if (i < 2) {
            // Music stem: apply duck gain and music volume.
            gain *= duckGain * musicVol;
        }
        // Ambient beds (slots 2 and 3) are NOT ducked — spec §Stingers.
        alSourcef(static_cast<ALuint>(m_streams[i].sourceHandle),
                  AL_GAIN, gain);
        alCheckError_real("updateStreams:alSourcef(AL_GAIN)");
    }
}

// ---------------------------------------------------------------------------
// updateOcclusion — per-source gain smoothing (audio thread, every 10 ms wake).
// ---------------------------------------------------------------------------
void AudioSystem::updateOcclusion() {
    if (!m_efxAvailable) return;

    constexpr float kOcclusionGainStep = 0.05f;

    std::lock_guard<std::mutex> lk(m_occlusionMutex);
    for (int i = 0; i < kEvictableSFXCount; ++i) {
        float& cur = m_occlusionGainCurrent[i];
        float  tgt = m_occlusionGainTarget[i].load(std::memory_order_relaxed);

        bool changed = false;
        if (std::abs(cur - tgt) > kOcclusionGainStep) {
            cur += (tgt > cur) ? kOcclusionGainStep : -kOcclusionGainStep;
            changed = true;
        } else if (cur != tgt) {
            cur     = tgt;
            changed = true;
        }

        if (changed) {
            m_fnFilterf(m_occlusionFilter[i], AL_LOWPASS_GAIN, cur);
            alSourcei(static_cast<ALuint>(m_sources[i]),
                      AL_DIRECT_FILTER,
                      static_cast<ALint>(m_occlusionFilter[i]));
        }
    }
}

// ---------------------------------------------------------------------------
// updateDuckState — called each audio thread wake with real-time dt.
// ---------------------------------------------------------------------------
void AudioSystem::updateDuckState(float dt) {
    switch (m_duckState.load(std::memory_order_relaxed)) {
        case DuckState::IDLE:
            break;

        case DuckState::DUCKING: {
            m_duckTimer += dt;
            float t = std::min(m_duckTimer / 0.2f, 1.0f);
            float gain = m_duckStartGain + (kMusicDuckGain - m_duckStartGain) * t;
            m_musicDuckGain.store(gain, std::memory_order_relaxed);
            if (m_duckTimer >= 0.2f) {
                m_musicDuckGain.store(kMusicDuckGain, std::memory_order_relaxed);
                m_duckTimer = 0.f;
                m_duckState.store(DuckState::DUCKED, std::memory_order_relaxed);
            }
            break;
        }

        case DuckState::DUCKED: {
            // Stay ducked while any stinger source is playing.
            // V1: check sources[55] (CRISIS) and sources[56] (MILESTONE) only.
            // sources[57] is IDLE in V1 and MUST NOT be queried.
            bool anyPlaying = false;
            for (int s : {static_cast<int>(StingerType::CRISIS),
                          static_cast<int>(StingerType::MILESTONE)}) {
                ALint state = AL_STOPPED;
                alGetSourcei(static_cast<ALuint>(m_sources[s]),
                             AL_SOURCE_STATE, &state);
                if (state == AL_PLAYING) { anyPlaying = true; break; }
            }
            if (!anyPlaying) {
                m_duckTimer = 0.f;
                m_duckState.store(DuckState::RELEASING, std::memory_order_relaxed);
            }
            break;
        }

        case DuckState::RELEASING: {
            m_duckTimer += dt;
            float t    = std::min(m_duckTimer / 1.5f, 1.0f);
            float gain = kMusicDuckGain + 0.6f * t;
            gain = std::max(kMusicDuckGain, std::min(gain, 1.0f));
            m_musicDuckGain.store(gain, std::memory_order_relaxed);
            if (m_duckTimer >= 1.5f) {
                m_musicDuckGain.store(1.0f, std::memory_order_relaxed);
                m_duckTimer = 0.f;
                m_duckState.store(DuckState::IDLE, std::memory_order_relaxed);
            }
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// onSourceRecycled — reset occlusion gain for a recycled SFX slot.
// Called from main thread (pool eviction / release via playSFX/stopSFX) OR from
// the audio thread (cleanupFinishedSFX). Acquires m_occlusionMutex internally.
// ---------------------------------------------------------------------------
void AudioSystem::onSourceRecycled(int i) {
    if (i < 0 || i >= kEvictableSFXCount) return;

    std::lock_guard<std::mutex> lk(m_occlusionMutex);
    m_occlusionGainCurrent[i] = 1.0f;
    m_occlusionGainTarget[i].store(1.0f, std::memory_order_relaxed);

    if (m_efxAvailable) {
        // Clear any stale AL error from main-thread callers (playSFX/stopSFX
        // eviction may call alSourceStop on a playing source, setting an error
        // that would otherwise be misattributed to alFilterf below).
        alGetError();
        m_fnFilterf(m_occlusionFilter[i], AL_LOWPASS_GAIN,   1.0f);
        alCheckError_real("alFilterf(AL_LOWPASS_GAIN) in onSourceRecycled");
        m_fnFilterf(m_occlusionFilter[i], AL_LOWPASS_GAINHF, 1.0f);
        alCheckError_real("alFilterf(AL_LOWPASS_GAINHF) in onSourceRecycled");
        alSourcei(static_cast<ALuint>(m_sources[i]),
                  AL_DIRECT_FILTER,
                  static_cast<ALint>(m_occlusionFilter[i]));
        alCheckError_real("alSourcei(AL_DIRECT_FILTER) in onSourceRecycled");
    }
}

// ===========================================================================
// IAudioSystem interface implementations
// ===========================================================================

// ---------------------------------------------------------------------------
// playSound — non-positional (2D) one-shot.
// ---------------------------------------------------------------------------
SoundHandle AudioSystem::playSound(SoundId id, SoundPriority priority,
                                    float gain) {
    if (m_deviceLost.load(std::memory_order_relaxed)) return 0;
    auto it = m_preloadedBuffers.find(id);
    if (it == m_preloadedBuffers.end()) {
        logWarning("playSound: SoundId " + std::to_string(id) + " not loaded");
        return 0;
    }

    // Acquire SFX source (no distance — non-positional, use 0.f).
    // Use AudioSourcePool logic inline (pool object is embedded in AudioSystem
    // to keep the implementation self-contained in Phase 7).
    // Simple scan for free slot.
    int upperBound = (priority == SoundPriority::LOW ||
                      priority == SoundPriority::NORMAL)
                     ? kTransientReserveStart
                     : kEvictableSFXCount;

    int idx = -1;
    for (int i = 0; i < upperBound; ++i) {
        if (!m_sfxSlots[i].occupied) { idx = i; break; }
    }

    if (idx < 0) {
        // Try eviction: find lowest-priority / greatest-distance.
        int   bestPri  = static_cast<int>(priority);
        float bestDist = -1.f;
        for (int i = 0; i < upperBound; ++i) {
            if (!m_sfxSlots[i].occupied) continue;
            int pri = static_cast<int>(m_sfxSlots[i].priority);
            if (pri < bestPri || (pri == bestPri &&
                    m_sfxSlots[i].listenerDistanceSq > bestDist)) {
                idx      = i;
                bestPri  = pri;
                bestDist = m_sfxSlots[i].listenerDistanceSq;
            }
        }
        if (idx < 0) return 0;  // Cannot acquire

        // Evict the candidate.
        alSourceStop(static_cast<ALuint>(m_sources[idx]));
        alSourcei(static_cast<ALuint>(m_sources[idx]), AL_BUFFER, 0);
        onSourceRecycled(idx);
    }

    ALuint src = static_cast<ALuint>(m_sources[idx]);
    ALuint buf = static_cast<ALuint>(it->second);

    // Non-positional setup.
    alSourcei (src, AL_SOURCE_RELATIVE, AL_TRUE);
    alSource3f(src, AL_POSITION,        0.f, 0.f, 0.f);
    alSourcef (src, AL_ROLLOFF_FACTOR,  0.f);
    alSourcei (src, AL_DIRECT_FILTER,   AL_FILTER_NULL);  // EFX bypass

    alSourcef (src, AL_GAIN,  gain);
    alSourcei (src, AL_BUFFER, static_cast<ALint>(buf));
    alSourcei (src, AL_LOOPING, AL_FALSE);
    alSourcePlay(src);

    SoundHandle handle = m_nextHandle.fetch_add(1, std::memory_order_relaxed);
    m_sfxSlots[idx] = {id, static_cast<unsigned int>(buf), handle,
                        priority, 0.f, true};
    return handle;
}

// ---------------------------------------------------------------------------
// playPositionalSound — world-positioned (3D) one-shot.
// ---------------------------------------------------------------------------
SoundHandle AudioSystem::playPositionalSound(SoundId id, vec3 pos,
                                              SoundPriority priority,
                                              float gain) {
    if (m_deviceLost.load(std::memory_order_relaxed)) return 0;
    auto it = m_preloadedBuffers.find(id);
    if (it == m_preloadedBuffers.end()) {
        logWarning("playPositionalSound: SoundId " +
                   std::to_string(id) + " not loaded");
        return 0;
    }

    // No listener position available in this method; use large distance as heuristic.
    float distSq = 0.f;

    int upperBound = (priority == SoundPriority::LOW ||
                      priority == SoundPriority::NORMAL)
                     ? kTransientReserveStart
                     : kEvictableSFXCount;

    int idx = -1;
    for (int i = 0; i < upperBound; ++i) {
        if (!m_sfxSlots[i].occupied) { idx = i; break; }
    }

    if (idx < 0) {
        int   bestPri  = static_cast<int>(priority);
        float bestDist = -1.f;
        for (int i = 0; i < upperBound; ++i) {
            if (!m_sfxSlots[i].occupied) continue;
            int pri = static_cast<int>(m_sfxSlots[i].priority);
            if (pri < bestPri || (pri == bestPri &&
                    m_sfxSlots[i].listenerDistanceSq > bestDist)) {
                idx      = i;
                bestPri  = pri;
                bestDist = m_sfxSlots[i].listenerDistanceSq;
            }
        }
        if (idx < 0) return 0;

        alSourceStop(static_cast<ALuint>(m_sources[idx]));
        alSourcei(static_cast<ALuint>(m_sources[idx]), AL_BUFFER, 0);
        onSourceRecycled(idx);
    }

    ALuint src = static_cast<ALuint>(m_sources[idx]);
    ALuint buf = static_cast<ALuint>(it->second);

    // Positional setup — default rolloff for general SFX category.
    alSourcei (src, AL_SOURCE_RELATIVE, AL_FALSE);
    alSource3f(src, AL_POSITION,        pos.x, pos.y, pos.z);
    alSource3f(src, AL_VELOCITY,        0.f, 0.f, 0.f);
    alSourcef (src, AL_ROLLOFF_FACTOR,  1.0f);
    alSourcef (src, AL_REFERENCE_DISTANCE, 10.f);
    alSourcef (src, AL_MAX_DISTANCE,       150.f);

    // Phase 10: EFX bypass for SFX_EARTHWORKS — construction occurs on open,
    // unoccluded tiles so the lowpass filter would incorrectly muffle the sound.
    // All other positional SFX use the occlusion filter (AL_DIRECT_FILTER is left
    // at its current value — either the filter bound during construction or the
    // value set by the last onSourceRecycled() call, which restores it to open).
    if (id == SFX_EARTHWORKS) {
        alSourcei(src, AL_DIRECT_FILTER, AL_FILTER_NULL);
        alCheckError_real("playPositionalSound:EFXBypass(SFX_EARTHWORKS)");
    }

    alSourcef(src, AL_GAIN,   gain);
    alSourcei(src, AL_BUFFER, static_cast<ALint>(buf));
    alSourcei(src, AL_LOOPING, AL_FALSE);
    alSourcePlay(src);

    SoundHandle handle = m_nextHandle.fetch_add(1, std::memory_order_relaxed);
    m_sfxSlots[idx] = {id, static_cast<unsigned int>(buf), handle,
                        priority, distSq, true};
    return handle;
}

// ---------------------------------------------------------------------------
// stopSound — stop a previously-started sound by handle.
// ---------------------------------------------------------------------------
void AudioSystem::stopSound(SoundHandle handle) {
    if (handle == 0) return;
    if (m_deviceLost.load(std::memory_order_relaxed)) return;
    for (int i = 0; i < kEvictableSFXCount; ++i) {
        if (m_sfxSlots[i].occupied && m_sfxSlots[i].handle == handle) {
            alSourceStop(static_cast<ALuint>(m_sources[i]));
            alSourcei(static_cast<ALuint>(m_sources[i]), AL_BUFFER, 0);
            onSourceRecycled(i);
            m_sfxSlots[i] = SFXSlot{};
            return;
        }
    }
    // Stale handle — silently ignored.
}

// ---------------------------------------------------------------------------
// setMusicTrack — begin crossfade to the specified music track.
// ---------------------------------------------------------------------------
void AudioSystem::setMusicTrack(MusicTrackId id) {
    if (m_deviceLost.load(std::memory_order_relaxed)) return;
    if (id == MUSIC_INVALID) return;
    std::string filename = musicTrackFilename(id);
    if (filename.empty()) {
        logError("setMusicTrack: unknown MusicTrackId " + std::to_string(id));
        return;
    }

    std::string path = assetPath(filename);

    // Lock while modifying stream state.
    std::lock_guard<std::mutex> lk(m_streamMutex);

    // Open on the active music slot (m_musicActiveSlot) if no music is playing yet.
    // If music is already playing, open on the opposite slot and start a crossfade.
    if (!m_streams[m_musicActiveSlot].isOpen) {
        if (openStreamOGG(m_musicActiveSlot, path, true)) {
            ALuint src = static_cast<ALuint>(m_streams[m_musicActiveSlot].sourceHandle);
            m_streams[m_musicActiveSlot].crossfadeGain = 1.0f;
            alSourcei(src, AL_LOOPING, AL_FALSE);
            alSourcePlay(src);
        }
    } else {
        // Music is already playing — initiate a crossfade to the new track.
        // Phase 10: use the incoming (non-active) slot.
        int inSlot = 1 - m_musicActiveSlot;
        if (openStreamOGG(inSlot, path, true)) {
            m_streams[inSlot].crossfadeGain = 0.0f;
            ALuint src = static_cast<ALuint>(m_streams[inSlot].sourceHandle);
            alSourcei(src, AL_LOOPING, AL_FALSE);
            alSourcePlay(src);
            m_musicCrossfadeT.store(0.0f, std::memory_order_relaxed);
            m_musicCrossfadeDuration = kMusicCrossfadeDurationSeconds;
            m_musicIncomingSlot      = inSlot;
        }
    }
    m_streamCV.notify_one();
}

// ---------------------------------------------------------------------------
// setSpeed — update simulation speed (affects time-of-day collapse logic).
// ---------------------------------------------------------------------------
void AudioSystem::setSpeed(SimSpeed speed) {
    m_currentSpeed = speed;
}

// ---------------------------------------------------------------------------
// triggerStinger — fire a one-shot stinger (subject to cooldown rules).
// ---------------------------------------------------------------------------
void AudioSystem::triggerStinger(StingerType type) {
    if (m_deviceLost.load(std::memory_order_relaxed)) return;
    int poolIdx = static_cast<int>(type);
    if (poolIdx < kEvictableSFXCount || poolIdx >= kSFXPoolSize) {
        logError("triggerStinger: invalid StingerType " + std::to_string(poolIdx));
        return;
    }

    // Look up buffer for the stinger SoundId.
    // SoundId 20 = stinger_crisis, SoundId 21 = stinger_milestone.
    SoundId sid = (type == StingerType::CRISIS) ? SFX_STINGER_CRISIS
                                                 : SFX_STINGER_MILESTONE;

    auto it = m_preloadedBuffers.find(sid);
    if (it == m_preloadedBuffers.end()) {
        logWarning("triggerStinger: stinger buffer not loaded for type " +
                   std::to_string(poolIdx));
        return;
    }

    // Check cooldown (5 s minimum between same-type triggers).
    int cooldownIdx = poolIdx - kEvictableSFXCount;  // 0 or 1
    double now = m_clock->nowSeconds();
    if (now - m_stingerLastTriggerTime[cooldownIdx] < kStingerCooldownSeconds) {
        return;  // Cooldown not elapsed
    }

    // Drop if source is already playing (same-type in-progress rule).
    ALuint src = static_cast<ALuint>(m_sources[poolIdx]);
    ALint  state = AL_STOPPED;
    alGetSourcei(src, AL_SOURCE_STATE, &state);
    alCheckError_real("triggerStinger_stateQuery");
    if (state == AL_PLAYING) {
        return;  // Drop — in-progress
    }

    m_stingerLastTriggerTime[cooldownIdx] = now;

    // Fire stinger.
    ALuint buf = static_cast<ALuint>(it->second);
    alSourcei(src, AL_BUFFER,  static_cast<ALint>(buf));
    alSourcei(src, AL_LOOPING, AL_FALSE);
    alSourcePlay(src);

    // Initiate music duck (IDLE or RELEASING → DUCKING).
    DuckState ds = m_duckState.load(std::memory_order_relaxed);
    if (ds == DuckState::IDLE) {
        m_duckStartGain = 1.0f;
    } else if (ds == DuckState::RELEASING) {
        m_duckStartGain = m_musicDuckGain.load(std::memory_order_relaxed);
    } else if (ds == DuckState::DUCKING) {
        // Re-entry during DUCKING: capture current gain, reset timer.
        m_duckStartGain = m_musicDuckGain.load(std::memory_order_relaxed);
    } else {
        // Already DUCKED — reset timer only.
        m_duckTimer = 0.f;
        return;
    }
    m_duckTimer = 0.f;
    m_duckState.store(DuckState::DUCKING, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// syncListenerToCamera — update OpenAL listener state (main thread, each frame).
// ---------------------------------------------------------------------------
void AudioSystem::syncListenerToCamera(const CameraState& cam) {
    if (m_deviceLost.load(std::memory_order_relaxed)) return;
    // AL_POSITION.
    alListener3f(AL_POSITION, cam.position.x, cam.position.y, cam.position.z);

    // AL_VELOCITY — always zero (no Doppler for camera movement).
    alListener3f(AL_VELOCITY, 0.f, 0.f, 0.f);

    // AL_ORIENTATION — 6-float [at.x at.y at.z up.x up.y up.z].
    // COORDINATE CONVERSION: Irrlicht left-handed (Z into screen) → OpenAL
    // right-handed (Z out of screen). Negate Z on both forward and up.
    ALfloat orientation[6] = {
         cam.forward.x,  cam.forward.y, -cam.forward.z,  // "at" (Z negated)
         cam.up.x,       cam.up.y,      -cam.up.z          // "up" (Z negated)
    };
    alListenerfv(AL_ORIENTATION, orientation);
}

// ---------------------------------------------------------------------------
// setGameOverState — V1 no-op.
// ---------------------------------------------------------------------------
void AudioSystem::setGameOverState(bool /*active*/) {
    // V1: no-op — Sandbox mode has no game-over condition.
    // Scenario mode (post-V1) will implement the full fade sequence.
    logWarning("setGameOverState() called in V1 Sandbox mode — no-op");
    // m_gameOverFade is NEVER set in any V1 code path.
}

// ---------------------------------------------------------------------------
// setTimeOfDay — select ambient bed for the given time period.
// ---------------------------------------------------------------------------
void AudioSystem::setTimeOfDay(TimeOfDay tod) {
    m_currentTimeOfDay.store(static_cast<int>(tod), std::memory_order_relaxed);
    m_timeOfDaySet = true;
    // Phase 10: signal the audio thread to begin an ambient bed crossfade.
    // m_pendingAmbientTod is an atomic<int> — the audio thread reads it once per
    // wake in updateStreams() and calls beginAmbientCrossfade() if != -1.
    m_pendingAmbientTod.store(static_cast<int>(tod), std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// transitionToMainMenu — stop all gameplay audio and crossfade back to main
// menu music (Phase 11m).
//
// Spec requirements:
//   1. Stop all sources[58..61] unconditionally (alSourceStop is a no-op for
//      non-playing sources — no guard required).
//   2. Select a main-menu variant (1 or 2) that differs from the last played
//      (random-excluding-repeat) using m_rng under m_streamMutex.
//   3. Open selected variant on the active music slot; set up 1 s crossfade
//      using kMenuToGameplayCrossfadeDurationSeconds (same constant-power
//      curve as the menu→gameplay transition).
//   4. Set m_mainMenuMusicLooping = true as the FINAL operation (completion
//      sentinel for tests and the audio thread's EOF→seek loop path).
//
// Threading: called from the main thread only.
// m_streamMutex is acquired in two minimal scopes — variant selection and
// stream open — NOT held across the entire method body.
// ---------------------------------------------------------------------------
void AudioSystem::transitionToMainMenu() {
    if (m_deviceLost.load(std::memory_order_relaxed)) return;

    // ------------------------------------------------------------------
    // Step 1: stop all active gameplay music stems and ambient beds
    //         on sources[58..61] unconditionally.
    // closeStream requires m_streamMutex; call in a single lock scope.
    // ------------------------------------------------------------------
    {
        std::lock_guard<std::mutex> lk(m_streamMutex);
        for (int i = 0; i < kStreamSourceCount; ++i) {
            closeStream(i);
        }
        // Reset crossfade state so updateStreams() does not attempt to
        // resume a crossfade that was in-flight before we stopped.
        m_musicIncomingSlot  = -1;
        m_ambientIncomingSlot = -1;
        m_musicCrossfadeT.store(0.0f, std::memory_order_relaxed);
        m_ambientCrossfadeT.store(0.0f, std::memory_order_relaxed);
    }

    // ------------------------------------------------------------------
    // Step 2: variant selection — random-excluding-repeat under mutex.
    // Candidates: {1, 2} minus m_lastMainMenuVariant.
    // On first call (m_lastMainMenuVariant == -1) both are candidates.
    // ------------------------------------------------------------------
    int selectedVariant = 1;  // default (overwritten below)
    {
        std::lock_guard<std::mutex> lk(m_streamMutex);
        std::vector<int> candidates;
        for (int v : {1, 2}) {
            if (v != m_lastMainMenuVariant) candidates.push_back(v);
        }
        // candidates is always non-empty (at most one exclusion from a 2-element set).
        std::uniform_int_distribution<int> dist(0, static_cast<int>(candidates.size()) - 1);
        selectedVariant = candidates[dist(m_rng)];
        m_lastMainMenuVariant = selectedVariant;
    }
    // Lock released before the OGG open + stream start below.

    // ------------------------------------------------------------------
    // Step 3: open selected variant and start playing with 1 s crossfade.
    // Since we closed all streams in Step 1, m_streams[m_musicActiveSlot]
    // is guaranteed not open — use the direct-start path (no crossfade
    // setup needed; gain starts at 1.0 immediately).
    // ------------------------------------------------------------------
    MusicTrackId trackId = (selectedVariant == 1) ? MUSIC_MAIN_MENU_01 : MUSIC_MAIN_MENU_02;
    std::string filename = musicTrackFilename(trackId);
    std::string path     = assetPath(filename);

    {
        std::lock_guard<std::mutex> lk(m_streamMutex);
        if (openStreamOGG(m_musicActiveSlot, path, /*isMusicStem=*/true)) {
            m_streams[m_musicActiveSlot].crossfadeGain = 1.0f;
            ALuint src = static_cast<ALuint>(m_streams[m_musicActiveSlot].sourceHandle);
            alSourcei(src, AL_LOOPING, AL_FALSE);
            alCheckError_real("transitionToMainMenu:looping");
            alSourcePlay(src);
            alCheckError_real("transitionToMainMenu:play");
            // Seed audio thread intensity so updateStreams() does not
            // trigger an unwanted gameplay stem crossfade immediately.
            m_audioThreadIntensity = static_cast<int>(MusicIntensity::CALM);
        }
        m_streamCV.notify_one();
    }

    // ------------------------------------------------------------------
    // Step 4 (FINAL operation): set completion sentinel.
    // Must be the last store so tests can use isMainMenuMusicLooping()
    // as a synchronisation check.
    // ------------------------------------------------------------------
    {
        std::lock_guard<std::mutex> lk(m_streamMutex);
        m_mainMenuMusicLooping = true;
    }
}

void AudioSystem::transitionToGameplay() {
    if (m_deviceLost.load(std::memory_order_relaxed)) return;
    // Start ambient bed on slot 2 (sources[60]) for the current time of day.
    TimeOfDay tod = static_cast<TimeOfDay>(m_currentTimeOfDay.load(std::memory_order_relaxed));
    std::string ambPath = assetPath(ambientBedFilename(tod));
    {
        std::lock_guard<std::mutex> lk(m_streamMutex);
        if (!m_streams[2].isOpen) {
            if (openStreamOGG(2, ambPath, false)) {
                ALuint src = static_cast<ALuint>(m_streams[2].sourceHandle);
                alSourcei(src, AL_LOOPING, AL_FALSE);
                alSourcePlay(src);
            }
        }
    }

    // Crossfade from main menu music to the first gameplay stem (MUSIC_CALM_01).
    //
    // The spec mandates a 1 s constant-power crossfade (kMenuToGameplayCrossfadeDurationSeconds)
    // — the same sources[58..59] used by main menu music are reused for gameplay stems.
    // Main menu and gameplay are mutually exclusive, so this is always valid.
    //
    // If main menu music is currently playing (the normal path): open MUSIC_CALM_01
    // on the opposite slot and initiate a 1 s crossfade via the existing updateStreams()
    // crossfade state machine.
    //
    // If no music is playing (edge case — e.g. main menu was muted or never started):
    // open MUSIC_CALM_01 immediately on the active slot with gain 1.0 and no crossfade.
    std::string calmPath = assetPath(musicTrackFilename(MUSIC_CALM_01));
    {
        std::lock_guard<std::mutex> lk(m_streamMutex);

        if (m_streams[m_musicActiveSlot].isOpen) {
            // Normal path: main menu music is playing — 1 s fade-out / fade-in.
            int inSlot = 1 - m_musicActiveSlot;
            if (openStreamOGG(inSlot, calmPath, true)) {
                m_streams[inSlot].crossfadeGain = 0.0f;
                ALuint src = static_cast<ALuint>(m_streams[inSlot].sourceHandle);
                alSourcei(src, AL_LOOPING, AL_FALSE);
                alCheckError_real("transitionToGameplay:looping");
                alSourcePlay(src);
                alCheckError_real("transitionToGameplay:play");
                m_musicCrossfadeT.store(0.0f, std::memory_order_relaxed);
                m_musicCrossfadeDuration = kMenuToGameplayCrossfadeDurationSeconds;
                m_musicIncomingSlot      = inSlot;
                // Seed the incoming intensity so updateStreams() does not re-trigger
                // a crossfade immediately after this one completes.
                m_audioThreadIntensity   = static_cast<int>(MusicIntensity::CALM);
            }
        } else {
            // Edge case: no music playing — start MUSIC_CALM_01 immediately.
            if (openStreamOGG(m_musicActiveSlot, calmPath, true)) {
                m_streams[m_musicActiveSlot].crossfadeGain = 1.0f;
                ALuint src = static_cast<ALuint>(m_streams[m_musicActiveSlot].sourceHandle);
                alSourcei(src, AL_LOOPING, AL_FALSE);
                alCheckError_real("transitionToGameplay:looping_direct");
                alSourcePlay(src);
                alCheckError_real("transitionToGameplay:play_direct");
                m_audioThreadIntensity = static_cast<int>(MusicIntensity::CALM);
            }
        }
        m_streamCV.notify_one();
    }
}

// ---------------------------------------------------------------------------
// update — per-frame main-thread update.
// ---------------------------------------------------------------------------
void AudioSystem::update(float /*realDeltaSeconds*/) {
    if (m_deviceLost.load(std::memory_order_relaxed)) return;
    // Phase 7 main-thread responsibilities:
    // 1. Advance occlusion raycast budget / per-source distance cull checks.
    //    (Full raycast implementation deferred to Phase 10 — occlusion gain
    //     targets are written by the main-thread occlusion raycast pass;
    //     the per-source smoothing runs on the audio thread in updateOcclusion().)
    //
    // 2. Process time-of-day transition queue.
    //    (Queued by setTimeOfDay(); executed here once per frame.)
    //
    // 3. Queue crossfade commands to audio thread via m_streamMutex.
    //    (NOTE: MUST NOT call alSourcef(AL_GAIN) directly on streaming sources
    //     from the main thread. All gain writes to streaming sources are done
    //     by the audio thread in updateStreams(). Crossfade requests are queued
    //     via m_streamMutex and processed by the audio thread.)

    // SFX cleanup is performed on the audio thread (cleanupFinishedSFX) to avoid
    // making AL calls here that could set AL_INVALID_OPERATION in the shared context
    // error state and bleed into the audio thread's alCheckError_real.
}

// ---------------------------------------------------------------------------
// setMasterVolume — applied immediately via alListenerf(AL_GAIN) on the
// calling thread (main thread).  m_masterVolume is plain float because it
// is only ever read/written from the main thread.
// ---------------------------------------------------------------------------
void AudioSystem::setMasterVolume(float gain) {
    m_masterVolume = gain;
    if (m_deviceLost.load(std::memory_order_relaxed)) return;
    alListenerf(AL_GAIN, gain);
    alCheckError_real("setMasterVolume");
}

// ---------------------------------------------------------------------------
// setMusicVolume — stored atomically; picked up by the audio thread at its
// next wake in updateStreams() (applied to music stream source gains).
// ---------------------------------------------------------------------------
void AudioSystem::setMusicVolume(float gain) {
    m_musicVolume.store(gain, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// setSFXVolume — stored atomically; picked up by the audio thread at its
// next wake (applied to SFX source gains).
// ---------------------------------------------------------------------------
void AudioSystem::setSFXVolume(float gain) {
    m_sfxVolume.store(gain, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// setMusicIntensity (Phase 10) — adaptive music tier driven by simulation state.
//
// Stores the requested intensity tier atomically.  The audio thread reads
// m_currentMusicIntensity at each wake (every ~10 ms) in updateStreams() and,
// if the tier has changed from the currently-playing tier, queues a beat-boundary
// crossfade to the stem pair matching the new intensity:
//   CALM   -> MUSIC_CALM_01 / MUSIC_CALM_02
//   GROWTH -> MUSIC_GROWTH_01 / MUSIC_GROWTH_02
//   CRISIS -> MUSIC_CRISIS_01 / MUSIC_CRISIS_02
//
// Time-of-day forced-Calm override (DUSK/NIGHT/DAWN) is applied internally by
// updateStreams(): when m_currentTimeOfDay != DAY, only CALM stems are selected
// regardless of m_currentMusicIntensity.  CitySimulation does NOT need to
// suppress GROWTH/CRISIS calls during off-hours.
//
// Calling with the tier already active is a no-op (audio thread detects no change).
// Thread-safety: call from the main thread only (store to m_currentMusicIntensity
// is atomic, so there is no data race with audio thread reads).
//
// NOTE: Phase 10 deliverable.  updateStreams() cross-fade logic that responds
// to m_currentMusicIntensity is also a Phase 10 deliverable and is implemented
// in the same Phase 10 commit that delivers the adaptive music stems and wires
// CitySimulation::update().
// ---------------------------------------------------------------------------
void AudioSystem::setMusicIntensity(MusicIntensity intensity) {
    m_currentMusicIntensity.store(static_cast<int>(intensity), std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Phase 11d — Vehicle engine audio pair implementation (Deliverable 3a).
//
// Threading model:
//   acquireVehicleEnginePair / releaseVehicleEnginePair / updateVehicleAudio
//   are MAIN THREAD entry points.  They write state into VehicleAudioSlot
//   fields atomically.  The audio thread reads those atomics in
//   updateVehicleEngines() (called every ~10 ms wake) and applies all AL
//   calls (AL_VELOCITY, AL_PITCH, AL_GAIN, AL_POSITION, alSourcePlay,
//   alSourceStop).  No AL calls are made on the main thread from these methods.
//
// Source acquisition uses the same inline pool logic as playSound() — scanning
// m_sfxSlots[0..kTransientReserveStart-1] for free NORMAL-priority slots.
// The VehicleAudioSlot array (m_vehicleAudio) is the sole cross-thread
// communication mechanism; no mutex is needed because all fields are atomic.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// acquireVehicleEnginePair — main thread entry point.
//
// Finds a free VehicleAudioSlot and two free SFX pool sources (NORMAL priority
// range [0..kTransientReserveStart-1]).  Stores their indices atomically and
// sets pendingInit so the audio thread performs AL setup on its next wake.
// Returns {idleIdx, moveIdx} on success, {-1,-1} if pool exhausted.
// ---------------------------------------------------------------------------
std::pair<int,int> AudioSystem::acquireVehicleEnginePair(ZoneType zone) {
    if (m_deviceLost.load(std::memory_order_relaxed)) return {-1, -1};

    // Determine base pitch from zone type.
    // Residential = car (1.0), Commercial = bus (0.85), Industrial = truck (0.85).
    float basePitch = (zone == ZoneType::Residential) ? 1.0f : 0.85f;

    // -----------------------------------------------------------------------
    // Step 1: Find a free VehicleAudioSlot.
    // -----------------------------------------------------------------------
    int slotIdx = -1;
    for (int i = 0; i < kMaxVehiclePairs; ++i) {
        if (m_vehicleAudio[i].idleSourceIdx.load(std::memory_order_relaxed) == -1) {
            slotIdx = i;
            break;
        }
    }

    // evictSlot >= 0 when eviction was needed; used in Step 3 to preserve pendingRelease.
    int evictSlot = -1;
    if (slotIdx < 0) {
        // All 12 slots occupied — attempt to evict the lowest-priority /
        // greatest-distance pair.  Vehicle pairs all have NORMAL priority,
        // so distance alone determines the eviction candidate.
        float evictDist = -1.f;
        for (int i = 0; i < kMaxVehiclePairs; ++i) {
            int   idleIdx = m_vehicleAudio[i].idleSourceIdx.load(std::memory_order_relaxed);
            if (idleIdx < 0) continue;  // sanity — already free
            float dist = (idleIdx >= 0 && idleIdx < kEvictableSFXCount)
                         ? m_sfxSlots[idleIdx].listenerDistanceSq : 0.f;
            if (dist > evictDist) {
                evictDist = dist;
                evictSlot = i;
            }
        }
        if (evictSlot < 0) return {-1, -1};

        // Queue the evicted sources for AL stop/detach on the audio thread via
        // pendingRelease.  Do NOT make AL calls on the main thread: both threads
        // share the same OpenAL context error state, so a main-thread AL error
        // (e.g. alSourceStop on a source the audio thread is concurrently using)
        // bleeds into the audio thread's alCheckError_real and triggers a
        // spurious AL_INVALID_OPERATION that disables audio.
        int evictIdle = m_vehicleAudio[evictSlot].idleSourceIdx.load(std::memory_order_relaxed);
        int evictMove = m_vehicleAudio[evictSlot].moveSourceIdx.load(std::memory_order_relaxed);
        if (evictIdle >= 0) {
            // Clear vehicle-reserved flag before clearing the slot — cleanupFinishedSFX
            // uses this atomic to skip vehicle sources; clearing it before the slot
            // prevents cleanupFinishedSFX from racing on m_sfxSlots[evictIdle].soundId.
            m_sfxVehicleReserved[evictIdle].store(false, std::memory_order_release);
            m_sfxSlots[evictIdle] = SFXSlot{};
            m_occlusionGainTarget[evictIdle].store(1.0f, std::memory_order_relaxed);
        }
        if (evictMove >= 0) {
            m_sfxVehicleReserved[evictMove].store(false, std::memory_order_release);
            m_sfxSlots[evictMove] = SFXSlot{};
            m_occlusionGainTarget[evictMove].store(1.0f, std::memory_order_relaxed);
        }
        // Signal audio thread to stop and detach the evicted sources.
        m_vehicleAudio[evictSlot].pendingReleaseIdle.store(evictIdle, std::memory_order_relaxed);
        m_vehicleAudio[evictSlot].pendingReleaseMove.store(evictMove, std::memory_order_relaxed);
        m_vehicleAudio[evictSlot].pendingRelease.store(true, std::memory_order_relaxed);
        m_vehicleAudio[evictSlot].pendingInit.store(false, std::memory_order_relaxed);
        m_vehicleAudio[evictSlot].speedFraction.store(0.f, std::memory_order_relaxed);
        m_vehicleAudio[evictSlot].worldX.store(0.f, std::memory_order_relaxed);
        m_vehicleAudio[evictSlot].worldZ.store(0.f, std::memory_order_relaxed);
        // Mark slot free — idleSourceIdx=-1 is the "free" sentinel; set last.
        m_vehicleAudio[evictSlot].moveSourceIdx.store(-1, std::memory_order_relaxed);
        m_vehicleAudio[evictSlot].idleSourceIdx.store(-1, std::memory_order_relaxed);
        slotIdx = evictSlot;
    }

    // -----------------------------------------------------------------------
    // Step 2: Acquire two free SFX pool sources in NORMAL range [0..50].
    // -----------------------------------------------------------------------
    int idleIdx = -1, moveIdx = -1;
    for (int i = 0; i < kTransientReserveStart; ++i) {
        if (!m_sfxSlots[i].occupied) {
            if (idleIdx < 0)       idleIdx = i;
            else if (moveIdx < 0)  moveIdx = i;
        }
        if (idleIdx >= 0 && moveIdx >= 0) break;
    }

    if (idleIdx < 0 || moveIdx < 0) {
        // Partial acquisition prohibited — release anything grabbed.
        // (No partial marks needed since we did not set occupied yet.)
        return {-1, -1};
    }

    // Reserve slots as vehicle engine sources BEFORE populating m_sfxSlots.
    // cleanupFinishedSFX (audio thread) loads this atomic flag with memory_order_acquire
    // to skip vehicle sources; the release store here ensures the flag is visible before
    // the non-atomic m_sfxSlots write, eliminating the data race on soundId.
    m_sfxVehicleReserved[idleIdx].store(true, std::memory_order_release);
    m_sfxVehicleReserved[moveIdx].store(true, std::memory_order_release);

    // Mark both pool slots as occupied (NORMAL priority, zero initial distance).
    m_sfxSlots[idleIdx] = SFXSlot{SFX_VEHICLE_ENGINE_IDLE, 0, 0,
                                   SoundPriority::NORMAL, 0.f, true};
    m_sfxSlots[moveIdx] = SFXSlot{SFX_VEHICLE_ENGINE_MOVE, 0, 0,
                                   SoundPriority::NORMAL, 0.f, true};

    // -----------------------------------------------------------------------
    // Step 3: Populate the VehicleAudioSlot atomically.
    // pendingInit=true signals the audio thread to perform AL setup on its
    // next wake (bind buffers, set AL_VELOCITY=0, alSourcePlay).
    // -----------------------------------------------------------------------
    m_vehicleAudio[slotIdx].speedFraction.store(0.f, std::memory_order_relaxed);
    m_vehicleAudio[slotIdx].worldX.store(0.f, std::memory_order_relaxed);
    m_vehicleAudio[slotIdx].worldZ.store(0.f, std::memory_order_relaxed);
    m_vehicleAudio[slotIdx].basePitch.store(basePitch, std::memory_order_relaxed);
    // Do not clear pendingRelease when eviction already queued a stop for the audio thread.
    if (evictSlot < 0) {
        m_vehicleAudio[slotIdx].pendingRelease.store(false, std::memory_order_relaxed);
    }
    m_vehicleAudio[slotIdx].moveSourceIdx.store(moveIdx, std::memory_order_relaxed);
    m_vehicleAudio[slotIdx].idleSourceIdx.store(idleIdx, std::memory_order_relaxed);  // last: marks slot live
    m_vehicleAudio[slotIdx].pendingInit.store(true, std::memory_order_relaxed);

    // Wake audio thread so it processes pendingInit promptly (< 10 ms latency).
    m_streamCV.notify_one();

    return {idleIdx, moveIdx};
}

// ---------------------------------------------------------------------------
// releaseVehicleEnginePair — main thread entry point.
//
// Finds the VehicleAudioSlot whose idle/move indices match, marks it for
// release (pendingRelease=true), frees the SFX pool slots so they can be
// reacquired, and wakes the audio thread to do the actual alSourceStop.
// Passing {-1,-1} (failed acquisition result) is a safe no-op per spec.
// ---------------------------------------------------------------------------
void AudioSystem::releaseVehicleEnginePair(int idleIdx, int moveIdx) {
    // Guard: {-1,-1} is a safe no-op.
    if (idleIdx == -1 || moveIdx == -1) return;
    if (m_deviceLost.load(std::memory_order_relaxed)) return;

    // Find the matching VehicleAudioSlot.
    for (int i = 0; i < kMaxVehiclePairs; ++i) {
        if (m_vehicleAudio[i].idleSourceIdx.load(std::memory_order_relaxed) == idleIdx &&
            m_vehicleAudio[i].moveSourceIdx.load(std::memory_order_relaxed) == moveIdx)
        {
            // Store release indices BEFORE clearing idleSourceIdx, so the audio
            // thread can read them when it processes pendingRelease.
            m_vehicleAudio[i].pendingReleaseIdle.store(idleIdx, std::memory_order_relaxed);
            m_vehicleAudio[i].pendingReleaseMove.store(moveIdx, std::memory_order_relaxed);
            // Signal audio thread to stop sources.
            m_vehicleAudio[i].pendingRelease.store(true, std::memory_order_relaxed);
            // Free SFX pool slots immediately so they can be reacquired.
            // alSourceStop happens on the audio thread via pendingRelease.
            // Clear m_sfxVehicleReserved BEFORE clearing m_sfxSlots so that
            // cleanupFinishedSFX, which checks the atomic flag, won't see a
            // stale reserved=true after the slot has been logically freed.
            if (idleIdx >= 0 && idleIdx < kEvictableSFXCount) {
                m_sfxVehicleReserved[idleIdx].store(false, std::memory_order_release);
                m_sfxSlots[idleIdx] = SFXSlot{};
            }
            if (moveIdx >= 0 && moveIdx < kEvictableSFXCount) {
                m_sfxVehicleReserved[moveIdx].store(false, std::memory_order_release);
                m_sfxSlots[moveIdx] = SFXSlot{};
            }
            // Mark slot as free so acquireVehicleEnginePair won't scan it as live.
            m_vehicleAudio[i].idleSourceIdx.store(-1, std::memory_order_relaxed);
            m_vehicleAudio[i].moveSourceIdx.store(-1, std::memory_order_relaxed);
            // Cancel any pending init for this slot (was acquired but never played).
            m_vehicleAudio[i].pendingInit.store(false, std::memory_order_relaxed);
            // Wake audio thread to process stop promptly.
            m_streamCV.notify_one();
            return;
        }
    }
    // No matching slot found — warn and return (implementation error or already released).
    logInfo("releaseVehicleEnginePair: no matching slot for idleIdx=" +
               std::to_string(idleIdx) + " moveIdx=" + std::to_string(moveIdx));
}

// ---------------------------------------------------------------------------
// updateVehicleAudio — main thread entry point (called per-frame per vehicle).
//
// Stores the current per-frame state atomically into the VehicleAudioSlot.
// The audio thread reads these on its next wake and applies AL_PITCH,
// AL_GAIN crossblend, and AL_POSITION.  No AL calls on the main thread.
// No-op if idleIdx == -1 (failed acquisition).
// ---------------------------------------------------------------------------
void AudioSystem::updateVehicleAudio(int idleIdx, int moveIdx,
                                     float speedFraction,
                                     float worldX, float worldZ) {
    if (idleIdx == -1) return;
    if (m_deviceLost.load(std::memory_order_relaxed)) return;

    // Find the matching VehicleAudioSlot and update per-frame state.
    for (int i = 0; i < kMaxVehiclePairs; ++i) {
        if (m_vehicleAudio[i].idleSourceIdx.load(std::memory_order_relaxed) == idleIdx &&
            m_vehicleAudio[i].moveSourceIdx.load(std::memory_order_relaxed) == moveIdx)
        {
            m_vehicleAudio[i].speedFraction.store(speedFraction, std::memory_order_relaxed);
            m_vehicleAudio[i].worldX.store(worldX, std::memory_order_relaxed);
            m_vehicleAudio[i].worldZ.store(worldZ, std::memory_order_relaxed);
            return;
        }
    }
    // No matching slot — silently ignore (slot may have been evicted).
}

// ---------------------------------------------------------------------------
// updateVehicleEngines — audio thread, called each wake (~10 ms).
//
// Processes pending vehicle init/release commands and applies per-frame AL
// state updates (AL_PITCH, AL_GAIN crossblend, AL_POSITION) for all active
// vehicle pairs.
//
// pendingInit: newly-acquired pair — bind buffers, set AL_VELOCITY=0, play.
// pendingRelease: releasing pair — stop both sources, clear slot.
// Active pair: apply speed-derived pitch/gain and world-space position.
//
// Crossblend thresholds per dynamic-soundscape.md §Vehicle Engine Audio:
//   idle gain = 1.0 - speedFraction (full at stop, 0 at max speed)
//   move gain = speedFraction        (0 at stop, full at max speed)
//   Both sources run simultaneously — never stop/restart at threshold.
// ---------------------------------------------------------------------------
void AudioSystem::updateVehicleEngines() {
    auto it_idle = m_preloadedBuffers.find(SFX_VEHICLE_ENGINE_IDLE);
    auto it_move = m_preloadedBuffers.find(SFX_VEHICLE_ENGINE_MOVE);
    const bool buffersReady = (it_idle != m_preloadedBuffers.end() &&
                                it_move != m_preloadedBuffers.end());

    for (int i = 0; i < kMaxVehiclePairs; ++i) {
        // ---------------------------------------------------------------
        // Process pendingRelease first (takes priority over pendingInit).
        // ---------------------------------------------------------------
        if (m_vehicleAudio[i].pendingRelease.load(std::memory_order_relaxed)) {
            // Main thread stored the source indices in pendingReleaseIdle/Move before
            // clearing idleSourceIdx and setting pendingRelease=true.
            int rIdle = m_vehicleAudio[i].pendingReleaseIdle.load(std::memory_order_relaxed);
            int rMove = m_vehicleAudio[i].pendingReleaseMove.load(std::memory_order_relaxed);
            if (rIdle >= 0 && rIdle < kEvictableSFXCount) {
                alSourceStop(static_cast<ALuint>(m_sources[rIdle]));
                alCheckError_real("updateVehicleEngines:release:alSourceStop(idle)");
                alSourcei(static_cast<ALuint>(m_sources[rIdle]), AL_BUFFER, 0);
                alCheckError_real("updateVehicleEngines:release:alSourcei(idle,AL_BUFFER,0)");
            }
            if (rMove >= 0 && rMove < kEvictableSFXCount) {
                alSourceStop(static_cast<ALuint>(m_sources[rMove]));
                alCheckError_real("updateVehicleEngines:release:alSourceStop(move)");
                alSourcei(static_cast<ALuint>(m_sources[rMove]), AL_BUFFER, 0);
                alCheckError_real("updateVehicleEngines:release:alSourcei(move,AL_BUFFER,0)");
            }
            m_vehicleAudio[i].pendingReleaseIdle.store(-1, std::memory_order_relaxed);
            m_vehicleAudio[i].pendingReleaseMove.store(-1, std::memory_order_relaxed);
            m_vehicleAudio[i].pendingRelease.store(false, std::memory_order_relaxed);
            continue;
        }

        int idleIdx = m_vehicleAudio[i].idleSourceIdx.load(std::memory_order_relaxed);
        int moveIdx = m_vehicleAudio[i].moveSourceIdx.load(std::memory_order_relaxed);

        if (idleIdx < 0 || moveIdx < 0) continue;  // slot free

        ALuint idleSrc = static_cast<ALuint>(m_sources[idleIdx]);
        ALuint moveSrc = static_cast<ALuint>(m_sources[moveIdx]);

        // ---------------------------------------------------------------
        // Process pendingInit: first wake after acquisition.
        // ---------------------------------------------------------------
        if (m_vehicleAudio[i].pendingInit.load(std::memory_order_relaxed) && buffersReady) {
            ALuint bufIdle = static_cast<ALuint>(it_idle->second);
            ALuint bufMove = static_cast<ALuint>(it_move->second);

            // Stop sources before binding buffers — AL_BUFFER on a PLAYING source
            // triggers AL_INVALID_OPERATION (reused SFX sources may still be playing).
            alSourceStop(idleSrc);
            alSourceStop(moveSrc);

            // Idle source: positional, looping, start at idle gain (speed=0).
            alSourcei (idleSrc, AL_SOURCE_RELATIVE, AL_FALSE);
            alCheckError_real("updateVehicleEngines:init:AL_SOURCE_RELATIVE(idle)");
            alSource3f(idleSrc, AL_VELOCITY,      0.f, 0.f, 0.f);
            alCheckError_real("updateVehicleEngines:init:AL_VELOCITY(idle)");
            alSourcef (idleSrc, AL_ROLLOFF_FACTOR, 1.f);
            alCheckError_real("updateVehicleEngines:init:AL_ROLLOFF_FACTOR(idle)");
            alSourcef (idleSrc, AL_REFERENCE_DISTANCE, 5.f);
            alCheckError_real("updateVehicleEngines:init:AL_REFERENCE_DISTANCE(idle)");
            alSourcef (idleSrc, AL_MAX_DISTANCE, 150.f);
            alCheckError_real("updateVehicleEngines:init:AL_MAX_DISTANCE(idle)");
            alSourcef (idleSrc, AL_GAIN,  1.0f);  // speed=0 → full idle gain
            alCheckError_real("updateVehicleEngines:init:AL_GAIN(idle)");
            float bp = m_vehicleAudio[i].basePitch.load(std::memory_order_relaxed);
            alSourcef (idleSrc, AL_PITCH, bp * 0.75f);  // stopped pitch
            alCheckError_real("updateVehicleEngines:init:AL_PITCH(idle)");
            alSourcei (idleSrc, AL_BUFFER,  static_cast<ALint>(bufIdle));
            alCheckError_real("updateVehicleEngines:init:AL_BUFFER(idle)");
            alSourcei (idleSrc, AL_LOOPING, AL_TRUE);
            alCheckError_real("updateVehicleEngines:init:AL_LOOPING(idle)");

            // Move source: positional, looping, start at zero gain (speed=0).
            alSourcei (moveSrc, AL_SOURCE_RELATIVE, AL_FALSE);
            alCheckError_real("updateVehicleEngines:init:AL_SOURCE_RELATIVE(move)");
            alSource3f(moveSrc, AL_VELOCITY,      0.f, 0.f, 0.f);
            alCheckError_real("updateVehicleEngines:init:AL_VELOCITY(move)");
            alSourcef (moveSrc, AL_ROLLOFF_FACTOR, 1.f);
            alCheckError_real("updateVehicleEngines:init:AL_ROLLOFF_FACTOR(move)");
            alSourcef (moveSrc, AL_REFERENCE_DISTANCE, 5.f);
            alCheckError_real("updateVehicleEngines:init:AL_REFERENCE_DISTANCE(move)");
            alSourcef (moveSrc, AL_MAX_DISTANCE, 150.f);
            alCheckError_real("updateVehicleEngines:init:AL_MAX_DISTANCE(move)");
            alSourcef (moveSrc, AL_GAIN,  0.0f);  // speed=0 → zero move gain
            alCheckError_real("updateVehicleEngines:init:AL_GAIN(move)");
            alSourcef (moveSrc, AL_PITCH, bp * 0.75f);
            alCheckError_real("updateVehicleEngines:init:AL_PITCH(move)");
            alSourcei (moveSrc, AL_BUFFER,  static_cast<ALint>(bufMove));
            alCheckError_real("updateVehicleEngines:init:AL_BUFFER(move)");
            alSourcei (moveSrc, AL_LOOPING, AL_TRUE);
            alCheckError_real("updateVehicleEngines:init:AL_LOOPING(move)");

            // Set initial position (may be 0,0 until first updateVehicleAudio frame).
            float wx = m_vehicleAudio[i].worldX.load(std::memory_order_relaxed);
            float wz = m_vehicleAudio[i].worldZ.load(std::memory_order_relaxed);
            alSource3f(idleSrc, AL_POSITION, wx, 0.f, wz);
            alCheckError_real("updateVehicleEngines:init:AL_POSITION(idle)");
            alSource3f(moveSrc, AL_POSITION, wx, 0.f, wz);
            alCheckError_real("updateVehicleEngines:init:AL_POSITION(move)");

            alSourcePlay(idleSrc);
            alCheckError_real("updateVehicleEngines:init:alSourcePlay(idle)");
            alSourcePlay(moveSrc);
            alCheckError_real("updateVehicleEngines:init:alSourcePlay(move)");

            m_vehicleAudio[i].pendingInit.store(false, std::memory_order_relaxed);
            continue;  // Position and pitch/gain are already set for this wake
        }

        // ---------------------------------------------------------------
        // Per-frame update: pitch, gain crossblend, position.
        // Only for fully-initialized sources (pendingInit == false).
        // ---------------------------------------------------------------
        if (m_vehicleAudio[i].pendingInit.load(std::memory_order_relaxed)) continue;

        float speed  = m_vehicleAudio[i].speedFraction.load(std::memory_order_relaxed);
        float bp     = m_vehicleAudio[i].basePitch.load(std::memory_order_relaxed);
        float wx     = m_vehicleAudio[i].worldX.load(std::memory_order_relaxed);
        float wz     = m_vehicleAudio[i].worldZ.load(std::memory_order_relaxed);

        // pitch = basePitch × lerp(0.75, 1.35, speedFraction)
        float pitch  = bp * (0.75f + speed * (1.35f - 0.75f));

        // gain crossblend: idle fades out as speed increases, move fades in.
        float gainIdle = 1.0f - speed;
        float gainMove = speed;

        alSourcef(idleSrc, AL_PITCH, pitch);
        alCheckError_real("updateVehicleEngines:AL_PITCH(idle)");
        alSourcef(moveSrc, AL_PITCH, pitch);
        alCheckError_real("updateVehicleEngines:AL_PITCH(move)");

        alSourcef(idleSrc, AL_GAIN, gainIdle * m_sfxVolume.load(std::memory_order_relaxed));
        alCheckError_real("updateVehicleEngines:AL_GAIN(idle)");
        alSourcef(moveSrc, AL_GAIN, gainMove * m_sfxVolume.load(std::memory_order_relaxed));
        alCheckError_real("updateVehicleEngines:AL_GAIN(move)");

        alSource3f(idleSrc, AL_POSITION, wx, 0.f, wz);
        alCheckError_real("updateVehicleEngines:AL_POSITION(idle)");
        alSource3f(moveSrc, AL_POSITION, wx, 0.f, wz);
        alCheckError_real("updateVehicleEngines:AL_POSITION(move)");
    }
}

// ---------------------------------------------------------------------------
// cleanupFinishedSFX — audio thread, called each wake after updateVehicleEngines.
//
// Reclaims SFX pool sources whose non-looping one-shot playback has ended.
// Runs exclusively on the audio thread so that:
//   - alGetSourcei(AL_SOURCE_STATE) and alSourcei(AL_BUFFER, 0) are never
//     called from the main thread concurrently with audio-thread AL calls.
//   - Errors from these calls cannot bleed into alCheckError_real and
//     falsely disable audio (the bug fixed by this refactor).
//
// Vehicle engine sources (SFX_VEHICLE_ENGINE_IDLE/MOVE) are skipped; they are
// looping and managed exclusively by updateVehicleEngines().
// ---------------------------------------------------------------------------
void AudioSystem::cleanupFinishedSFX() {
    for (int i = 0; i < kEvictableSFXCount; ++i) {
        if (!m_sfxSlots[i].occupied) continue;
        // Vehicle engine sources loop forever and are managed by updateVehicleEngines.
        // Use the atomic m_sfxVehicleReserved flag — reading the non-atomic
        // m_sfxSlots[i].soundId while the main thread writes it is a data race.
        // The acquire load pairs with the release store in acquireVehicleEnginePair.
        if (m_sfxVehicleReserved[i].load(std::memory_order_acquire)) continue;

        ALint state = AL_STOPPED;
        alGetSourcei(static_cast<ALuint>(m_sources[i]), AL_SOURCE_STATE, &state);
        alCheckError_real("cleanupFinishedSFX:alGetSourcei");
        if (state == AL_STOPPED) {
            alSourcei(static_cast<ALuint>(m_sources[i]), AL_BUFFER, 0);
            alCheckError_real("cleanupFinishedSFX:alSourcei(AL_BUFFER,0)");
            onSourceRecycled(i);
            m_sfxSlots[i] = SFXSlot{};
        }
    }
}
