# Audio Architecture Specification Review
## Senior C++ Developer (OpenAL Soft) — Technical Gap Analysis

**Scope**: All files under `architecture/audio-architecture/`, `architecture/testing/testability-architecture.md`, and `architecture/testing/framework.md`.

**Reviewer perspective**: OpenAL Soft C++ implementation — correctness, thread safety, resource lifecycle, test coverage.

---

## Summary Statistics

| Severity | Count |
|---|---|
| CRITICAL | 7 |
| HIGH | 12 |
| MEDIUM | 9 |
| LOW | 6 |
| DUPLICATE | 5 |

---

## CRITICAL Issues

---

### CRIT-1 — `onSourceRecycled` AL calls made from main thread with no current context guarantee

**File**: `audio-occlusion.md` (§ Pool slot recycle — mandatory occlusion state reset)
**Severity**: CRITICAL

**Description**: The `onSourceRecycled(i)` spec calls `m_fnFilterf`, `alSourcei(AL_DIRECT_FILTER)`, and `alCheckError` from the **main thread** (at SFX pool acquisition time). The spec justifies this with the process-wide context established by `alcMakeContextCurrent(m_context)` in the constructor. However, that process-wide context is only guaranteed to be current until `alcMakeContextCurrent(nullptr)` is called — the destructor teardown sequence calls that in Step 6. During the narrow race window where the main thread calls `onSourceRecycled` while the destructor is executing Step 6 (after the audio thread has joined but before AL resource deletion is complete), the main thread makes AL calls with no current context, producing undefined behaviour.

More importantly, the spec in `audio-occlusion.md` explicitly states "called from the main thread at SFX pool acquisition time" and that `m_occlusionMutex` must be held — but `acquireSFXSource()` is callable at any time including between audio thread join and context destruction. The audio thread join in destructor step 3, followed by `alcMakeContextCurrent(m_context)` in step 3.5, followed by source stop calls — all happen while `acquireSFXSource()` is theoretically still callable from the game loop (which continues until `AudioSystem` is destroyed).

**The real gap**: there is no spec for quiescing callers before destruction begins. The destructor spec (`audio-thread-shutdown.md`) does not address the requirement to stop all main-thread AL callers (including `onSourceRecycled`, `syncListenerToCamera`, `playSound`, etc.) before step 3.5 re-binds the context. If any of these are called concurrently with the destructor, they race against the context teardown sequence.

**Proposed resolution**: Add a spec section to `audio-thread-shutdown.md` describing how the main-thread AL call window is closed: a `m_deviceLost`-style atomic `m_shutdownInitiated` flag must be checked at the top of every main-thread AL path (`onSourceRecycled`, `syncListenerToCamera`, `playSound`, `playPositionalSound`, `acquireSFXSource`). Set `m_shutdownInitiated.store(true)` as the very first destructor step, before `m_stopThread.store(true)`.

---

### CRIT-2 — Starvation recovery `alSourcePlay` must NOT be called under `m_streamMutex` if it can block

**File**: `streaming-architecture.md` (§ Starvation recovery)
**Severity**: CRITICAL

**Description**: The spec mandates that the starvation recovery check and `alSourcePlay()` call occur "in the same lock scope as `alSourceQueueBuffers`" (step 3 of the split-lock pattern, inside `m_streamMutex`). Meanwhile, the spec separately mandates that OGG decoding must occur outside `m_streamMutex` because it is "variable-duration CPU work". However, `alSourcePlay()` calls the AL driver, which on PulseAudio/ALSA can block for a non-trivial duration during buffer flushing. Calling `alSourcePlay()` while holding `m_streamMutex` violates the stated goal of not blocking the main thread on the mutex.

The spec in the starvation recovery section says: "The starvation-recovery check MUST be performed in the same lock scope as `alSourceQueueBuffers`" and then at the end of step 3 — but this creates a contradiction: `alSourceQueueBuffers` is an AL call that can also block on some backends. The issue is that the rationale for "same lock scope" is a TOCTOU race between flag setting and state check, but `alSourcePlay()` is a separate driver call that may block the main thread via the mutex.

**Proposed resolution**: Clarify that the "same lock scope" requirement is correct for the `AL_SOURCE_STATE` check and `m_intentionallyStopped` flag read, but that `alSourcePlay()` must be preceded by `alGetError()` to clear error state before the call. Add a note that the implementation must use a non-blocking `m_streamMutex` try-lock fallback path on the main thread to prevent main-thread stalls if the audio thread is slow. Alternatively, explicitly state that `alSourcePlay()` under `m_streamMutex` is acceptable because the lock is never held across the full decode cycle — only across the AL queue/state calls.

---

### CRIT-3 — `alcCheckError` void* parameter is technically UDR on C-strict compilers

**File**: `error-checking.md` (§ Phase 3 Stub Signature)
**Severity**: CRITICAL

**Description**: The spec permanently freezes `alcCheckError` with a `void*` parameter in the header. While the spec correctly states that `ALCdevice*` converts implicitly to `void*` in C++, the reverse conversion (`reinterpret_cast<ALCdevice*>(device)` in `al_check.cpp`) is technically implementation-defined, not guaranteed, for types that are not standard-layout. OpenAL Soft's `ALCdevice_struct` is an opaque internal struct; the round-trip through `void*` relies on a specific ABI assumption.

More critically, the spec acknowledges the stub is "frozen permanently" but the `al_check.cpp` implementation must `reinterpret_cast` the `void*` back to `ALCdevice*`. If the `IAlcFunctions` seam mock also passes mock device pointers through this `void*` round-trip, and the mock's `ALCdevice` type differs in alignment from OpenAL Soft's, the cast produces undefined behaviour in tests.

**The real gap**: the spec does not specify what happens in tests when `alcCheckError` is called with a mock device pointer. The `IAlcFunctions` seam does not wrap `alcCheckError` (error checking is separate from device/context management), meaning tests that exercise code paths calling `alcCheckError` would call the real `al_check.cpp` implementation with whatever pointer the mock system produced.

**Proposed resolution**: Add a note that `alcCheckError` should be a no-op (early return) when `m_deviceLost == true`, and document explicitly that in headless CI tests `alcCheckError` is not called because all ALC paths gate on `m_deviceLost`. If `alcCheckError` can be called in tests, the spec must provide a safe implementation for the null/mock device case.

---

### CRIT-4 — `m_gameOverFade` / `m_gameOverFadeT` are plain `bool`/`float` but written by main thread, read by audio thread

**File**: `audio-system.md` (§ private members section — game-over fade state)
**Severity**: CRITICAL

**Description**: The spec declares:
```cpp
bool  m_gameOverFade{false};   // set to true by setGameOverState()
float m_gameOverFadeT{0.0f};   // advanced by audio thread dt each wake
```

`setGameOverState()` is a main-thread call. `m_gameOverFade` is written on the main thread and read by the audio thread. `m_gameOverFadeT` is written by the audio thread and read by… the spec says "advanced by audio thread dt each wake, used to compute per-stem gain during fade" — so it appears to be audio-thread only. But `setGameOverState(false)` is described as "resets `m_gameOverFade` and `m_gameOverFadeT`" — so the main thread also writes `m_gameOverFadeT` on the false path, while the audio thread writes it each wake.

