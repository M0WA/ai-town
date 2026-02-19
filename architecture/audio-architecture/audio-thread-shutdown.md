# Audio Thread Shutdown Sequence (destructor — mandatory order)

1. `m_stopThread.store(true)`
2. `m_streamCV.notify_all()` — wake streaming thread immediately
3. **Guard join with `joinable()`**: `if (m_audioThread.joinable()) m_audioThread.join();` — always check `joinable()` before calling `join()`. If the constructor threw before the audio thread was started (e.g., `ALC_EXT_thread_local_context` was absent), `m_audioThread` is a default-constructed `std::thread` (not joinable); calling `join()` on it is undefined behavior.
3.5. **Re-bind context to main thread before any AL cleanup**: After the audio thread has joined, the thread-local context (set by `alcSetThreadContext` in the audio thread) no longer applies. The main thread must re-establish the context as current before making any subsequent AL/ALC calls. Guard this step: `if (m_context && m_useThreadLocalCtx) { alcMakeContextCurrent(m_context); }` — only re-bind if thread-local context was active AND the context was successfully created. If `m_context` is null (constructor failed before `alcCreateContext`), skip all AL/ALC resource deletion and context teardown (steps 4–7) — there are no resources to clean up. **This step is mandatory when `m_context != nullptr && m_useThreadLocalCtx`** — AL resource deletion calls (steps 4–5) are undefined behavior on a thread with no current context. Calling `alcMakeContextCurrent(m_context)` here is safe because the audio thread has fully exited (join() returned), so there is no longer a concurrent AL-calling thread for the context.
4. For every streaming source: `alSourceStop(src)` then query the live buffer count and unqueue all buffers before deletion. **Always query `AL_BUFFERS_QUEUED` after `alSourceStop` — never hardcode the buffer count**, as the thread may have stopped mid-cycle with a non-8 queued count:
```cpp
// For each streaming source:
alSourceStop(src);
ALint queued = 0;
alGetSourcei(src, AL_BUFFERS_QUEUED, &queued);
if (queued > 0) {
    std::vector<ALuint> tmp(queued);
    alSourceUnqueueBuffers(src, queued, tmp.data());
}
```
**Omitting these calls causes `AL_INVALID_OPERATION` on `alDeleteBuffers()`** because OpenAL refuses to delete buffers that are still attached to a source queue.
4a. For every SFX pool source (indices 0 to kSFXPoolSize-1, i.e., 0..57 in V1): call `alSourceStop(src)` then `alSourcei(src, AL_BUFFER, 0)` to detach the static buffer binding. Required before `alDeleteBuffers` — OpenAL returns `AL_INVALID_OPERATION` if buffers are still attached to sources at deletion time. SFX pool sources use `AL_BUFFER` binding (not a queue), so `alSourceUnqueueBuffers` is not applicable; setting `AL_BUFFER` to 0 detaches the binding.
4b. Release all EFX lowpass filter objects — one per evictable source (stinger and stream sources have no occlusion filters). **Only execute if `m_efxAllocationAttempted == true`** (NOT `m_efxAvailable`). Must occur after step 3 (thread join) and before step 7 (alcDestroyContext).

  **Why `m_efxAllocationAttempted`, not `m_efxAvailable`**: These two booleans serve different roles, defined in audio-system.md and audio-occlusion.md:
  - `m_efxAllocationAttempted` is set to `true` once at the start of the EFX filter allocation loop and is **never changed thereafter**. It signals that the allocation loop was entered and that some `m_occlusionFilter[]` entries may be non-null.
  - `m_efxAvailable` is set to `false` mid-loop on any partial allocation failure (e.g., `m_fnGenFilters` returns `AL_FILTER_NULL`, or a subsequent `m_fnFilteri`/`m_fnFilterf` setup call fails). It guards runtime occlusion paths — when `false`, the audio thread skips per-frame filter updates.

  Using `m_efxAvailable` as the shutdown guard would incorrectly skip cleanup when partial allocation occurred: `m_efxAvailable` is `false` after failure, but some filters in `m_occlusionFilter[0..i-1]` are already allocated and must be deleted. `m_efxAllocationAttempted` is `true` in this case and correctly allows the shutdown loop to run, where the per-entry `!= AL_FILTER_NULL` check handles partially-allocated arrays safely.

  **Loop bound: use `kEvictableSFXCount` (55), NOT `kSFXPoolSize` (58) or `kTotalSources` (62)** — stinger sources [55..57] and stream sources [58..61] have no occlusion filters; iterating beyond index 54 accesses out-of-bounds memory. **Do NOT use a single batch `m_fnDeleteFilters(kEvictableSFXCount, m_occlusionFilter)` call** — if any filter IDs were not allocated (e.g., due to partial initialization), passing zero IDs to `alDeleteFilters` produces `AL_INVALID_NAME`. Use a per-filter loop that checks for valid IDs:
  ```cpp
  if (m_efxAllocationAttempted) {
      for (int i = 0; i < kEvictableSFXCount; ++i) {  // NOTE: kEvictableSFXCount (55), NOT kSFXPoolSize
          if (m_occlusionFilter[i] != AL_FILTER_NULL) {
              m_fnDeleteFilters(1, &m_occlusionFilter[i]);
              m_occlusionFilter[i] = AL_FILTER_NULL;
          }
      }
  }
  ```
  This is safe for both fully-allocated pools and partial allocations (where the loop stopped early due to `m_fnGenFilters` failure). **Mid-loop allocation failure cleanup**: If `m_fnGenFilters` allocates a filter (non-null ID) but the subsequent `m_fnFilteri` or `m_fnFilterf` setup call fails, the filter must be deleted immediately and the ID explicitly reset to `AL_FILTER_NULL` before breaking out of the allocation loop:
  ```cpp
  m_fnGenFilters(1, &m_occlusionFilter[i]);
  if (m_occlusionFilter[i] == AL_FILTER_NULL) break;  // allocation failed; remaining entries stay null
  if (/* setup fails */) {
      m_fnDeleteFilters(1, &m_occlusionFilter[i]);  // clean up the allocated filter
      m_occlusionFilter[i] = AL_FILTER_NULL;         // mark as unallocated for shutdown loop
      break;
  }
  ```
  Without this, the shutdown loop's `!= AL_FILTER_NULL` check cannot distinguish "allocated but setup failed" from "fully set up".
  **V1 constant summary**: `kEvictableSFXCount = 55` (sources[0..54] receive EFX filters; sources[55..57] are stingers/reserved with no filters; sources[58..61] are streams with no filters).
5. `alDeleteSources(...)` + `alDeleteBuffers(...)` — release all AL resources (safe only after steps 4 and 4a)
6. `alcMakeContextCurrent(nullptr)`
7. `alcDestroyContext(m_context)`
8. `alcCloseDevice(m_device)`

**Never** destroy the context (steps 5–7) before joining the thread (step 3). The streaming thread calling AL functions on a destroyed context is undefined behavior and will crash.
