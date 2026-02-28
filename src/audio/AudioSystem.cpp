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

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

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
// Static logging helpers.
// ---------------------------------------------------------------------------
void AudioSystem::logWarning(const std::string& msg) {
    std::cerr << "[AudioSystem WARNING] " << msg << '\n';
}
void AudioSystem::logError(const std::string& msg) {
    std::cerr << "[AudioSystem ERROR] " << msg << '\n';
}
void AudioSystem::logInfo(const std::string& msg) {
    std::cerr << "[AudioSystem INFO] " << msg << '\n';
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
AudioSystem::AudioSystem(IClock* clock, IAlcFunctions* alcFunctions)
    : m_clock(clock)
{
    // -----------------------------------------------------------------------
    // Step 1: Open device.
    // -----------------------------------------------------------------------
    m_device = alcOpenDevice(nullptr);
    if (!m_device) {
        throw std::runtime_error("alcOpenDevice failed — no audio device available");
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
    // Pre-load vehicle engine loops (OGG, mono, 6–20 s).
    m_preloadQueue.push_back({SFX_VEHICLE_ENGINE_IDLE,
                              assetPath("sfx_vehicle_engine_idle.ogg"), true});
    m_preloadQueue.push_back({SFX_VEHICLE_ENGINE_MOVE,
                              assetPath("sfx_vehicle_engine_move.ogg"), true});
    // Pre-load stinger WAVs.
    m_preloadQueue.push_back({SFX_STINGER_CRISIS,
                              assetPath("stinger_crisis.wav"), false});
    m_preloadQueue.push_back({SFX_STINGER_MILESTONE,
                              assetPath("stinger_milestone.wav"), false});

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

        // Compute real-time dt using IClock (not fixed 0.01f).
        double now = m_clock->nowSeconds();
        float  dt  = static_cast<float>(now - m_lastDuckWakeTime);
        m_lastDuckWakeTime = now;

        updateStreams();
        updateOcclusion();
        updateDuckState(dt);
    }
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
    if (s.isOpen && s.vf) {
        AudioStreamUtils::closeOGG(s.vf);
        s.isOpen = false;
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

    s.isMusicStem     = isMusicStem;
    s.m_samplesQueued = 0;
    s.m_nextBarBoundary = 0;
    s.isOpen          = true;
    s.m_intentionallyStopped = false;
    s.crossfadeGain   = 1.0f;

    if (isMusicStem) {
        if (!loadMusicSidecar(path, s.bpm, s.beatsPerBar)) {
            // Missing/invalid sidecar for music stem is a hard error.
            AudioStreamUtils::closeOGG(s.vf);
            s.isOpen = false;
            throw std::runtime_error("Missing or invalid sidecar for: " + path);
        }
    }

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
            // EOF — loop by seeking to sample 0.
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
        {
            std::lock_guard<std::mutex> lk(m_streamMutex);
            alBufferData(bufHandle, format, pcmBuf.data(), byteCount,
                         static_cast<ALsizei>(sr));
            alSourceQueueBuffers(src, 1, &bufHandle);

            s.m_samplesQueued += static_cast<uint64_t>(framesDecoded);
            ++queued;

            // Starvation recovery: if source stopped unintentionally, restart it.
            ALint state = AL_STOPPED;
            alGetSourcei(src, AL_SOURCE_STATE, &state);
            if (state == AL_STOPPED && !s.m_intentionallyStopped) {
                logWarning("Stream starvation recovery — restarting source " +
                           std::to_string(slot));
                alSourcePlay(src);
                // Reset sample counter to avoid stale bar-boundary calculation.
                // Count buffers now queued after the recovery requeue.
                ALint bq2 = 0;
                alGetSourcei(src, AL_BUFFERS_QUEUED, &bq2);
                s.m_samplesQueued = static_cast<uint64_t>(bq2) *
                                    AudioStream::kSamplesPerBuffer;
            }
        }
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

        // Crossfade check.
        uint64_t sp = AudioStream::computeSamplesPlayed(s.m_samplesQueued, buffersQueued);
        if (sp >= s.m_nextBarBoundary && s.m_nextBarBoundary > 0) {
            // Bar boundary crossed — advance to next boundary.
            uint64_t spb = static_cast<uint64_t>(
                (44100.0 * 60.0 / s.bpm) * s.beatsPerBar);
            s.m_nextBarBoundary = ((sp / spb) + 1) * spb;
            // TODO Phase 10: fire crossfade command here.
        }
    }

    return queued;
}

// ---------------------------------------------------------------------------
// updateStreams — called once per audio thread wake.
// ---------------------------------------------------------------------------
void AudioSystem::updateStreams() {
    for (int i = 0; i < kStreamSourceCount; ++i) {
        if (!m_streams[i].isOpen) continue;

        // Proactive starvation prevention: refill buffers on every wake.
        refillStream(i);

        // Apply crossfade gain (music duck applied multiplicatively).
        float gain = m_streams[i].crossfadeGain;
        if (i < 2) {
            // Music stem — apply duck gain.
            gain *= m_musicDuckGain.load(std::memory_order_relaxed);
        }
        alSourcef(static_cast<ALuint>(m_streams[i].sourceHandle),
                  AL_GAIN, gain);
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
// Called from main thread (pool eviction / release); must hold m_occlusionMutex.
// ---------------------------------------------------------------------------
void AudioSystem::onSourceRecycled(int i) {
    if (i < 0 || i >= kEvictableSFXCount) return;

    std::lock_guard<std::mutex> lk(m_occlusionMutex);
    m_occlusionGainCurrent[i] = 1.0f;
    m_occlusionGainTarget[i].store(1.0f, std::memory_order_relaxed);

    if (m_efxAvailable) {
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
    if (id == MUSIC_INVALID) return;
    std::string filename = musicTrackFilename(id);
    if (filename.empty()) {
        logError("setMusicTrack: unknown MusicTrackId " + std::to_string(id));
        return;
    }

    // Simple V1 implementation: start on stream slot 0 if not open, crossfade otherwise.
    std::string path = assetPath(filename);

    // Lock while modifying stream state.
    std::lock_guard<std::mutex> lk(m_streamMutex);

    // Open on stream slot 0 (music stems use slots 0..1).
    if (!m_streams[0].isOpen) {
        if (openStreamOGG(0, path, true)) {
            ALuint src = static_cast<ALuint>(m_streams[0].sourceHandle);
            alSourcei(src, AL_LOOPING, AL_FALSE);
            alSourcePlay(src);
        }
    }
    // TODO Phase 10: full crossfade between slots 0 and 1.
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
    m_currentTimeOfDay = tod;
    m_timeOfDaySet     = true;
    // TODO Phase 10: crossfade ambient bed to the correct one for this time period.
    // For now the ambient bed is started in transitionToGameplay().
}

// ---------------------------------------------------------------------------
// transitionToGameplay — crossfade from main menu music to gameplay audio.
// ---------------------------------------------------------------------------
void AudioSystem::transitionToGameplay() {
    // V1: Start default calm music stem and ambient bed for current time of day.
    // TODO Phase 10: 1 s crossfade from main menu music.

    // Start ambient bed on slot 2 (sources[60]).
    std::string ambPath = assetPath(ambientBedFilename(m_currentTimeOfDay));
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
}

// ---------------------------------------------------------------------------
// update — per-frame main-thread update.
// ---------------------------------------------------------------------------
void AudioSystem::update(float /*realDeltaSeconds*/) {
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

    // Clean up finished (non-looping) SFX sources to keep the pool available.
    for (int i = 0; i < kEvictableSFXCount; ++i) {
        if (!m_sfxSlots[i].occupied) continue;
        ALint state = AL_STOPPED;
        alGetSourcei(static_cast<ALuint>(m_sources[i]),
                     AL_SOURCE_STATE, &state);
        if (state == AL_STOPPED) {
            alSourcei(static_cast<ALuint>(m_sources[i]), AL_BUFFER, 0);
            onSourceRecycled(i);
            m_sfxSlots[i] = SFXSlot{};
        }
    }
}

// ---------------------------------------------------------------------------
// setMasterVolume — applied immediately via alListenerf(AL_GAIN) on the
// calling thread (main thread).  m_masterVolume is plain float because it
// is only ever read/written from the main thread.
// ---------------------------------------------------------------------------
void AudioSystem::setMasterVolume(float gain) {
    m_masterVolume = gain;
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