Neither `m_gameOverFade` nor `m_gameOverFadeT` are declared as `std::atomic`, creating a C++ data race (UB). The spec declares other cross-thread members as `std::atomic<float>` and `std::atomic<DuckState>` but misses these two. The comment "post-V1 code-path stubs" does not exempt them from the data race if they are declared and visible to both threads.

**Proposed resolution**: Declare `m_gameOverFade` as `std::atomic<bool>` and `m_gameOverFadeT` as `std::atomic<float>` in the spec. Alternatively, add a note that `m_gameOverFade` must be read under `m_streamMutex` on the audio thread and written under `m_streamMutex` on the main thread — but `std::atomic<bool>` is the simpler and more correct approach.

---

### CRIT-5 — `alGenSources` failure is not handled

**File**: `audio-occlusion.md` (§ alGenSources placement requirement), `audio-system.md` (§ constructor sequence Step 1.5)
**Severity**: CRITICAL

**Description**: The constructor sequence spec calls `alGenSources(kTotalSources, m_sources)` followed by `alCheckError("alGenSources")`. `alCheckError` throws `std::runtime_error` on failure. However, `alGenSources` can partially succeed: it fills as many entries of `m_sources` as it can before failing. If the throw path is taken after partial source generation (e.g., 47 of 62 sources allocated before failure), those 47 source handles are leaked — there is no cleanup spec for the partial-generation case.

The spec does not specify that on `alGenSources` failure the already-generated source handles must be freed with `alDeleteSources(partialCount, m_sources)` before throwing. On OpenAL Soft `alGenSources` is documented as atomic (all or nothing on some implementations) but this is not guaranteed across all backends.

**Proposed resolution**: Add a RAII guard or explicit cleanup step to `audio-system.md` constructor sequence: if `alCheckError` after `alGenSources` throws, the implementation must call `alDeleteSources(kTotalSources, m_sources)` before propagating the exception (using `std::fill_if` to delete only non-zero entries). Alternatively, specify that a successful `alGenSources` call on OpenAL Soft is always atomic and document the OpenAL Soft version contract.

---

### CRIT-6 — `cleanupFinishedSFX` and `acquireSFXSource` have an unspecced concurrent access path on `m_sfxSlots`

**File**: `source-pool.md` (§ SFX Pool Thread Safety)
**Severity**: CRITICAL

**Description**: The spec correctly identifies the race on `m_sfxSlots[i].soundId` and introduces `m_sfxVehicleReserved[]` as the fix. However, the spec does not address a second race: `acquireSFXSource()` (main thread) writes `m_sfxSlots[idleIdx]` (the non-atomic slot struct) while `cleanupFinishedSFX()` (audio thread) reads `m_sfxSlots[i]` for the `AL_SOURCE_STATE` query iteration. The vehicle-reserved fix only skips vehicle engine sources; for all other SFX slots, `cleanupFinishedSFX()` reads the slot's `soundId` field (or other fields) to perform its state check, while the main thread may be writing the same struct via `acquireSFXSource()`.

The spec says "replace any read of `m_sfxSlots[i].soundId` with `m_sfxVehicleReserved[i].load()`" but does not specify whether the `SFXSlot` struct is wholly replaced by this pattern or whether other fields (e.g., a `bool occupied` flag, a `SoundHandle`) still require atomic access. If `SFXSlot` has any field read by the audio thread and written by the main thread, that field needs to be `std::atomic` or protected by a mutex.

**Proposed resolution**: The spec must either (a) fully define the `SFXSlot` struct and specify which fields are accessed by which thread, with `std::atomic` for any cross-thread fields; or (b) specify that `cleanupFinishedSFX()` only uses `m_sfxVehicleReserved[i]` and `alGetSourcei(AL_SOURCE_STATE)` (both thread-safe paths) and does NOT read any field of `m_sfxSlots[i]` — making `m_sfxSlots` a purely main-thread data structure with the audio thread accessing only the atomic flag.

---

### CRIT-7 — No spec for device loss recovery during runtime (beyond silent-mode constructor path)

**File**: `audio-system.md`, `audio-thread-shutdown.md`, `streaming-architecture.md`
**Severity**: CRITICAL

**Description**: The spec handles `alcOpenDevice` returning null at construction by entering silent mode. However, there is no spec for runtime device loss — an OpenAL device can be disconnected (e.g., USB headset unplugged, PulseAudio daemon crash) at any point during gameplay. When this happens on OpenAL Soft, AL calls begin silently failing; eventually `alcGetError` returns a device-specific error code. The audio thread may enter an infinite retry loop, or worse, `ov_read` and `alBufferData` calls may succeed (returning data to a silently-failed AL) while the starvation recovery path fires repeatedly.

There is no spec for: detecting device loss during the audio thread wake cycle, setting `m_deviceLost` atomically on detection, and quiescing the audio thread cleanly after device loss. The `m_deviceLost` flag is described as a constructor-time guard only; no runtime detection path is specified.

**Proposed resolution**: Add a new spec section `## Runtime Device Loss Detection` to `audio-system.md` specifying: (1) after each `alBufferData` or `alSourceQueueBuffers` call in `updateStreams()`, check `alcGetError(m_device)` for `ALC_INVALID_DEVICE`; (2) on detection, set `m_deviceLost.store(true)`, log an error, and break out of the streaming loop; (3) from that point all IAudioSystem calls become no-ops via the `m_deviceLost` gate; (4) the audio thread exits cleanly without joining-deadlock.

---

## HIGH Issues

---

### HIGH-1 — `alCheckError` in `onSourceRecycled` is called from main thread but spec calls `alCheckError_real` (throws)

**File**: `audio-occlusion.md` (§ Pool slot recycle)
**Severity**: HIGH

**Description**: The `onSourceRecycled` code snippet calls `alCheckError("alFilterf(AL_LOWPASS_GAIN) in onSourceRecycled")` — which resolves to `alCheckError_real` per the error-checking spec, and throws `std::runtime_error` on failure. However, `alCheckError_real` is designed to be called on the audio thread (where throws can be caught by the streaming loop). If `onSourceRecycled` is called on the main thread and throws, the exception propagates up through `acquireSFXSource()` → caller code in the main game loop. The spec does not specify a catch handler at any call site of `acquireSFXSource()` or `releaseVehicleEnginePair()`.

This is particularly dangerous because it means an EFX driver error during slot recycle (e.g., filter object corrupted after partial EFX allocation failure) would terminate the game with an unhandled exception.

**Proposed resolution**: Specify that `onSourceRecycled` must use a `try/catch` around EFX calls and log errors rather than propagating throws, since it runs on the main thread in a context where exception propagation is not safe. Alternatively, change the EFX calls in `onSourceRecycled` to use `alGetError()` directly with explicit error logging rather than the throwing wrapper.

---

### HIGH-2 — HRTF initialization: `alcCheckError` called after `alcMakeContextCurrent` returns `ALC_FALSE` does not throw in the documented code path

**File**: `hrtf-initialization.md`
**Severity**: HIGH

**Description**: The code snippet shows:
```cpp
if (!alcMakeContextCurrent(context_)) {
    alcCheckError(device_, "alcMakeContextCurrent");
    throw std::runtime_error("AudioSystem: alcMakeContextCurrent failed");
}
```

