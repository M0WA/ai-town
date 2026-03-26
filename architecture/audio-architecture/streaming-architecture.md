# Streaming Architecture (music / long ambient)

> **NOTE: Throughout this spec, "samples" means PCM sample frames (one time point across all channels), NOT interleaved scalar values.** A stereo frame at 44100 Hz is one point in time containing two interleaved 16-bit values (L + R). A mono frame is one 16-bit value. When this spec says `kSamplesPerBuffer` frames, it means `kSamplesPerBuffer` time-points — NOT `kSamplesPerBuffer` interleaved scalars.

## kSamplesPerBuffer — stereo and mono clarification

`kSamplesPerBuffer` counts **PCM frames** (one time point per channel group), not interleaved scalar values:

- **Stereo streams (music stems, ambient beds)**: `kSamplesPerBuffer` PCM frames = `kSamplesPerBuffer × 2` interleaved 16-bit values in the raw buffer. The `alBufferData` byte count is `kSamplesPerBuffer × 2 × sizeof(ALshort)`.
- **Mono streams (future non-zone assets, if any)**: `kSamplesPerBuffer` PCM frames = `kSamplesPerBuffer` interleaved 16-bit values in the raw buffer. The `alBufferData` byte count is `kSamplesPerBuffer × 1 × sizeof(ALshort)`. Note: zone ambient loops are NOT in this category — they are pre-loaded SFX pool assets, not streaming sources. See note below.
- **General formula**: `byteCount = kSamplesPerBuffer × channels × sizeof(ALshort)`.

**Zone ambient loops are mono positional (1 channel) and are NOT streamed.** Per the V1 Audio Asset Manifest and audio-asset-formats.md, zone loops (`zone_residential`, `zone_commercial`, `zone_industrial`) are 12–18 s OGG files classified in the "Looping game SFX" tier — they are **pre-loaded** (fully decoded at load time into a single AL buffer) and played from the SFX pool, not the stream partition. `kSamplesPerBuffer` applies only to the **four streaming sources** (music stems: sources[58..59]; ambient beds: sources[60..61]). The byte count formula with `channels = 2` applies only to these four streaming sources, which are all authored stereo. Zone loop SFX pool sources are separate from the stream partition and have no `kSamplesPerBuffer` relationship.

Using the interleaved scalar count instead of the frame count in `alBufferData` (i.e., passing `kSamplesPerBuffer * 2` as the size parameter for stereo) would upload twice as many bytes as allocated, causing a buffer overrun. Using frame count correctly (passing `kSamplesPerBuffer * channels * sizeof(ALshort)`) is mandatory.

---

