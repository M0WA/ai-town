#pragma once
// AudioSourcePool.h — Source pool management for AudioSystem.
//
// Manages the 62-source pool with priority-based SFX eviction, stinger slot
// reservation, stream source reservation, and vehicle pair tracking.
//
// This header includes ZERO OpenAL headers — AL types are represented as
// unsigned int (ALuint) to preserve headless-CI compilation.
//
// AudioSystem owns a private AudioSourcePool m_pool member as the single source
// of truth for all 62 AL source handles.  playSound/playPositionalSound delegate
// to m_pool.acquireSFXSource(priority) with no inline pool scanning.
// Vehicle engine acquisition/release delegates to
// m_pool.acquireVehicleEnginePair / releaseVehicleEnginePair.

#include "src/interfaces/audio_types.h"
#include <array>
#include <cstdint>

// Forward declaration — avoids circular include cycle between AudioSourcePool.h
// and AudioSystem.h.  The full definition is only needed in AudioSourcePool.cpp.
class AudioSystem;

// ---------------------------------------------------------------------------
// VehiclePairSlot — internal tracking for one active vehicle engine pair.
// Defined here (not in AudioSystem.h) because AudioSourcePool owns the pair
// tracking array; AudioSystem.h includes AudioSourcePool.h to get this type.
// ---------------------------------------------------------------------------
struct VehiclePairSlot {
    int   idleSourceIdx{-1};       // index into m_sources[] (-1 = unused)
    int   moveSourceIdx{-1};       // index into m_sources[] (-1 = unused)
    float listenerDistanceSq{0.f}; // cached squared listener distance (for eviction)
    int   priority{0};             // vehicle entity priority at acquire time
};

// PoolSFXEntry — internal per-source state for the evictable SFX pool.
// Renamed from SFXSourceSlot (B-18) to distinguish from AudioSystem::SFXSlot.
struct PoolSFXEntry {
    bool          occupied{false};
    SoundPriority priority{SoundPriority::LOW};
    float         listenerDistanceSq{0.f};
    unsigned int  buffer{0};   // ALuint — static buffer currently bound
};

// AudioSourcePool — manages source lifecycle within AudioSystem.
//
// AudioSystem owns all 62 ALuint source handles in m_sources[]; AudioSourcePool
// operates on references to that array (passed at construction) so there is one
// canonical handle array.
//
// All methods are called on the main thread ONLY (no internal mutex — callers
// must serialize if necessary, or rely on the design that pool operations are
// main-thread-only in V1).
class AudioSourcePool {
public:
    // src points to AudioSystem::m_sources[kTotalSources].
    // occlGainCur / occlGainTgt: pointers into AudioSystem occlusion arrays.
    // The AudioSystem pointer is used to call onSourceRecycled().
    explicit AudioSourcePool(
        unsigned int* sources,
        float*        occlusionGainCurrent,
        AudioSystem*  audioSystem
    );

    // Acquire one evictable SFX source index.
    // LOW/NORMAL: consider only sources[0..kTransientReserveStart-1] (= [0..50]).
    // HIGH/CRITICAL: consider sources[0..kEvictableSFXCount-1]       (= [0..54]).
    // Evicts the lowest-priority / greatest-distance source if pool is full.
    // Returns source index, or -1 on failure.
    [[nodiscard]] int acquireSFXSource(SoundPriority priority, float listenerDistanceSq);

    // Release an evictable SFX source by index (includes AL stop + buffer detach).
    void releaseSFXSource(int idx);

    // Notify the pool that slot idx was already stopped and detached by the audio thread
    // (cleanupFinishedSFX). Resets only the pool's occupied tracking without making AL calls.
    void notifyFreed(int idx);

    // Acquire a stream source (indices 58..61); returns index or -1 if all in use.
    [[nodiscard]] int acquireStreamSource();

    // Release a stream source by index.
    void releaseStreamSource(int idx);

    // Acquire the stinger source for the given type (fixed index from StingerType value).
    // Returns the source index (e.g., 55 for CRISIS, 56 for MILESTONE).
    [[nodiscard]] int acquireStingerSource(StingerType type);

    // Acquire two SFX pool sources atomically as a vehicle engine pair.
    // Both sources are from sources[0..kTransientReserveStart-1] (NORMAL priority range).
    // On success: fills outIdle and outMove source indices, returns pair slot index [0..11].
    // On failure (pool exhausted or no free pair slot after eviction): returns -1.
    [[nodiscard]] int acquireVehicleEnginePair(int& outIdle, int& outMove,
                                               float listenerDistanceSq, int vehiclePriority);

    // Release a vehicle engine pair by pair slot index.
    void releaseVehicleEnginePair(int pairIdx);

    // Update listener distance for an occupied SFX slot (for eviction tiebreaking).
    void updateSFXSlotDistance(int idx, float distanceSq);

    // Update listener distance for a vehicle pair (for eviction tiebreaking).
    void updateVehiclePairDistance(int pairIdx, float distanceSq);

    // Mark an SFX slot as occupied with given metadata.
    void markOccupied(int idx, SoundPriority priority, float distanceSq, unsigned int buffer);

private:
    unsigned int* m_sources;              // non-owning, points into AudioSystem::m_sources[]
    float*        m_occlusionGainCurrent; // non-owning
    AudioSystem*  m_audioSystem;          // non-owning

    PoolSFXEntry m_sfxSlots[kEvictableSFXCount];
    bool          m_streamOccupied[kStreamSourceCount]{};

    std::array<VehiclePairSlot, kMaxVehiclePairs> m_vehiclePairs{};

    // Find a free SFX source within [0, upperBound).
    // Returns index, or -1 if none free.
    [[nodiscard]] int findFreeSFXSource(int upperBound) const;

    // Find the best eviction candidate within [0, upperBound):
    // lowest priority, greatest distance as tiebreak.
    // Returns index, or -1 if none evictable (shouldn't happen).
    [[nodiscard]] int findEvictionCandidate(int upperBound, SoundPriority callerPriority) const;

    // Call AudioSystem::onSourceRecycled() for the given source index.
    void callOnSourceRecycled(int idx);
};