The `alcCheckError` call here is redundant — if `alcMakeContextCurrent` returned `ALC_FALSE`, `alcCheckError` will call `alcGetError(device_)`. Per OpenAL Soft behaviour, `alcMakeContextCurrent` returning `ALC_FALSE` does not always set a device error — the error state may be `ALC_NO_ERROR` even when the function fails (e.g., when the context is already current on another thread). This means `alcCheckError` may NOT throw, and execution falls through to the explicit `throw std::runtime_error(...)`. This is correct behaviour, but the code pattern implies `alcCheckError` is expected to throw and the explicit throw is a fallback — which is the wrong mental model. If the programmer later removes "the redundant explicit throw", the path silently changes from always-throws to sometimes-does-not-throw.

Additionally, the `hrtf-initialization.md` code uses `context_` (trailing underscore naming) while `audio-system.md` uses `m_context` (m_ prefix). This is a naming inconsistency within the spec.

**Proposed resolution**: Remove the `alcCheckError` call from this specific path and keep only the explicit `throw`. Add a spec note explaining that `alcMakeContextCurrent` failure is explicitly thrown regardless of `alcGetError` state. Standardize naming to `m_context`/`m_device` throughout `hrtf-initialization.md` to match `audio-system.md`.

---

### HIGH-3 — `syncListenerToCamera` must check `m_deviceLost` before making AL calls

**File**: `spatial-audio.md` (§ Listener sync)
**Severity**: HIGH

**Description**: `syncListenerToCamera()` calls `alListener3f` and `alListenerfv` from the main thread. The spec documents thread safety extensively (process-wide context, permanent binding) but does not specify what happens when `m_deviceLost == true`. In silent mode (device absent or lost), the main thread has no current AL context, so these AL calls would fail silently or produce `AL_INVALID_OPERATION` errors. More critically, during destruction (after step 6: `alcMakeContextCurrent(nullptr)`), if `syncListenerToCamera()` is still being called by the game loop, it makes AL calls with no context.

The spec says `syncListenerToCamera` is called "once per frame from the main thread after Irrlicht updates the camera" but does not specify a guard condition.

**Proposed resolution**: Add to the `syncListenerToCamera()` spec: "Must check `m_deviceLost` at entry and return early without any AL calls when `m_deviceLost == true`." This is consistent with the pattern used for all other IAudioSystem methods in silent mode.

---

### HIGH-4 — `computeNextBarBoundary` uses `double` arithmetic but `spb` is cast from `double` to `uint64_t` with potential loss

**File**: `dynamic-soundscape.md` (§ Beat-boundary synchronization)
**Severity**: HIGH

**Description**: The spec shows:
```cpp
uint64_t spb = static_cast<uint64_t>((sr * 60.0 / bpm) * beatsPerBar);
```

At 90 BPM / 4 beats per bar / 44100 Hz: `(44100 * 60.0 / 90.0) * 4 = 44100 * 0.6667 * 4 = 117600.0`. This is exact. However, for other BPM values or fractional beat-per-bar counts, the `static_cast<uint64_t>` truncates fractional samples. Over 36 bars (96 s at 90 BPM), an error of even 0.5 samples per bar accumulates to 18 samples — negligible (~0.4 ms at 44100 Hz) and perceptually irrelevant. But the spec locks BPM to 90 and beatsPerBar to 4, so this is only a MEDIUM concern for V1.

More importantly, `computeNextBarBoundary` takes `uint32_t sr` but `m_samplesQueued` is `uint64_t`. In `computeSamplesPlayed`, `samplesQueued - queued` is a `uint64_t` subtraction — if `samplesQueued < queued` the underflow protection `? samplesQueued - queued : 0` is correct. However the guard only checks if `samplesQueued > queued`, which is strictly greater than. When they are equal (exactly `kNumBuffers` buffers queued, none played yet), `samplesPlayed = 0` — correct behaviour. This edge case is handled but only by accident of the `>` vs `>=` choice.

Additionally: the spec says `bpm` parameter is `float` in the function signature, but `sr` is `uint32_t`. The computation `sr * 60.0 / bpm * beatsPerBar` mixes unsigned integer and floating-point arithmetic — if `sr` overflows `uint32_t` during promotion to double, the result is wrong. At 44100 Hz this is not a problem, but the spec should document the type requirements.

**Proposed resolution**: Specify the exact types for all parameters of `computeNextBarBoundary`. Add a note that this formula is only valid for the V1 locked BPM/rate values and document the accumulated drift calculation showing it is negligible for V1 play sessions.

---

### HIGH-5 — Interrupted crossfade spec uses `t_offset = (2/π) × arccos(current_gain_out)` but this can produce `NaN` when `current_gain_out > 1.0`

**File**: `dynamic-soundscape.md` (§ Interrupted crossfade)
**Severity**: HIGH

**Description**: The interrupted crossfade formula `t_offset = (2/π) × arccos(current_gain_out)` is mathematically correct when `current_gain_out ∈ [0.0, 1.0]`. However, the spec does not specify clamping of `current_gain_out` before computing `arccos`. If any floating-point rounding produces a gain slightly above `1.0f` (which can happen with `sin(t × π/2)` at `t=1.0f` due to IEEE 754 precision), `arccos` returns `NaN`. `NaN` propagated into `m_musicCrossfadeT` causes all subsequent crossfade gain updates to produce `NaN` gains on sources, which OpenAL Soft handles as implementation-defined (often produces `AL_INVALID_VALUE` or ignores).

The spec also states "current_gain_out refers to stem B's current gain AT THE MOMENT OF INTERRUPTION — i.e., stem B's gain_in value from the interrupted crossfade (`sin(interrupted_t × π/2)`)". This is the incoming stem's gain becoming the outgoing gain. The `sin` function at `t = 1.0f` should return exactly `1.0f`, but floating-point `sin(π/2)` is not exactly `1.0f` on all implementations.

**Proposed resolution**: Specify `current_gain_out = std::clamp(current_gain_out, 0.0f, 1.0f)` before `arccos`. Add this clamp to the interrupted-crossfade spec pseudo-code. Also add clamping of `t_offset` to `[0.0f, 1.0f]` after the arccos computation.

---

### HIGH-6 — `updateVehicleEngines` on audio thread calls `alSourcef(AL_GAIN)` and `alSourcef(AL_PITCH)` but no `alCheckError` requirement is specified

**File**: `audio-system.md` (§ updateVehicleAudio method spec), `dynamic-soundscape.md` (§ Vehicle Engine Audio)
**Severity**: HIGH

**Description**: The spec for `updateVehicleAudio` says the audio thread reads the atomic fields and applies `AL_PITCH`, `AL_GAIN`, and `AL_POSITION` calls. The project rule is "All AL calls → `alCheckError()`". However, the per-vehicle update path runs on every audio thread wake for up to 24 sources (12 pairs × 2). The spec does not state whether `alCheckError` is called after each of the 3 AL calls per source, or whether a batch error check is acceptable.

At 100 wakes/s × 24 sources × 3 calls = 7,200 `alCheckError` calls per second on the audio thread. Each `alCheckError` calls `alGetError()` which is a driver round-trip. This performance cost is unspecced and may be significant on PulseAudio backends.

**Proposed resolution**: Add a spec section to `audio-system.md` clarifying that `alCheckError` is required after each `alSourcef`/`alSource3f` call on the audio thread in `updateVehicleEngines()`, with a note that the performance cost at 24 sources × 100 Hz must be benchmarked and that a batch error check (call `alGetError()` once after all 24 sources, not after each individual call) is an acceptable optimization — provided the error is attributed to the full batch rather than a specific call.