- Dedicated **audio thread** (`std::thread` + `std::mutex` + `std::condition_variable`) wakes every **10 ms** independent of frame rate (not 20 ms — see proactive starvation prevention note below)
- **CRITICAL — audio thread must set context current at startup**: The audio thread entry function must call `m_fnSetThreadCtx(m_context)` as its **first action** before any AL calls. The constructor sequence for loading this function pointer and handling its absence is defined canonically in `audio-system.md` — this spec does not duplicate it. See `audio-system.md`, section "ALC_EXT_thread_local_context Requirement", for the full constructor sequence.

  **Summary of the canonical protocol (defined in `audio-system.md`)**: `alcGetProcAddress(m_device, "alcSetThreadContext")` is the sole check — `alcIsExtensionPresent` is NOT called for this extension. If `alcGetProcAddress` returns null, the `AudioSystem` constructor throws `std::runtime_error` synchronously before launching the audio thread. `m_useThreadLocalCtx` is set to `true` only after the proc address is confirmed non-null, immediately before `m_audioThread` is launched. The audio thread therefore never runs without a valid `m_fnSetThreadCtx` — there is no in-thread missing-extension detection branch, no `m_initError = true` path for a missing extension, and no `else` branch that signals init failure for an absent extension. The only `m_initError` paths in the audio thread are for `m_fnSetThreadCtx(m_context) == ALC_FALSE` (context bind failure) and other runtime errors encountered after the thread has started.

  The audio thread init block is:

  ```cpp
  // At the top of audio thread, BEFORE any AL call:
  // m_fnSetThreadCtx is guaranteed non-null here (constructor threw if it was null).
  if (m_fnSetThreadCtx(m_context) == ALC_FALSE) {
      { std::lock_guard<std::mutex> lk(m_initMutex); m_initError = true; m_initDone = true; }
      m_initCV.notify_one();
      return;
  }
  // IMPORTANT: initialize m_lastDuckWakeTime BEFORE notify_one().
  // notify_one() unblocks the constructor immediately; if the main thread then calls
  // triggerStinger() before m_lastDuckWakeTime is written, the audio thread computes
  // an epoch-sized dt on its first wake (data race / UB).  Initialize first, signal after.
  m_lastDuckWakeTime = m_clock->nowSeconds();

  // PRE-LOAD PHASE — process the pre-load command queue BEFORE signaling init complete
  // and BEFORE entering the streaming loop.
  //
  // Purpose: all pre-loaded OGG SFX buffer operations (alBufferData, alBufferiv,
  // alGetProcAddress for AL_SOFT_loop_points) must execute on the audio thread while
  // it has a confirmed current thread-local context (established by the
  // alcSetThreadContext call above). They must NOT be performed on the main thread
  // after the audio thread has claimed the context — alGetProcAddress on a thread
  // without a current context may return null on some OpenAL implementations.
  //
  // Enqueue protocol (main thread, inside AudioSystem constructor, BEFORE thread launch):
  //   m_preloadQueue.push(PreloadCommand{ soundId, filePath, looping });
  //   // ... repeat for all pre-load tier assets ...
  //   // No lock is needed because the thread has not started yet; the std::thread
  //   // constructor acts as a happens-before barrier for all writes performed before it.
  //   m_audioThread = std::thread(&AudioSystem::audioThreadFunc, this);
  //
  // Drain protocol (audio thread, after alcSetThreadContext succeeds, before notify_one):
  {
      PreloadCommand cmd;
      while (m_preloadQueue.tryPop(cmd)) {
          // Decode OGG into AL buffer, set AL_SOFT_loop_points if available,
          // store (cmd.soundId → bufferId) in m_preloadedBuffers map.
          // All AL calls here are safe: thread-local context is current.
          processPreloadCommand(cmd);
      }
  }
  // m_preloadQueue is empty at this point. The streaming loop never drains it again.
  // All subsequent pre-load requests after construction are PROHIBITED in V1 — the
  // pre-load queue is a construction-time mechanism only. If future requirements
  // demand runtime asset loading, a separate runtime-load command queue must be added
  // to the streaming loop (post-V1 scope).

  { std::lock_guard<std::mutex> lk(m_initMutex); m_initDone = true; }
  m_initCV.notify_one();
  // Now enter streaming loop...
  ```

  **Pre-load queue type — choose exactly one of the two patterns below.** The drain loop shown in the code block above calls `m_preloadQueue.tryPop(cmd)`, which is a method on an SPSC queue. A plain `std::vector` has no `tryPop()` member; mixing `std::vector` storage with the SPSC drain loop would be a compile error. Pick one pattern and use it consistently throughout the implementation:

  **Pattern A (SPSC queue — matches the drain loop shown above)**:
  `m_preloadQueue` is a non-blocking single-producer / single-consumer queue that exposes a `tryPop(cmd)` method (e.g., a lock-free ring buffer or any queue adapter implementing this interface). The drain loop already shown is correct for this type:

  ```cpp
  PreloadCommand cmd;
  while (m_preloadQueue.tryPop(cmd)) {
      processPreloadCommand(cmd);
  }
  ```

  Any SPSC queue implementation that provides `tryPop(T&) -> bool` is acceptable.

  **Pattern B (std::vector — alternative)**:
  `m_preloadQueue` is a `std::vector<PreloadCommand>` populated by the constructor before thread launch. Replace the `tryPop` drain loop with a range-for iteration:

  ```cpp
  for (auto& cmd : m_preloadQueue) {
      processPreloadCommand(cmd);
  }
  ```

  This is valid because `m_preloadQueue` is written entirely on the main thread before `m_audioThread = std::thread(...)` is called (no concurrent access), so no lock is needed and the full vector is readable in one pass.

  **IMPLEMENTER NOTE — `push()` vs `push_back()` (Pattern B only)**: The constructor sequence in `audio-system.md` Step 1.6 uses `m_preloadQueue.push(PreloadCommand{...})`. `std::vector` has no `push()` method; that call is written for the SPSC queue type used by Pattern A. When Pattern B is chosen, the implementer MUST replace every `m_preloadQueue.push(...)` call in the Step 1.6 loop with `m_preloadQueue.push_back(...)`. This substitution is an implementer responsibility when selecting Pattern B — `audio-system.md` Step 1.6 is not modified because it remains correct as written for Pattern A.

  **Exactly one pattern must be chosen; do not mix SPSC `tryPop` with `std::vector` iteration.** No mutex is needed in either case because the main thread has finished writing before `m_audioThread = std::thread(...)` is called, and the audio thread is the only reader.

  **Cross-reference**: The pre-load OGG threading requirement (audio thread only, loop point handling, `AL_SOFT_loop_points`) is specified fully in the "Pre-loaded looping OGG SFX — loop point handling" section of this document. The constructor sequence that enqueues pre-load commands before thread launch is described in `audio-system.md` (Section: ALC_EXT_thread_local_context Requirement, constructor sequence Steps 1–5).

  **If `ALC_EXT_thread_local_context` is absent, the constructor throws before the thread is ever launched** — the audio thread never observes this condition. Do not fall back to relying on `alcMakeContextCurrent` on the main thread — that approach is undefined behavior for multi-threaded OpenAL and is prohibited. OpenAL Soft on any reasonably modern system (2013 or later) ships with `ALC_EXT_thread_local_context`; a null proc address indicates a severely outdated OpenAL installation.
