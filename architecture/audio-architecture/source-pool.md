# Source Pool with Streaming Partition

```
Total AL sources pre-allocated: 62  (kTotalSources = 62, unchanged V1 and post-V1)

V1 layout:
  Evictable SFX pool:    sources[0..54]   (55 sources, priority-based eviction;
                                           each receives one EFX occlusion filter)
  Stinger reservation:   sources[55..56]  (2 sources, non-evictable, V1 stingers only)
                                           (kStingerCount = 2; no EFX filter)
  Reserved (post-V1):    sources[57]      (1 source — post-V1 game-over stinger slot;
                                           evictable in V1; no game-over stinger in V1;
                                           no EFX filter)
  Non-evictable streams: sources[58..61]  (4 sources, never evicted; no EFX filter)
    └─ streams[0..1]:  music stems (2 sources — outgoing + incoming crossfade)
    └─ streams[2..3]:  ambient bed layer (active + crossfade target)

Post-V1 layout (Scenario mode — when stinger_game_over is added):
  Evictable SFX pool:    sources[0..54]   (55 sources)  (kEvictableSFXCount = 55)
  Stinger reservation:   sources[55..57]  (3 sources)   (kStingerCount = 3)
  Non-evictable streams: sources[58..61]  (4 sources, unchanged)

Named constants (V1): kEvictableSFXCount = 55, kStingerCount = 2,
  kSFXPoolSize = 58 (evictable 55 + stingers 2 + reserved 1), kStreamSourceCount = 4,
  kTotalSources = 62.
```

**Rationale for kStreamSourceCount = 4**: At most 2 music stems are simultaneously active (outgoing + incoming crossfade) and at most 2 ambient bed sources are simultaneously active (active + crossfade target). 4 stream slots cover all simultaneous usage exactly. Allocating 6 stream slots (as in a prior draft) would permanently waste 2 non-evictable sources that can never be acquired by any code path.
Transient reserve: sources[51..54] (4 sources within the evictable pool) are soft-reserved for transient HIGH-priority SFX (vehicle horns, UI sounds, build/demolish sounds). Zone ambient loops (LOW priority) and vehicle/traffic sources (NORMAL priority) are excluded from acquiring these 4 slots. The reserve is enforced in `acquireSFXSource()`: LOW and NORMAL priority callers only consider sources[0..50]; **HIGH and CRITICAL** priority may consider sources[0..54]. **Rationale**: vehicle/traffic sources are NORMAL priority (up to 24 sources / 12 vehicles), which at large city sizes can fill sources[0..50] entirely. Without excluding NORMAL from [51..54], vehicle sources would absorb the transient reserve, starving HIGH-priority UI sounds (build/demolish confirmation, urgency tones). Only HIGH and CRITICAL — which cover short-lived transient events — may access the reserve. kTransientReserveStart = 51.

- `AudioSourcePool::acquireSFXSource(SoundPriority p)` — evictable; considers only `sources[0..50]` for **LOW and NORMAL priority**; considers `sources[0..54]` for **HIGH and CRITICAL priority**. The distinction between NORMAL and HIGH is both acquisition range AND eviction priority: NORMAL sources cannot acquire [51..54] (transient reserve), and when the pool is full NORMAL sources are evicted before HIGH, and HIGH before CRITICAL. CRITICAL sources are last to be evicted but can still be evicted by a subsequent CRITICAL acquisition if all slots are occupied by CRITICAL.
- `AudioSourcePool::acquireStreamSource()` — non-evictable; returns `AL_NONE` if all 4 in use
- **Ambient beds MUST use the stream partition** (`acquireStreamSource()`), not the SFX pool. Marking ambient beds CRITICAL in the evictable pool contradicts the eviction contract — if the pool is full of HIGH/CRITICAL SFX the ambient bed source could still be theoretically targeted. The stream partition guarantees they are never evicted.
- **Stinger source reservation — structurally enforced (V1)**: The eviction algorithm in `acquireSFXSource()` considers only `sources[0..54]` as eviction candidates (the general SFX pool below the stinger boundary). `sources[55..56]` are permanently reserved for V1 stingers and are NEVER returned by `acquireSFXSource()`. `sources[57]` is evictable in V1 (no game-over stinger). Stinger sources are acquired and released via a dedicated `acquireStingerSource(StingerType type)` accessor, where `StingerType` is an enum:
  ```cpp
  // V1: kStingerCount = 2
  enum class StingerType { CRISIS = 55, MILESTONE = 56 };
  // Post-V1 (Scenario mode): add GAME_OVER = 57
  ```
  `acquireStingerSource(StingerType)` returns the fixed source index for that stinger type directly. This makes the reservation structurally enforced in the pool implementation rather than relying on caller discipline (e.g., marking a SFX pool source CRITICAL does not prevent eviction if all general SFX slots are occupied by CRITICAL priority sounds).