---

### HIGH-7 — No spec for WAV file loading: WAV decoder implementation, error handling, mono/stereo format selection

**File**: `audio-asset-formats.md`, `audio-system.md`, `v1-audio-asset-manifest.md`
**Severity**: HIGH

**Description**: The spec extensively covers OGG loading via libvorbisfile (`ov_fopen`, `ov_read`, `ov_pcm_seek`, `ov_clear`, `ov_info`) but has no corresponding spec for WAV loading. V1 has 16 WAV PCM SFX assets plus 2 stingers (18 WAV files total). The spec does not specify:
- Which WAV decoder library to use (libaudiofile? custom RIFF parser? dr_wav?).
- How to validate WAV format tags (must be PCM 0x0001, not ADPCM or IEEE float).
- How to handle multi-chunk WAV files (LIST chunks, INFO chunks before the data chunk).
- Error handling when `data` chunk is not found or truncated.
- How `alBufferData(AL_FORMAT_MONO16, ...)` is populated from the raw WAV bytes.

The `audio-asset-formats.md` spec says "Pre-loaded AL buffer" for Tier 1 WAV assets but does not specify the loading path, only that it is "pre-loaded". Without a WAV loading spec, implementers may use incompatible approaches.

**Proposed resolution**: Add a `## WAV Loading` section to `audio-system.md` or `audio-asset-formats.md` specifying: the decoder library (or minimal RIFF parser requirements), format validation checks, error handling (log + skip asset vs. throw), and how the decoded PCM is passed to `alBufferData`. Given the V1 WAV assets are all 44100 Hz mono 16-bit, a minimal RIFF parser is sufficient and should be specified.

---

### HIGH-8 — `m_occlusionGainTarget` initialization uses `memory_order_relaxed` but audio thread reads it on first wake without a synchronization point

**File**: `audio-system.md` (§ private members — `m_occlusionGainTarget`)
**Severity**: HIGH

**Description**: The spec specifies:
```cpp
// Required initialization (in AudioSystem constructor, before thread launch):
for (auto& t : m_occlusionGainTarget) t.store(1.0f, std::memory_order_relaxed);
m_audioThread = std::thread(&AudioSystem::audioThreadFunc, this);  // launch AFTER init
```

`std::thread` constructor is a happens-before barrier for all writes performed before it — so `memory_order_relaxed` stores before the `std::thread` constructor ARE visible to the thread. This is correct per the C++ memory model (§6.9.2.1: thread launch synchronizes-with the beginning of the new thread). The spec note is technically correct but misleadingly places the burden on the `std::thread` constructor without explaining WHY `relaxed` is safe here.

The deeper issue: the spec notes say `m_occlusionGainTarget` is "std::atomic<float>" but the `std::atomic<float>` array is declared as `std::atomic<float> m_occlusionGainTarget[kEvictableSFXCount]`. In C++11/14/17, `std::atomic` is not copy-constructible and arrays of atomics are not value-initialized through normal means — they are default-initialized (indeterminate state for floats). The `for` loop initializing them before thread launch is mandatory; if an implementer omits it (relying on the `{}` initializer on the member declaration to zero-initialize), the `{}` initializer on non-trivially-constructible members like `std::atomic<float>` initializes to 0.0f, not 1.0f. A 0.0f occlusion gain target means all 55 sources play at zero gain immediately. The spec says "MANDATORY: Initialize all elements to 1.0f" but the `{}` declaration initializes to 0.0f — this is a trap for implementers.

**Proposed resolution**: The member declaration `std::atomic<float> m_occlusionGainTarget[kEvictableSFXCount];` must NOT use `{}` in the spec (which would suggest zero-initialization to 0.0f). Remove the `{}` from the spec declaration and rely exclusively on the explicit `for` loop. Add a note: "The `{}` initializer initializes `std::atomic<float>` to 0.0f (its value-initialized state), NOT 1.0f — do NOT rely on member `{}` initialization for this array."

---

### HIGH-9 — `DUCKED` state checks only V1 stinger sources [55..56] but spec says "query AL_SOURCE_STATE for both V1 stinger sources" — no per-wake error check

**File**: `dynamic-soundscape.md` (§ DUCKED state)
**Severity**: HIGH

**Description**: The spec says: "Each audio thread wake, query `AL_SOURCE_STATE` for **both V1 stinger sources** (crisis at sources[55], milestone at sources[56])." There is no `alCheckError` requirement after these `alGetSourcei(AL_SOURCE_STATE, ...)` calls. Per the project error-checking rule ("All AL calls → `alCheckError()`"), these calls require error checking. At 100 Hz × 2 sources this is 200 checks/second in the DUCKED state, but they are mandatory.

More importantly, the DUCKED state check involves querying source state after the stinger has potentially been stopped and its buffer rebound (e.g., if `onSourceRecycled` ran on the stinger source — which the spec says should not happen because stingers are non-evictable, but the spec does not explicitly prohibit `cleanupFinishedSFX` from querying stinger source state and acting on AL_STOPPED stingers). `cleanupFinishedSFX` is documented to iterate `m_sfxSlots[0..kEvictableSFXCount-1]` — stingers are at indices 55/56, which are outside this range (range is 0..54 = kEvictableSFXCount-1 = 54). So stinger sources are correctly excluded from `cleanupFinishedSFX`. This is correct but only because `kEvictableSFXCount = 55` and the loop is `< kEvictableSFXCount`. A future misread could break this.

**Proposed resolution**: Add `alCheckError` calls after each `alGetSourcei(AL_SOURCE_STATE)` call in the DUCKED state wake loop. Add a comment in the `cleanupFinishedSFX` spec explicitly noting that the loop bound `< kEvictableSFXCount` ensures stinger sources (55/56) and stream sources (58..61) are never touched by the cleanup loop.

---

### HIGH-10 — `transitionToMainMenu` spec says "stops all active gameplay music stems and ambient beds on sources[58..61]" — no spec for in-flight crossfades

**File**: `audio-system.md` (§ IAudioSystem::transitionToMainMenu)
**Severity**: HIGH

**Description**: `transitionToMainMenu()` "stops all active gameplay music stems and ambient beds on sources[58..61]". However, the spec does not describe what happens when a crossfade is currently in progress (e.g., music is mid-crossfade between calm and growth stems when the player quits). The crossfade state includes `m_musicCrossfadeT` (atomic), the outgoing source (with ongoing gain curve), and the incoming source (with rising gain). Calling `alSourceStop` on both without properly resetting the crossfade state machine will leave `m_musicCrossfadeT` at a non-zero value, which means the next `transitionToGameplay()` call will attempt a mid-crossfade continuation using stale gain values.

The spec says "stops all sources[58..61] unconditionally via alSourceStop" but does not specify: (1) reset `m_musicCrossfadeT` to 0; (2) reset the crossfade command queue (pending crossfade commands must be discarded); (3) reset `m_ambientCrossfadeT` to 0; (4) set `m_intentionallyStopped = true` for all 4 streams before calling `alSourceStop`.

Without (4) especially, the starvation recovery path will fire on the next audio thread wake and attempt to restart the stopped streams — restarting gameplay audio that was supposed to be stopped.