- **8 buffers × 64 KB each** (~3 s total buffered audio at 44100 Hz stereo: 8 × 16384 frames / 44100 Hz ≈ 2.97 s) — main-thread update would risk starvation during large city loads
- `AudioStream` holds `OggVorbis_File` as a **persistent member** across updates (opened via `ov_fopen` or `ov_open_callbacks`; closed via `ov_clear` in destructor; not re-opened each call)
- **Audio thread loop structure** — check `m_stopThread` FIRST after waking, before any AL call: this ensures once `join()` returns, no AL calls are in-flight, making the main-thread shutdown sequence (stop/unqueue/delete) safe from race conditions:

  ```cpp
  while (true) {
      { std::unique_lock<std::mutex> lock(m_streamMutex);
        m_streamCV.wait_for(lock, std::chrono::milliseconds(10),
            [this]{ return m_stopThread.load(); }); }
      if (m_stopThread.load()) break;  // check BEFORE any AL call
      updateStreams();
  }
  ```

- **OGG decode must occur outside `m_streamMutex`**: OGG decoding (calling `ov_read`) is variable-duration CPU work and must occur **outside** `m_streamMutex`. The main thread also acquires `m_streamMutex` for `alSourceStop()` calls; holding the mutex across a full decode cycle risks frame stalls on the main thread. The correct pattern within `updateStreams()`:
  1. Acquire `m_streamMutex` briefly to call `alGetSourcei(src, AL_BUFFERS_PROCESSED, &count)` and `alSourceUnqueueBuffers(...)`. Release the mutex.
  2. Decode PCM into a **local thread-local buffer** (outside the mutex).
  3. Acquire `m_streamMutex` again to call `alBufferData(...)` and `alSourceQueueBuffers(...)`. Release the mutex.
  Only the actual AL calls (`alBufferData`, `alSourceQueueBuffers`, `alGetSourcei(AL_BUFFERS_PROCESSED)`) require holding the mutex. This serializes all AL calls on shared streaming source handles between main thread and audio thread without blocking the main thread during decode.
- **MANDATORY — `alGetError()` before `alBufferData` in step 3**: Before calling `alBufferData` in step 3 of the split-lock pattern, call `alGetError()` to clear any stale AL error state left by earlier calls in the same wake cycle (e.g. from step 1). `alBufferData` returns `AL_INVALID_OPERATION` if the target buffer is still attached to a source (i.e. it was processed by OpenAL but not yet unqueued by `alSourceUnqueueBuffers`). If this error is not cleared before the next `alCheckError` call in `updateStreams` (typically on `alSourcef(AL_GAIN)` in the gain loop), the stale error is attributed to the wrong callsite — producing a misleading "Invalid Operation" on `alSourcef(AL_GAIN)` that terminates the audio thread. The mandatory pattern is:

  ```cpp
  // Step 3 — inside m_streamMutex:
  alGetError();  // clear any stale error from earlier AL calls this wake
  alBufferData(bufHandle, format, data, byteCount, sampleRate);
  {
      ALenum err = alGetError();
      if (err != AL_NO_ERROR) {
          // Buffer still attached — skip; m_samplesQueued not incremented;
          // same bufIdx will be retried next wake after the buffer is unqueued.
          logError("refillStream: alBufferData failed — buffer still attached");
          break;
      }
  }
  alSourceQueueBuffers(src, 1, &bufHandle);
  alGetError();  // consume — queue failure is non-fatal; starvation recovery handles restart
  ```

  Do NOT call `alCheckError_real` (which throws) around `alBufferData` — a buffer-still-attached failure is a recoverable transient condition, not a fatal error. The audio thread must not terminate on this condition.