- **Stinger source non-positional setup (mandatory at pool construction)**: During `AudioSourcePool` construction (after `alGenSources` creates all 62 sources), stinger sources (V1 indices 55..56) MUST have the following attributes set **immediately at pool initialization**, before any stinger is ever played:
  ```cpp
  // Set for each V1 stinger source index s in {55, 56}:
  // (Post-V1: extend to {55, 56, 57} when stinger_game_over is added)
  alSourcei(sources[s], AL_SOURCE_RELATIVE, AL_TRUE);  // non-positional
  alSource3f(sources[s], AL_POSITION, 0.f, 0.f, 0.f); // position at origin (required when SOURCE_RELATIVE is true)
  alSourcef(sources[s], AL_ROLLOFF_FACTOR, 0.f);       // no distance attenuation
  alSource3f(sources[s], AL_VELOCITY, 0.f, 0.f, 0.f);  // no Doppler
  ```
  **Why at construction, not at acquire time**: `acquireStingerSource()` is called on the audio thread at the moment the stinger fires. Setting up source attributes on the audio thread at acquire time is correct, but if these attributes were omitted, a stinger fired in quick succession might use stale positional attributes from a previous audio state. Setting them at construction ensures these attributes are always correct regardless of how many times the source is reused. Stream sources (indices 58..61) have `AL_SOURCE_RELATIVE = AL_TRUE` set by the streaming subsystem during `AudioStream` initialization; the pool does not set attributes for stream sources.

### SoundPriority Enum

```cpp
enum class SoundPriority { LOW = 0, NORMAL = 1, HIGH = 2, CRITICAL = 3 };
```
- Eviction selects the **lowest-priority** source with the **greatest distance** from the listener (distance-weighted tiebreak)
- Ambient beds: use the **stream partition** (non-evictable, `acquireStreamSource()`); not assigned a SFX pool priority
- Service events (fire, police): CRITICAL
- Traffic/vehicle **engine sounds** (`sfx_vehicle_engine_idle`, `sfx_vehicle_engine_move`): NORMAL
- **Vehicle horn** (`sfx_vehicle_horn`): **HIGH** — horns are brief transient events that must access the transient reserve (sources[51..54]). NORMAL priority cannot access the reserve and would be starved by up-to-24 engine sources filling sources[0..50]. See V1 Audio Asset Manifest for horn source cap and cooldown rules.
- UI sounds: HIGH (short, must complete)
- Zone ambient loops: LOW (expendable at distance)
- `playSound()` interface requires `SoundPriority` parameter
- Streaming sources (`AudioStream` objects) always use `acquireStreamSource()`

### EFX Occlusion Filter Allocation Scope

**EFX lowpass filters are allocated only for evictable SFX pool sources (indices 0..54 = `kEvictableSFXCount` general-SFX boundary).**  Stinger sources (V1 indices 55..56), the reserved post-V1 slot (index 57), and stream sources (indices 58..61) do NOT receive occlusion filters:
- Stinger sources (55..56) are one-shot non-positional WAV sounds (`AL_SOURCE_RELATIVE = AL_TRUE`) — occlusion filtering is not applicable.
- Reserved slot (57) is evictable in V1 but non-positional by design — no EFX filter allocated.
- Stream sources are non-positional stereo (`AL_SOURCE_RELATIVE = AL_TRUE`) — occlusion filtering is not applicable.

**Critical loop bound**: All EFX filter allocation loops, cleanup loops, and occlusion update loops MUST iterate over `kEvictableSFXCount` (55 in V1), NOT `kSFXPoolSize` (58) or `kTotalSources` (62). In V1 the `m_occlusionFilter[]` array is sized `kEvictableSFXCount = 55`; the loop runs over indices 0..54 (55 iterations). Iterating beyond index 54 accesses memory outside the array and produces undefined behavior. Sources[55..57] (stingers and reserved slot) and sources[58..61] (streams) do not receive EFX occlusion filters because they are non-positional sources. See Audio Thread Shutdown spec for the mandatory filter cleanup loop bound.

### Source Budget Allocation