**Proposed resolution**: Expand the `transitionToMainMenu()` spec to enumerate all state that must be reset: crossfade progress counters, crossfade command queue flush, `m_intentionallyStopped` flag for all 4 stream sources set under `m_streamMutex`, and `alSourceStop` calls for all 4 sources also under `m_streamMutex` to prevent starvation recovery race.

---

### HIGH-11 — `IAlcFunctions` seam is mentioned but not defined in audio architecture specs

**File**: `audio-system.md` (§ AudioSystem constructor signature, `alcFunctions: non-owning pointer to IAlcFunctions`)
**Severity**: HIGH

**Description**: The spec references `IAlcFunctions` as the "seam so audio tests can run without an AL device" and `DefaultAlcFunctions` as the production implementation. The CLAUDE.md project rules reference `src/audio/ialc_functions.h`. However, none of the audio architecture spec files define the `IAlcFunctions` interface — what methods it must expose, how `DefaultAlcFunctions` implements them, or how the mock bypasses the real ALC calls. The memory notes mention it exists at `src/audio/ialc_functions.h` but this is a source path, not a spec.

Without a spec for `IAlcFunctions`, test engineers cannot implement `MockAlcFunctions` correctly. The spec also does not state which specific ALC calls are intercepted by the seam (`alcOpenDevice`, `alcCreateContext`, `alcMakeContextCurrent`, `alcSetThreadContext` — or only the thread-local context function?).

**Proposed resolution**: Add a `## IAlcFunctions Interface` section to `audio-system.md` specifying the interface methods, their signatures, and the `DefaultAlcFunctions` wrapper. This should include which calls the seam intercepts and the rationale for why those specific calls are in the seam vs. hardcoded.

---

### HIGH-12 — No spec for `AudioSystem::loadSound()` method signature, buffer caching, or error handling on missing assets

**File**: `audio-system.md`, `v1-audio-asset-manifest.md`
**Severity**: HIGH

**Description**: The manifest spec mentions `AudioSystem::loadSound()` as the path used for stinger loading, and the SoundId table documents 25 SoundIds. However, no spec file defines the `loadSound()` method signature, buffer caching behavior (is it idempotent? can the same SoundId be loaded twice?), or error handling when the asset file is missing or corrupt.

The pre-load queue mechanism is described for Tier 2 OGG SFX (via `processPreloadCommand` on the audio thread), but Tier 1 WAV loading (which does NOT need to be on the audio thread per the spec — there is no process-wide context constraint because WAV loading only calls `alBufferData` once) is not specified at all in terms of threading, call sequence, or error policy.

**Proposed resolution**: Add a `## Asset Loading API` section to `audio-system.md` specifying `loadSound(SoundId id, const std::string& path, bool looping)` signature, idempotency contract, and error policy (missing file → log error + assign null buffer to SoundId → `playSound(id)` becomes a no-op).

---

## MEDIUM Issues

---

### MED-1 — Zone ambient loop EFX bypass not specified for `alSourcei(AL_DIRECT_FILTER, AL_FILTER_NULL)`

**File**: `spatial-audio.md`, `audio-occlusion.md`
**Severity**: MEDIUM

**Description**: `spatial-audio.md` specifies `alSourcei(src, AL_DIRECT_FILTER, AL_FILTER_NULL)` for UI / notification sounds to bypass EFX occlusion. However, `sfx_earthworks` (in `v1-audio-asset-manifest.md`) specifies "AL_DIRECT_FILTER: AL_FILTER_NULL — EFX bypass because construction occurs on open, unoccluded tiles". There is no spec for WHEN this bypass is set — at `acquireSFXSource()` time? Per `playPositionalSound()` call? The audio-occlusion.md spec pre-binds filters at pool construction, meaning every evictable SFX source already has an EFX filter attached. The bypass for `sfx_earthworks` requires explicitly unbinding the filter at play time, then what happens when the source is recycled — is the filter re-bound by `onSourceRecycled`?

The spec in `audio-occlusion.md` says `onSourceRecycled` calls `alSourcei(m_sources[i], AL_DIRECT_FILTER, m_occlusionFilter[i])` — this re-binds the filter. So the bypass is automatically cleared on recycle. But the spec does not document this as a deliberate design choice or specify that `sfx_earthworks` and similar "EFX bypass at tile center" sounds MUST be played and NOT reused (one-shot), relying on the recycle step to restore the filter.

**Proposed resolution**: Add a note to `audio-occlusion.md` and `spatial-audio.md` that per-call EFX bypass (setting `AL_DIRECT_FILTER` to `AL_FILTER_NULL` at play time) is a runtime override that `onSourceRecycled` will automatically undo. Document that this pattern is correct and expected for all non-occluded positional sources.

---

### MED-2 — `AL_SOFT_loop_points` extension loaded on audio thread but `alBufferiv` proc address lookup may return the wrong function type

**File**: `streaming-architecture.md` (§ Pre-loaded looping OGG SFX — loop point handling)
**Severity**: MEDIUM

**Description**: The spec uses:
```cpp
auto alBufferiv = reinterpret_cast<LPALBUFFERIV>(alGetProcAddress("alBufferiv"));
```

`LPALBUFFERIV` requires `<AL/alext.h>`. The spec does not specify how this type is declared without including `alext.h`. If `processPreloadCommand` is in `audio_system.cpp` (which includes `<AL/alc.h>` and presumably `<AL/al.h>`), then `<AL/alext.h>` can also be included there — but this is not stated. If `alext.h` is not included, the `LPALBUFFERIV` type is undefined, forcing a manual `using LPALBUFFERIV = void(*)(ALuint, ALenum, const ALint*)` — which must be specified in the spec.

**Proposed resolution**: Add to the `processPreloadCommand` context in `streaming-architecture.md`: "The `LPALBUFFERIV` type is available from `<AL/alext.h>`. Since `audio_system.cpp` may freely include OpenAL headers, include `<AL/alext.h>` in `audio_system.cpp` and use the standard typedef. Do NOT include `<AL/alext.h>` in any `.h` file."

---

### MED-3 — No spec for `AudioStream` struct/class layout and ownership

**File**: `streaming-architecture.md`
**Severity**: MEDIUM

**Description**: The streaming spec describes `AudioStream` as holding `OggVorbis_File` as a persistent member, `m_samplesQueued`, `m_nextBarBoundary`, `m_intentionallyStopped`, `isOpen`, `vf` (VorbisFile), and AL buffer handles. But no spec document defines the full `AudioStream` struct/class: its fields, constructors, ownership, and where it lives (`AudioSystem` member? separate file?). The spec says "AudioStream holds OggVorbis_File as a persistent member" but is `AudioStream` a struct in `AudioSystem`, a separate class, or a private inner class?

The 4 stream sources use sources[58..61] and the spec documents stream[0..1] = music stems, stream[2..3] = ambient beds. But whether `AudioSystem` has `AudioStream m_streams[4]` or `std::array<AudioStream, 4>` is not specified.

**Proposed resolution**: Add a `## AudioStream Data Structure` section to `streaming-architecture.md` defining the full field list, the array size (`kStreamSourceCount = 4`), and the relationship between stream index and source pool index (stream[i] uses source at pool index `kSFXPoolSize + i`).

---

### MED-4 — `sfx_vehicle_horn` simultaneous cap (max 3) enforcement is mentioned in the manifest but not in any implementation spec