- **MANDATORY — `openStreamOGG` must flush AL buffer queue when reopening an active slot**: `openStreamOGG` may be called on a slot that is already open (e.g. when a second TOD crossfade begins while the first is still in progress). When closing the existing stream, the implementation MUST call `alSourceStop` and `alSourceUnqueueBuffers` on the slot's source before resetting `m_samplesQueued = 0`. If the AL buffers are not flushed but `m_samplesQueued` is reset to 0, `refillStream`'s round-robin index (`m_samplesQueued % kNumBuffers`) starts at 0 but the ACTUAL free buffers are the oldest ones in the queue (in FIFO order). The index is immediately out of sync: buffer at index 1 (the next round-robin slot) is still attached when `refillStream` tries to write new data into it, producing persistent `AL_INVALID_OPERATION` errors from `alBufferData` and stalling the stream permanently. Required pattern inside `openStreamOGG` for the re-open path:

  ```cpp
  if (s.isOpen && s.vf) {
      AudioStreamUtils::closeOGG(s.vf);
      s.isOpen = false;
      // Flush AL state: stop source and unqueue all buffers before resetting counter.
      alSourceStop(src);
      ALint queued = 0;
      alGetSourcei(src, AL_BUFFERS_QUEUED, &queued);
      if (queued > 0) {
          std::vector<ALuint> tmp(queued);
          alSourceUnqueueBuffers(src, queued, tmp.data());
      }
      s.m_samplesQueued = 0;
  }
  ```