| Category | Sources | Notes |
|---|---|---|
| Music / adaptive stems | 2 (stream partition, sources[58..59]) | Outgoing + incoming crossfade target; at most 2 simultaneously active |
| Ambient bed layer | 2 (stream partition, sources[60..61]) | Active + crossfade target; non-evictable |
| City zone layer | 16 | Distance-culled; max active radius 300 m from camera |
| Traffic / vehicle SFX | 24 | LOD-cull at > 150 m from listener; max 12 simultaneous vehicles (24 slots ÷ 2 sources per vehicle); idle and move sources acquired/released as atomic pair |
| Service events (fire, police, etc.) | 8 | CRITICAL priority; last to be evicted; evicted only when all general evictable SFX slots are occupied by CRITICAL-priority sources |
| UI / notification sounds | 4 | `AL_SOURCE_RELATIVE = AL_TRUE`, non-positional; caller must set `alSourcei(src, AL_SOURCE_RELATIVE, AL_TRUE)` and `alSource3f(src, AL_POSITION, 0,0,0)` immediately after `acquireSFXSource()` returns |
| Musical stingers (V1: crisis/milestone) | 2 | Reserved non-evictable SFX pool slots (indices 55/56); WAV PCM pre-loaded; acquired via `acquireStingerSource(StingerType)` only. Post-V1: game-over stinger added at index 57 (kStingerCount=3). |
| Reserve (incl. post-V1 game-over slot) | 4 | Burst headroom; sources[57] is the post-V1 game-over stinger slot (evictable in V1). The 4 counts: 3 evictable-pool burst slots beyond the named categories (55 total evictable − 16 zone − 24 traffic − 8 service − 4 UI = 3) plus sources[57] = 4 total. Row sum check: 2+2+16+24+8+4+2+4 = 62 = kTotalSources. |

### Vehicle Engine Source Constraints

The 24 Traffic/Vehicle SFX pool slots support a maximum of **12 simultaneously-audible vehicles** because each vehicle requires 2 sources (one for `sfx_vehicle_engine_idle`, one for `sfx_vehicle_engine_move`, crossblended in real time).

- **Paired acquisition**: `AudioSourcePool::acquireVehicleEnginePair(ALuint& outIdle, ALuint& outMove)` must atomically reserve 2 SFX pool slots or return `false` and reserve neither. This prevents orphaned single-source vehicles.
- **Paired release**: `AudioSourcePool::releaseVehicleEnginePair(ALuint idle, ALuint move)` returns both sources to the pool atomically. Never release only one of a pair.
- **Eviction unit**: When the pool must evict to satisfy a new vehicle acquisition request, the eviction candidate selection must find the lowest-priority vehicle pair (both sources share the same vehicle entity priority and distance) and evict both together.
- **Audio LOD cull**: Vehicle engine sources are culled (pair released) when the vehicle exceeds **150 m** from the listener. This aligns with the `AL_INVERSE_DISTANCE_CLAMPED` max distance of 150 m for traffic/vehicles — beyond this distance the attenuation model produces inaudible output anyway.
- **Re-entry**: When a culled vehicle re-enters the 150 m range, a new `acquireVehicleEnginePair()` call is attempted. If the pool is full (12 vehicles already active), the re-entering vehicle receives no engine audio until a slot becomes available via eviction.

### Vehicle Engine Source Pairing — Internal Tracking

`AudioSourcePool` maintains a pair-tracking table to enable atomic pair eviction:

```cpp
// Internal to AudioSourcePool — not part of IAudioSystem interface:
struct VehiclePairSlot {
    int  idleSourceIdx{-1};   // index into sources[] for the idle source (-1 = unused)
    int  moveSourceIdx{-1};   // index into sources[] for the move source (-1 = unused)
    float listenerDistanceSq{0.f}; // cached squared distance at last update (for eviction selection)
    int   priority{0};        // copied from the vehicle entity priority at acquire time
};
std::array<VehiclePairSlot, 12> m_vehiclePairs{};  // up to 12 active vehicles (kMaxVehiclePairs = 12)
```

**`acquireVehicleEnginePair()` — atomic acquisition with eviction**:

1. Scan `m_vehiclePairs` for the first entry with `idleSourceIdx == -1` (an unused pair slot).
2. If a free pair slot exists, find 2 free sources from the NORMAL-priority evictable SFX range (`sources[0..50]`) and assign them. Return the pair index.
3. If NO free pair slot exists (all 12 vehicles active), select the eviction candidate: the pair with the **lowest combined priority** and, as a tiebreak, the **greatest average listener distance squared**. Evict both sources of the candidate pair:
   - Call `alSourceStop` on both source indices.
   - Call `onSourceRecycled(i)` for each (resets occlusion state).
   - Mark both `idleSourceIdx` and `moveSourceIdx` back to free in the pool.
   - Clear the `VehiclePairSlot` entry.
4. Assign the newly-freed pair slot to the incoming vehicle. Return the pair index.
5. **Partial acquisition is prohibited**: if either source index within the pair cannot be acquired after eviction (implementation error), the entire acquisition fails — call `onSourceRecycled` on any partially-acquired source, mark it free, and return `(-1, -1)`.

**`releaseVehicleEnginePair(int pairIdx)`**:

1. Look up `m_vehiclePairs[pairIdx]`.
2. Call `onSourceRecycled(idleSourceIdx)` and `onSourceRecycled(moveSourceIdx)`.
3. Return both source indices to the free pool.
4. Reset `m_vehiclePairs[pairIdx]` to `{-1, -1, 0.f, 0}`.

Releasing only one source of a pair (e.g., on LOD cull) is prohibited. The cull path must call `releaseVehicleEnginePair(pairIdx)` — never free individual sources from a pair via `acquireSFXSource`/release paths.
