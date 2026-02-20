# HRTF Initialization

```cpp
// Request HRTF explicitly:
ALCint attrs[] = { ALC_HRTF_SOFT, ALC_TRUE, 0 };
context_ = alcCreateContext(device_, attrs);
alcCheckError(device_, "alcCreateContext");

// MANDATORY: Make context current on the main thread BEFORE any AL* call.
// Without this, alDistanceModel() below runs with no current context and silently fails,
// leaving the default AL_INVERSE_DISTANCE (unclamped) which causes gain clipping on
// near-field sources. The audio thread will later call alcSetThreadContext() for its
// own thread-local context — that does not affect this main-thread process-wide context.
if (!alcMakeContextCurrent(context_)) {
    alcCheckError(device_, "alcMakeContextCurrent");
    throw std::runtime_error("AudioSystem: alcMakeContextCurrent failed");
}

// Verify HRTF actually activated (requires ALC_SOFT_HRTF extension):
if (alcIsExtensionPresent(device_, "ALC_SOFT_HRTF")) {
    ALCint hrtfStatus;
    alcGetIntegerv(device_, ALC_HRTF_STATUS_SOFT, 1, &hrtfStatus);
    if (hrtfStatus != ALC_HRTF_ENABLED_SOFT)
        logWarning("HRTF requested but not enabled. Deploy default.mhr alongside the binary.");
}
```

- Ship `default.mhr` (HRTF data file) alongside `soft_oal.dll` on Windows and in the Linux package
- HRTF failure is a warning (degrade gracefully), not a hard abort
- **Distance model initialization**: After `alcMakeContextCurrent` succeeds and before any source is created, call:

  ```cpp
  alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);
  alCheckError("alDistanceModel");
  // Set default rolloff factor for all subsequently created sources:
  // (per-source rolloff is set at source creation time per category)
  ```

  The `alDistanceModel` call is mandatory — the OpenAL default is `AL_INVERSE_DISTANCE` (unclamped), which allows source gain to exceed 1.0 for near-field sources below the reference distance, causing audio clipping. `AL_INVERSE_DISTANCE_CLAMPED` caps gain at the reference distance.