- **MANDATORY — `openStreamOGG` must pre-fill all AL buffer slots before setting `isOpen = true`**: After the OGG file is opened, sidecar loaded (music stems only), and the re-open flush performed (if slot was already active), `openStreamOGG` must decode and queue all `kNumBuffers` AL buffer slots synchronously before setting `s.isOpen = true`. This pre-fill loop calls `alBufferData` + `alSourceQueueBuffers` for each slot and increments `s.m_samplesQueued` by the actual decoded frame count. After `openStreamOGG` returns, the caller calls `alSourcePlay()` on a source that already has `kNumBuffers` buffers queued — the source plays immediately with no gap. Without pre-fill: on the first audio thread wake after `alSourcePlay()`, the source is already `AL_STOPPED` (no data was ever queued), starvation recovery fires for all 4 streams simultaneously, and 4 rapid `alSourcePlay()` calls can overwhelm the PulseAudio backend (ALSOFT "Broken pipe"). **Thread safety**: the audio thread skips slots where `s.isOpen == false`; pre-fill runs before `s.isOpen = true` is set, so no `m_streamMutex` is needed during the pre-fill loop. **EOF handling during pre-fill**: if `ov_read()` returns 0 (EOF) before all 8 buffers are filled (only possible for very short ambient files), call `ov_pcm_seek(vf, 0)` and retry the decode for that slot — if the retry also returns 0 frames, break and leave the remaining slots empty (the audio thread will refill them on wakes).
- On every audio thread wake: dequeue processed buffers, re-fill and re-queue
- **Proactive starvation prevention**: On every audio thread wake, check `AL_BUFFERS_PROCESSED`; if processed count == total buffer count (all 8 buffers consumed, none queued), immediately re-fill and re-queue all buffers before the source stops. Music stream thread interval: **10 ms** (not 20 ms) to minimize gap risk.
- **Starvation recovery** (fallback): If source has entered `AL_STOPPED` AND `stream.m_intentionallyStopped == false`, refill buffers then call `alSourcePlay()`; log a warning. **After calling `alSourcePlay()` in starvation recovery, reset only `stream.m_nextBarBoundary = 0`** — do NOT reset `m_samplesQueued`. Rationale: `m_samplesQueued` is used as the round-robin buffer index via `(m_samplesQueued / kSamplesPerBuffer) % kNumBuffers`. Resetting it mid-loop causes the next iteration to compute an index that collides with the buffer just queued in the first iteration of the same refill loop, making `alBufferData` fail (buffer is still attached to source). Only one buffer ends up queued (~371 ms of audio), the source starvates again on the next wake, and starvation recovery fires every 10 ms — flooding the PulseAudio backend until SIGKILL. Resetting `m_nextBarBoundary = 0` is sufficient: the bar-boundary will be recomputed on the next wake using the current `m_samplesQueued` value as the base, which is correct because the source continues playing from its current queue position after `alSourcePlay()`. The starvation-recovery check (`AL_STOPPED && !m_intentionallyStopped`) **must be performed in the same lock scope as `alSourceQueueBuffers`** (step 3 of the split-lock pattern). After re-queuing while still holding `m_streamMutex`, check `alGetSourcei(AL_SOURCE_STATE)` — if `AL_STOPPED && !m_intentionallyStopped`, call `alSourcePlay()`. This atomicity prevents the race where: (1) main thread acquires mutex, sets flag + calls stop, releases mutex; (2) audio thread acquires mutex in step 3, re-queues, checks state (sees AL_STOPPED), observes flag correctly, and decides whether to restart — all in one critical section. Without this, a check between step 1 (unqueue mutex release) and step 3 (queue mutex acquire) could observe a partially-updated state. **Do not restart a source that was intentionally stopped**: add a `bool m_intentionallyStopped` flag to each `AudioStream` (protected by `m_streamMutex`). The main thread must set `m_intentionallyStopped = true` **and** call `alSourceStop()` within the **same `m_streamMutex` lock scope** — these two operations must not be split across separate lock acquisitions:

  ```cpp
  // Correct: both in the same lock scope
  {
      std::lock_guard<std::mutex> lk(m_streamMutex);
      stream.m_intentionallyStopped = true;
      alSourceStop(stream.sourceHandle);
  }
  ```

  **Why same lock scope is required**: If `m_intentionallyStopped = true` is set in one lock scope and `alSourceStop()` is called in a separate lock scope, the audio thread can wake between the two operations, observe that the source is still `AL_PLAYING` with `m_intentionallyStopped = true`, and the subsequent `alSourceStop()` on the main thread arrives after the audio thread has already run a starvation check — producing a race where the source may or may not be restarted depending on scheduling. Atomicity of flag + stop within one lock scope eliminates this window. The audio thread's starvation recovery acquires `m_streamMutex` before checking `m_intentionallyStopped`, ensuring it sees a consistent snapshot of both the flag and the source state.
- **Ambient bed streaming loop handling**: Ambient beds use a **200 ms pre-baked DAW crossfade** at the loop boundary (see V1 Audio Asset Manifest). The streaming buffer size is ~371 ms per buffer (64 KB / 4 bytes per stereo frame / 44100 Hz ≈ 371 ms). Because 200 ms < 371 ms, the 200 ms crossfade region at the end of the file will be contained within the last one or two streaming buffers — it will NOT align with buffer boundaries. The streaming implementation MUST handle ambient bed loops using one of the following approaches (in order of preference):
  **libvorbisfile EOF detection**: `ov_read()` returns `0` bytes when the end of the OGG stream is reached (as distinct from a negative error code). The decode loop must check for a `0` return from `ov_read()` to detect EOF and trigger the seek-to-0. Do NOT treat a `0` return as an error — it is the normal EOF signal. Negative return values from `ov_read()` indicate decoding errors and must be handled separately.
  1. **Runtime stream seek (preferred)**: When the OGG decoder reaches EOF for an ambient bed (i.e., `ov_read()` returns `0`), immediately call `ov_pcm_seek(vf, 0)` and continue decoding into the same streaming buffer. This creates a seamless loop without the 200 ms pre-baked crossfade (the DAW crossfade tail at the end of the file is NOT decoded — the stream loops back before reaching it). For this to work without a click, ambient bed assets authored specifically for this approach must have a seamless boundary at sample 0 (no silence, no audible transient at the loop point). Authors should be notified of this requirement. **If `AL_SOFT_loop_points` is available**: set the loop start to sample 0 and the loop end to the sample index where the pre-baked crossfade begins (i.e., `totalSamples - crossfadeSamples`). This allows the extension to handle the seek transparently.
  2. **Continuous stream fallback**: Treat the ambient bed as an infinite OGG stream — when `ov_read()` returns `0` (EOF), call `ov_pcm_seek(vf, 0)` and continue. The 200 ms pre-baked crossfade tail at EOF is naturally skipped when the seek-to-0 occurs before the tail is decoded. This is acceptable behavior: the loop point is not crossfaded, but a well-authored seamless loop boundary at sample 0 is perceptually equivalent.
  **Do NOT attempt to queue the pre-baked crossfade region as a separate buffer** — the 200 ms crossfade tail is smaller than one buffer period, making it impossible to queue it as a discrete AL buffer without starvation risk. The runtime loop-point seek approach is always correct; the pre-baked crossfade region in the file is a production fallback for non-streamed playback modes only.