**File**: `v1-audio-asset-manifest.md`, `source-pool.md`, `audio-system.md`
**Severity**: MEDIUM

**Description**: The manifest spec for `sfx_vehicle_horn` says: "global simultaneous cap: max 3 horn sources playing at any time across all vehicles". This is a runtime enforcement requirement. However, no implementation spec file (source-pool.md, audio-system.md) describes how this cap is tracked, which data structure counts active horn sources, or how `playSound(SFX_VEHICLE_HORN, HIGH)` determines whether the cap has been reached before acquiring a source.

The per-vehicle 2 s cooldown also requires a per-vehicle timestamp, but no data structure is specified to hold per-vehicle horn cooldowns.

**Proposed resolution**: Add a `## Vehicle Horn Rate Limiting` section to `source-pool.md` or `dynamic-soundscape.md` specifying: the `m_activeHornCount` atomic counter, `m_vehicleHornCooldown[vehicleId]` timestamp map, the check sequence in `playVehicleHorn()`, and what happens when the cap is reached (silent drop with no error).

---

### MED-5 — `m_duckTimer` units and reset semantics are underspecified across state transitions

**File**: `dynamic-soundscape.md` (§ DuckState machine)
**Severity**: MEDIUM

**Description**: The spec defines `m_duckTimer` as "seconds elapsed in current duck phase (audio thread only)". The DUCKING phase resets `m_duckTimer = 0` on stinger-during-DUCKING re-entry. The RELEASING phase says "where `m_duckTimer` is the elapsed time since the RELEASING state was entered (0 at entry, capped at 1.5 s)". However, the spec does not explicitly state that `m_duckTimer` is reset to 0 on DUCKED → RELEASING transition. The reader must infer this from the "0 at entry" phrase.

