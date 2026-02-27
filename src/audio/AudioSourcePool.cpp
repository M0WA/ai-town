// AudioSourcePool.cpp — Source pool implementation.
#include "src/audio/AudioSourcePool.h"
#include "src/audio/AudioSystem.h"   // for onSourceRecycled forward call

#include <AL/al.h>
#include <algorithm>
#include <cassert>
#include <limits>

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
AudioSourcePool::AudioSourcePool(
    unsigned int* sources,
    float*        occlusionGainCurrent,
    void*         audioSystem)
    : m_sources(sources)
    , m_occlusionGainCurrent(occlusionGainCurrent)
    , m_audioSystem(audioSystem)
{
    // All m_sfxSlots default-constructed to {occupied=false, ...}.
    // m_streamOccupied initialized to false.
    // m_vehiclePairs initialized to {-1,-1,0.f,0} via std::array default.
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------
int AudioSourcePool::findFreeSFXSource(int upperBound) const {
    for (int i = 0; i < upperBound; ++i) {
        if (!m_sfxSlots[i].occupied) return i;
    }
    return -1;
}

int AudioSourcePool::findEvictionCandidate(int upperBound, SoundPriority callerPriority) const {
    int   bestIdx  = -1;
    int   bestPri  = static_cast<int>(callerPriority);   // only evict strictly lower priority
    float bestDist = -1.f;

    for (int i = 0; i < upperBound; ++i) {
        if (!m_sfxSlots[i].occupied) continue;
        int pri = static_cast<int>(m_sfxSlots[i].priority);
        // Evict lowest priority; distance as tiebreak.
        if (pri < bestPri || (pri == bestPri && m_sfxSlots[i].listenerDistanceSq > bestDist)) {
            bestIdx  = i;
            bestPri  = pri;
            bestDist = m_sfxSlots[i].listenerDistanceSq;
        }
    }
    return bestIdx;
}

void AudioSourcePool::callOnSourceRecycled(int idx) {
    // Cast through void* to avoid circular header include.
    reinterpret_cast<AudioSystem*>(m_audioSystem)->onSourceRecycled(idx);
}

// ---------------------------------------------------------------------------
// SFX source acquisition
// ---------------------------------------------------------------------------
int AudioSourcePool::acquireSFXSource(SoundPriority priority, float listenerDistanceSq) {
    // Determine range based on priority.
    // LOW/NORMAL: [0..kTransientReserveStart-1] = [0..50]
    // HIGH/CRITICAL: [0..kEvictableSFXCount-1] = [0..54]
    int upperBound = (priority == SoundPriority::LOW || priority == SoundPriority::NORMAL)
                     ? kTransientReserveStart
                     : kEvictableSFXCount;

    // Try to find a free slot.
    int idx = findFreeSFXSource(upperBound);
    if (idx >= 0) {
        m_sfxSlots[idx].occupied        = true;
        m_sfxSlots[idx].priority        = priority;
        m_sfxSlots[idx].listenerDistanceSq = listenerDistanceSq;
        m_sfxSlots[idx].buffer          = 0;
        return idx;
    }

    // No free slot — try eviction.
    int evictIdx = findEvictionCandidate(upperBound, priority);
    if (evictIdx < 0) {
        // All slots are >= caller priority — cannot evict.
        return -1;
    }

    // Evict the candidate.
    alSourceStop(m_sources[evictIdx]);
    alSourcei(m_sources[evictIdx], AL_BUFFER, 0);
    callOnSourceRecycled(evictIdx);

    m_sfxSlots[evictIdx].occupied        = true;
    m_sfxSlots[evictIdx].priority        = priority;
    m_sfxSlots[evictIdx].listenerDistanceSq = listenerDistanceSq;
    m_sfxSlots[evictIdx].buffer          = 0;
    return evictIdx;
}

void AudioSourcePool::releaseSFXSource(int idx) {
    if (idx < 0 || idx >= kEvictableSFXCount) return;
    alSourceStop(m_sources[idx]);
    alSourcei(m_sources[idx], AL_BUFFER, 0);
    callOnSourceRecycled(idx);
    m_sfxSlots[idx] = SFXSourceSlot{};
}

void AudioSourcePool::markOccupied(int idx, SoundPriority priority,
                                    float distanceSq, unsigned int buffer) {
    if (idx < 0 || idx >= kEvictableSFXCount) return;
    m_sfxSlots[idx].occupied           = true;
    m_sfxSlots[idx].priority           = priority;
    m_sfxSlots[idx].listenerDistanceSq = distanceSq;
    m_sfxSlots[idx].buffer             = buffer;
}

// ---------------------------------------------------------------------------
// Stream source acquisition
// ---------------------------------------------------------------------------
int AudioSourcePool::acquireStreamSource() {
    // Stream sources occupy indices kSFXPoolSize..kTotalSources-1 = 58..61.
    // m_streamOccupied[0..3] maps to sources[58..61].
    for (int i = 0; i < kStreamSourceCount; ++i) {
        if (!m_streamOccupied[i]) {
            m_streamOccupied[i] = true;
            return kSFXPoolSize + i;
        }
    }
    return -1;  // AL_NONE equivalent
}

void AudioSourcePool::releaseStreamSource(int idx) {
    int slot = idx - kSFXPoolSize;
    if (slot < 0 || slot >= kStreamSourceCount) return;
    m_streamOccupied[slot] = false;
}

// ---------------------------------------------------------------------------
// Stinger source acquisition
// ---------------------------------------------------------------------------
int AudioSourcePool::acquireStingerSource(StingerType type) {
    // StingerType enum values ARE the pool indices (intentional coupling per spec).
    return static_cast<int>(type);
}

// ---------------------------------------------------------------------------
// Vehicle engine pair acquisition
// ---------------------------------------------------------------------------
int AudioSourcePool::acquireVehicleEnginePair(int& outIdle, int& outMove,
                                               float listenerDistanceSq,
                                               int vehiclePriority) {
    // Find a free pair slot.
    int freeSlot = -1;
    for (int i = 0; i < kMaxVehiclePairs; ++i) {
        if (m_vehiclePairs[i].idleSourceIdx == -1) {
            freeSlot = i;
            break;
        }
    }

    if (freeSlot < 0) {
        // All pair slots occupied — evict the lowest-priority / greatest-distance pair.
        int   evictSlot  = -1;
        int   evictPri   = vehiclePriority + 1; // evict only if strictly lower
        float evictDist  = -1.f;
        for (int i = 0; i < kMaxVehiclePairs; ++i) {
            int pri = m_vehiclePairs[i].priority;
            float dist = m_vehiclePairs[i].listenerDistanceSq;
            if (pri < evictPri || (pri == evictPri && dist > evictDist)) {
                evictSlot = i;
                evictPri  = pri;
                evictDist = dist;
            }
        }
        if (evictSlot < 0) return -1;  // Cannot evict

        // Evict both sources of the candidate pair.
        int idleIdx = m_vehiclePairs[evictSlot].idleSourceIdx;
        int moveIdx = m_vehiclePairs[evictSlot].moveSourceIdx;
        alSourceStop(m_sources[idleIdx]);
        alSourceStop(m_sources[moveIdx]);
        alSourcei(m_sources[idleIdx], AL_BUFFER, 0);
        alSourcei(m_sources[moveIdx], AL_BUFFER, 0);
        callOnSourceRecycled(idleIdx);
        callOnSourceRecycled(moveIdx);
        m_sfxSlots[idleIdx] = SFXSourceSlot{};
        m_sfxSlots[moveIdx] = SFXSourceSlot{};
        m_vehiclePairs[evictSlot] = VehiclePairSlot{};
        freeSlot = evictSlot;
    }

    // Acquire two free sources within [0..kTransientReserveStart-1] (NORMAL range).
    int idleIdx = -1, moveIdx = -1;
    for (int i = 0; i < kTransientReserveStart && (idleIdx < 0 || moveIdx < 0); ++i) {
        if (!m_sfxSlots[i].occupied) {
            if (idleIdx < 0)       idleIdx = i;
            else if (moveIdx < 0)  moveIdx = i;
        }
    }

    if (idleIdx < 0 || moveIdx < 0) {
        // Partial acquisition — prohibited. Return nothing.
        if (idleIdx >= 0) { m_sfxSlots[idleIdx] = SFXSourceSlot{}; }
        return -1;
    }

    m_sfxSlots[idleIdx].occupied           = true;
    m_sfxSlots[idleIdx].priority           = SoundPriority::NORMAL;
    m_sfxSlots[idleIdx].listenerDistanceSq = listenerDistanceSq;
    m_sfxSlots[moveIdx].occupied           = true;
    m_sfxSlots[moveIdx].priority           = SoundPriority::NORMAL;
    m_sfxSlots[moveIdx].listenerDistanceSq = listenerDistanceSq;

    m_vehiclePairs[freeSlot] = VehiclePairSlot{idleIdx, moveIdx, listenerDistanceSq, vehiclePriority};

    outIdle = idleIdx;
    outMove = moveIdx;
    return freeSlot;
}

void AudioSourcePool::releaseVehicleEnginePair(int pairIdx) {
    if (pairIdx < 0 || pairIdx >= kMaxVehiclePairs) return;
    int idleIdx = m_vehiclePairs[pairIdx].idleSourceIdx;
    int moveIdx = m_vehiclePairs[pairIdx].moveSourceIdx;
    if (idleIdx >= 0) {
        alSourceStop(m_sources[idleIdx]);
        alSourcei(m_sources[idleIdx], AL_BUFFER, 0);
        callOnSourceRecycled(idleIdx);
        m_sfxSlots[idleIdx] = SFXSourceSlot{};
    }
    if (moveIdx >= 0) {
        alSourceStop(m_sources[moveIdx]);
        alSourcei(m_sources[moveIdx], AL_BUFFER, 0);
        callOnSourceRecycled(moveIdx);
        m_sfxSlots[moveIdx] = SFXSourceSlot{};
    }
    m_vehiclePairs[pairIdx] = VehiclePairSlot{};
}

void AudioSourcePool::updateSFXSlotDistance(int idx, float distanceSq) {
    if (idx >= 0 && idx < kEvictableSFXCount) {
        m_sfxSlots[idx].listenerDistanceSq = distanceSq;
    }
}

void AudioSourcePool::updateVehiclePairDistance(int pairIdx, float distanceSq) {
    if (pairIdx >= 0 && pairIdx < kMaxVehiclePairs) {
        m_vehiclePairs[pairIdx].listenerDistanceSq = distanceSq;
    }
}