- **Music stem streaming loop handling**: Gameplay music stems (sources[58..59]) use the **same EOF→seek loop mechanism as ambient beds**. When `ov_read()` returns `0` at EOF for any music stem, call `ov_pcm_seek(vf, 0)` and continue decoding into the same streaming buffer. This applies to all music tracks regardless of `MusicTrackId`, including main menu music (`MusicTrackId::MainMenu`, track IDs 1 and 2). A `0` return from `ov_read()` is the normal EOF signal — do NOT treat it as an error. Negative return values indicate decoding errors and must be handled separately.

  Key invariants shared with ambient bed looping:
  - The seek-to-0 is performed **inside the current decode cycle**, not on the next audio thread wake — the streaming buffer is filled continuously across the loop boundary.
  - `m_samplesQueued` is NOT reset on loop; it continues to increment across loop boundaries and is used solely as the round-robin buffer index (`(m_samplesQueued / kSamplesPerBuffer) % kNumBuffers`).
  - The stream thread interval remains **10 ms** — no change for looping music stems.
  - Music stem OGG files must be authored with a seamless boundary at sample 0 (no silence, no audible transient at the loop point) to ensure a click-free loop when the seek-to-0 occurs.
  - The `m_nextBarBoundary` counter tracks the next crossfade bar boundary in samples and is **not** affected by the EOF seek; the bar counter increments monotonically across loop boundaries using `m_samplesQueued` as the base.

- **Pre-loaded looping OGG SFX — loop point handling**: The "Looping game SFX" tier (5–20 s, OGG, pre-loaded into a single AL buffer) must use `AL_SOFT_loop_points` extension if available, to honor authored loop metadata. **Threading requirement**: All pre-loaded OGG buffer operations (`alBufferData`, `alBufferiv`, `alGetProcAddress("alBufferiv")`) must be performed on the **audio thread** (which has a guaranteed current context via `alcSetThreadContext`). Do NOT perform these operations on the main thread after the audio thread has claimed the thread-local context — `alGetProcAddress` called on a thread without a current context may return null on some OpenAL implementations. Pre-loading work must be posted to the audio thread via a command queue and completed before entering the streaming loop. Check at init (on audio thread):

  ```cpp
  if (alIsExtensionPresent("AL_SOFT_loop_points")) {
      auto alBufferiv = reinterpret_cast<LPALBUFFERIV>(alGetProcAddress("alBufferiv"));
      if (!alBufferiv) {
          // Extension advertised but proc not found — skip loop point setup.
          // Buffer will loop at natural boundary (sample 0), which is acceptable
          // when the file is authored with a seamless boundary at sample 0.
      } else {
          ALint loopPts[2] = { 0, totalSamples }; // loop start/end in PCM sample frames
      // totalSamples: total number of PCM sample *frames* in the OGG file, obtained from
      // the decoder: ov_pcm_total(vf, -1) returns total PCM frames as ogg_int64_t.
      // A PCM sample frame = one sample per channel at one point in time (for mono = 1 sample,
      // for stereo = 2 interleaved samples, but OpenAL loop points count in frames, not interleaved values).
      // Do NOT use byte offsets or interleaved sample counts as the loop end value.
          alBufferiv(bufferId, AL_LOOP_POINTS_SOFT, loopPts);
      }
  }
  ```

  Loop points default to `(0, totalSamples)` when the extension is absent — the runtime's seek-to-0 via `ov_pcm_seek(vf, 0)` is sufficient. OGG files in this tier must be authored with a seamless boundary at sample 0 to ensure acceptable behavior without the extension. Pre-loaded OGG files exceeding 10 s should be reclassified to the streaming tier if `AL_SOFT_loop_points` is unavailable on a target platform.