More importantly: on RELEASING → DUCKING re-entry (stinger fires while releasing), the spec says "set `m_duckStartGain = m_musicDuckGain` (capture current gain at interruption), reset `m_duckTimer = 0`, transition to DUCKING". This resets `m_duckTimer` to 0, correct. But the DUCKING ramp formula is `m_musicDuckGain = m_duckStartGain + (0.4f - m_duckStartGain) * (m_duckTimer / 0.2f)`. If `m_duckStartGain > 0.4f` (true when releasing from above 0.4) this produces the correct downward ramp. If `m_duckStartGain < 0.4f` (impossible — gain can't go below 0.4 in normal operation), the formula would ramp UP. The spec does not guard against this case, relying on the state machine invariant that RELEASING always starts at 0.4 and ramps up.

**Proposed resolution**: Add explicit state-transition reset tables showing which fields are reset on each transition: `m_duckTimer = 0` on IDLE→DUCKING, DUCKED→RELEASING, and RELEASING→DUCKING; `m_duckStartGain` captured on IDLE→DUCKING and RELEASING→DUCKING.

---

### MED-6 — No spec for how `setMusicTrack(MusicTrackId)` differs from `setMusicIntensity(MusicIntensity)`

**File**: `audio-system.md` (§ IAudioSystem interface)
**Severity**: MEDIUM

**Description**: The `IAudioSystem` interface exposes both `setMusicTrack(MusicTrackId id)` and `setMusicIntensity(MusicIntensity intensity)`. The intent of `setMusicTrack` is documented as "Begin streaming the specified music track (with beat-boundary crossfade from the current track)" — this implies it is a direct track selection API. `setMusicIntensity` selects by tier (CALM/GROWTH/CRISIS) and the audio system picks the variant.

However, the spec does not define what `setMusicTrack` does that `setMusicIntensity` does not: Can callers use `setMusicTrack` to force a specific variant (e.g., always `music_crisis_02` during testing)? Or is `setMusicTrack` only used for main menu music (`MusicTrackId::MainMenu` → picks main_menu_01 or 02)? The crossfade behaviour for a `setMusicTrack` call is undefined — does it use bar-boundary synchronization? Does it bypass the time-of-day forced-Calm override?

**Proposed resolution**: Add a clarifying paragraph to the `setMusicTrack()` method comment in `audio-system.md` explaining: (1) `setMusicTrack` is used ONLY for main menu music selection by `UIManager` at startup; (2) `setMusicIntensity` is the API for all gameplay music; (3) `setMusicTrack` bypasses bar-boundary synchronization (it is a direct track replacement); (4) `setMusicTrack` is NOT subject to the time-of-day forced-Calm override (main menu runs in a separate audio context from gameplay).

---

### MED-7 — `alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED)` call has no `alCheckError` requirement in HRTF spec

**File**: `hrtf-initialization.md`
**Severity**: MEDIUM

**Description**: The spec shows `alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED)` followed by `alCheckError("alDistanceModel")`. This is correct. However, there is no spec for what happens if this call fails (e.g., if the context was not made current before the call — which cannot happen per the constructor sequence, but the spec does not make this dependence explicit). The `hrtf-initialization.md` is a standalone document that was clearly written independently of `audio-system.md`'s constructor sequence. A reader implementing from only `hrtf-initialization.md` might not know to call `alcMakeContextCurrent` first.

**Proposed resolution**: Add a cross-reference in `hrtf-initialization.md`: "Step 1 (`alcMakeContextCurrent`) in the constructor sequence (`audio-system.md`) MUST succeed before any call in this section — `alDistanceModel` and all subsequent `al*` calls require a current context."

---

### MED-8 — Production briefs define `kZoneLoopMaxPreloadDurationSeconds = 18.0` but no spec file defines this constant in C++

**File**: `production-briefs/zone-loop-production-brief.md`
**Severity**: MEDIUM

**Description**: The zone-loop production brief mentions `kZoneLoopMaxPreloadDurationSeconds = 18.0` as a C++ constant that enforces the 18 s hard cap. This constant is not defined or referenced in any implementation spec file (`audio-asset-formats.md`, `source-pool.md`, `audio-system.md`). There is no spec for WHERE in the C++ code this constant lives, whether it belongs in `audio_types.h`, `source-pool.h`, or a new `audio_constants.h`, or how it is used at load time to validate zone loop durations.

**Proposed resolution**: Add `kZoneLoopMaxPreloadDurationSeconds` to the compile-time constants section of `source-pool.md` (§ Phase 3 Compile-Time Constants) alongside the existing pool layout constants. Specify that `AudioSystem::processPreloadCommand()` validates OGG duration against this constant before calling `alBufferData`.

---

### MED-9 — `testability-architecture.md` MockAudioSystem is described as having 19 methods but does not document the `updateVehicleAudio` signature

**File**: `architecture/testing/testability-architecture.md`
**Severity**: MEDIUM

**Description**: `testability-architecture.md` references `MockAudioSystem` with 19 `MOCK_METHOD` entries and says it lives at `tests/simulation/MockAudioSystem.h`. The `audio-system.md` spec documents the three Phase 11d vehicle methods and notes their `MOCK_METHOD` signatures in inline comments. However, `testability-architecture.md` does not reference these three new methods, leaving the test architecture spec out of sync with the IAudioSystem method count expansion. A test engineer reading `testability-architecture.md` alone would see no mention of the vehicle engine pair mock methods.

**Proposed resolution**: Add to `testability-architecture.md` a reference to the Phase 11d vehicle engine audio mock methods (`acquireVehicleEnginePair`, `releaseVehicleEnginePair`, `updateVehicleAudio`) and confirm the method count is 19 (matching `audio-system.md`'s comment "Phase history: Phase 7 (base 14 methods) → ... → Phase 11d (+3 = 18) → Phase 11m (+1 = 19)").

---

## DUPLICATE Content Issues

---

### DUP-1 — Shutdown sequence for streaming sources is documented in BOTH `audio-thread-shutdown.md` AND partially in `streaming-architecture.md`

**Files**: `audio-thread-shutdown.md` (§ Step 4), `streaming-architecture.md` (§ MANDATORY — `openStreamOGG` must flush AL buffer queue)
**Severity**: MEDIUM

`audio-thread-shutdown.md` specifies the authoritative shutdown loop for streaming sources (query `AL_BUFFERS_QUEUED`, call `alSourceUnqueueBuffers`). `streaming-architecture.md` shows a nearly identical pattern inside `openStreamOGG` for the re-open path. While the two uses are different (shutdown vs. stream restart), the code is so similar that an implementer might copy from one and miss a detail in the other. The shutdown spec additionally checks `alCheckError` after `alSourceStop` which is not shown in the `openStreamOGG` pattern.

**Proposed resolution**: Add a cross-reference note in `streaming-architecture.md` `openStreamOGG` section pointing to `audio-thread-shutdown.md` for the authoritative unqueue pattern. Note the difference: `openStreamOGG` does not call `alCheckError` after `alSourceStop` in the re-open path (the error would be stale from the playing state — not a failure).

---

### DUP-2 — Non-positional source setup (`AL_SOURCE_RELATIVE`, `AL_ROLLOFF_FACTOR = 0`) is duplicated across `source-pool.md`, `spatial-audio.md`, and `dynamic-soundscape.md`

**Files**: `source-pool.md` (§ Stinger source non-positional setup), `spatial-audio.md` (§ UI / notification sounds), `dynamic-soundscape.md` (§ Stinger source allocation)
**Severity**: LOW

Three separate spec sections describe the same non-positional source setup pattern:
```cpp
alSourcei(s, AL_SOURCE_RELATIVE, AL_TRUE);
alSource3f(s, AL_POSITION, 0.f, 0.f, 0.f);
alSourcef(s, AL_ROLLOFF_FACTOR, 0.f);
alSource3f(s, AL_VELOCITY, 0.f, 0.f, 0.f);
```
Each is slightly different (stinger version includes `AL_VELOCITY`; UI sound version does not mention `AL_VELOCITY`; `dynamic-soundscape.md` version only mentions `AL_SOURCE_RELATIVE = AL_TRUE`). The inconsistency creates ambiguity: should `AL_VELOCITY` be set for UI sources?

**Proposed resolution**: Define a canonical "non-positional source setup" procedure in `spatial-audio.md` (which already covers all distance model setup). Cross-reference this procedure from `source-pool.md` and `dynamic-soundscape.md` rather than re-specifying it.

---

### DUP-3 — `m_lastDuckWakeTime` initialization requirement is duplicated in both `dynamic-soundscape.md` and `streaming-architecture.md`

**Files**: `dynamic-soundscape.md` (§ `m_duckTimer` advancement), `streaming-architecture.md` (§ Audio thread init block — "IMPORTANT: initialize `m_lastDuckWakeTime` BEFORE notify_one()")
**Severity**: LOW

Both files contain near-identical explanation of why `m_lastDuckWakeTime` must be initialized before `notify_one()`. The text in `streaming-architecture.md` is slightly more condensed but the essential spec content is the same. This duplication means a change to the initialization requirement must be applied in two places.

**Proposed resolution**: Make `dynamic-soundscape.md` the single authoritative source for the duck state machine initialization sequence. In `streaming-architecture.md`, replace the duplicate with a single cross-reference sentence.

---

### DUP-4 — StingerType enum and coupling rationale is documented in both `source-pool.md` and `audio-system.md`

**Files**: `source-pool.md` (§ Stinger source reservation — structurally enforced), `audio-system.md` (§ IAudioSystem — `StingerType` enum comment block)
**Severity**: LOW

Both files contain the StingerType enum values (CRISIS=55, MILESTONE=56), the coupling rationale, and the post-V1 promotion sequence. The `audio-system.md` version is less detailed; `source-pool.md` is authoritative. But having two places creates drift risk — a post-V1 promotion that updates `source-pool.md` must also update `audio-system.md`.

**Proposed resolution**: Remove the StingerType enum detail from `audio-system.md`'s IAudioSystem comment and replace with "See source-pool.md for enum values and coupling rationale." Retain only the forward-declaration in `audio-system.md`.

---

### DUP-5 — EFX filter allocation loop and shutdown loop are duplicated between `audio-occlusion.md` and `audio-thread-shutdown.md`

**Files**: `audio-occlusion.md` (§ Full pool construction loop and shutdown cleanup), `audio-thread-shutdown.md` (§ Step 4b — Release EFX lowpass filter objects)
**Severity**: MEDIUM

`audio-thread-shutdown.md` contains the exact same filter deletion loop as `audio-occlusion.md`. The `audio-thread-shutdown.md` version is the "authoritative shutdown sequence" but the actual filter handling detail (loop bound `kEvictableSFXCount`, per-filter null check, reset to `AL_FILTER_NULL`) is in `audio-occlusion.md`. The two versions agree but any future change must be applied in two places.

**Proposed resolution**: In `audio-thread-shutdown.md` step 4b, present only the high-level contract ("delete EFX filters; loop bound kEvictableSFXCount; check != AL_FILTER_NULL") and add "See audio-occlusion.md for the authoritative code pattern." Remove the duplicate code block from `audio-thread-shutdown.md`.

---

## LOW / MINOR Issues

---

### LOW-1 — `hrtf-initialization.md` uses `context_`/`device_` variable names inconsistent with `audio-system.md` `m_context`/`m_device`

**File**: `hrtf-initialization.md`
**Severity**: LOW

All member variable names in the hrtf-initialization spec use trailing underscore (`context_`, `device_`) while `audio-system.md` uses `m_` prefix convention (`m_context`, `m_device`). This inconsistency will confuse implementers cross-referencing the two files.

**Proposed resolution**: Update `hrtf-initialization.md` to use `m_context`/`m_device` naming throughout.

---

### LOW-2 — Production briefs are partially approved but approval metadata format is inconsistent

**Files**: `production-briefs/music-production-brief.md` (HTML comment approval), others (no approval metadata)
**Severity**: LOW

`music-production-brief.md` uses an HTML comment for approval: `<!-- APPROVED: sound-artist-opensoftal 2026-02-25 -->`. Other production briefs (`vehicle-sfx-production-brief.md`, `ambient-bed-production-brief.md`, `stinger-production-brief.md`, `wav-sfx-production-brief.md`, `zone-loop-production-brief.md`) have no approval comment. The `dynamic-soundscape.md` has a signed-off section at the bottom using a different format. Inconsistent approval tracking creates ambiguity about which briefs are final and which are still provisional.

**Proposed resolution**: Standardize on a single approval format in all production brief documents (e.g., the HTML comment approach used in `music-production-brief.md`) and mark each brief as either `APPROVED` or `DRAFT`.

---

### LOW-3 — No spec for `MusicTrackId` persistence — if the game is saved during CRISIS music, what track resumes on load?

**File**: `audio-system.md`, `v1-audio-asset-manifest.md`
**Severity**: LOW

Save/load is a separate architecture domain, but `IAudioSystem` is called by `UIManager` after game load. There is no spec for whether `AudioSystem` state (current music track, ambient bed, duck state) is persisted or reset on load. Given that `transitionToGameplay()` is called on load, and the music system will restart from the initial state, the current music intensity on load depends on `CitySimulation::update()` being called before the first music intensity set — which may produce a brief silence or wrong-stem start.

**Proposed resolution**: Add to `audio-system.md` §transitionToGameplay: "Music intensity state at load is re-established by the first `setMusicIntensity()` call from `CitySimulation::update()` after load. `transitionToGameplay()` does NOT restore saved music intensity — it begins fresh with `MusicIntensity::CALM`. The first `CitySimulation::update()` call will set the correct intensity immediately."

---

### LOW-4 — `sfx_earthworks` is `AL_SOURCE_RELATIVE = AL_FALSE` (positional) but listed under "build/demolish feedback" with other non-positional SFX in the manifest table notes

**File**: `v1-audio-asset-manifest.md`
**Severity**: LOW

The manifest notes for `sfx_earthworks` explicitly clarify "This does NOT make the sound non-positional — `sfx_earthworks` remains a world-space positional source with `AL_SOURCE_RELATIVE = AL_FALSE`. Do NOT set `AL_SOURCE_RELATIVE = AL_TRUE`." The manifest for `sfx_build_place`, `sfx_build_demolish`, and `sfx_road_build` also specify positional at tile center. But `sfx_earthworks` additionally says "EFX bypass: AL_FILTER_NULL" — meaning it is positional but EFX-bypassed. This combination (positional + EFX bypass) is unusual and is only documented in the manifest, not in `spatial-audio.md` or `audio-occlusion.md`.

**Proposed resolution**: Add a note to `audio-occlusion.md` § Source Recycle Requirement: "Per-sound EFX bypass (setting `AL_DIRECT_FILTER = AL_FILTER_NULL` at play time) is correct and supported for specific assets that are positional but play in inherently unoccluded contexts (e.g., `sfx_earthworks` played at tile center in an open construction site)."

---

### LOW-5 — Vehicle engine OGG encoding quality is unspecified in `vehicle-sfx-production-brief.md` vs. `audio-asset-formats.md` and `v1-audio-asset-manifest.md`

**File**: `production-briefs/vehicle-sfx-production-brief.md`
**Severity**: LOW

`vehicle-sfx-production-brief.md` specifies "encode at **libvorbis -q 6** (minimum) for vehicle engine loops" but `audio-asset-formats.md` does not have an explicit entry for vehicle engine OGG quality (only music stems -q 8, ambient beds -q 7, zone loops -q 6). The `v1-audio-asset-manifest.md` OGG Vorbis Encoding Quality table also does not include vehicle engine loops. A reader of `v1-audio-asset-manifest.md` alone would not know the engine loop encoding quality.

**Proposed resolution**: Add a "Vehicle engine loops" row to the OGG Vorbis Encoding Quality table in `v1-audio-asset-manifest.md`: `-q 6` (same as zone loops — mono tonal content).

---

### LOW-6 — `dynamic-soundscape.md` Phase 1 sign-off section is misplaced

**File**: `dynamic-soundscape.md` (end of file)
**Severity**: LOW

The file ends with:
```
## Phase 1 sound-artist-opensoftal sign-off
**Date**: 2026-02-21
**Role**: sound-artist-opensoftal
```

This appears to be an incomplete section (no sign-off text or status). Having a sound artist sign-off section in a developer specification file is inconsistent with the file's purpose (implementation spec). This section should be in a separate review artifact or the production brief.

**Proposed resolution**: Remove the Phase 1 sign-off section from `dynamic-soundscape.md`. Sign-off should be tracked in production briefs or a separate review document. If the sign-off needs to be in the spec, add a complete entry (who reviewed what and whether approved or conditionally approved).

---

## MISSING Technical Specifications

---

### MISSING-1 — No spec for audio thread panic handling / AL error escalation from audio thread

**Severity**: HIGH

The spec describes `alCheckError_real` as throwing `std::runtime_error`. If this throw occurs on the audio thread (inside `updateStreams`, `updateOcclusion`, or `updateVehicleEngines`), it will propagate up through the audio thread's top-level function (`audioThreadFunc`). An unhandled exception on a `std::thread` calls `std::terminate()` — crashing the entire process. No spec document describes what the audio thread should do on a fatal AL error:
- Should it catch all exceptions, log the error, set `m_deviceLost = true`, and exit cleanly?
- Should it signal `m_initCV` with an error to notify the main thread?
- Is `std::terminate()` acceptable for audio thread panics?

**Proposed resolution**: Add a `## Audio Thread Error Handling` section to `streaming-architecture.md` specifying that the audio thread's top-level loop MUST wrap all work in a `try/catch(std::exception&)` block, log the error, set `m_deviceLost.store(true, memory_order_release)`, call `alGetError()` to clear the error state, and continue the loop (degraded mode) OR exit if `m_deviceLost` was already set (unrecoverable).

---

### MISSING-2 — No spec for `AudioSystem::update()` main thread responsibilities beyond "advance occlusion raycast budget"

**Severity**: MEDIUM

The `IAudioSystem::update(float realDeltaSeconds)` method is described as: "advance occlusion raycast budget, push time-of-day transitions, and forward any pending crossfade or zone-layer source updates." But the spec does not define what "advance occlusion raycast budget" means in terms of implementation — how many raycasts are dispatched per `update()` call, how the round-robin source selection works, and what data is written to `m_occlusionGainTarget` after a raycast hit/miss. The `audio-occlusion.md` file describes the result (gain target values, smoothing) but not the main-thread raycast scheduling algorithm.

**Proposed resolution**: Add a `## Main Thread Raycast Budget` section to `audio-occlusion.md` specifying: per-frame budget (max 8 raycasts), source iteration order (round-robin over occupied evictable SFX slots within 100 m), how sources beyond 100 m are handled (target set to 1.0f without raycasting), and the per-source cooldown (1 raycast per 6 frames tracked via a `uint32_t m_occlusionFrameCounter[kEvictableSFXCount]` array).

---

### MISSING-3 — No spec for `AudioStream` object reuse across `transitionToGameplay` / `transitionToMainMenu` calls

**Severity**: MEDIUM

`transitionToMainMenu()` and `transitionToGameplay()` both operate on `sources[58..61]` (the stream partition). There is no spec for the state of the `AudioStream` objects after these transitions. Specifically: when `transitionToMainMenu()` stops all 4 streams, are the `AudioStream` objects reset to a fresh state (closing `OggVorbis_File`, resetting `m_samplesQueued`, etc.) or left in a stopped state? When `transitionToGameplay()` is called next, does it call `openStreamOGG` on already-open slots (triggering the re-open path) or on fresh slots?

**Proposed resolution**: Add to `streaming-architecture.md` a section on stream slot reuse across transition calls, specifying that `transitionToMainMenu()` must call `openStreamOGG(slot, nullptr)` or equivalent to close the OGG file handle and mark slots as `isOpen = false`, preventing the re-open path from being triggered on `transitionToGameplay()`.

---

End of review.
