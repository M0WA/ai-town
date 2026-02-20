# Error Checking — Two Distinct Wrappers

- **`alCheckError(op)`**: calls `alGetError()`; used for all AL source/buffer/listener calls
- **`alcCheckError(device, op)`**: calls `alcGetError(device)`; used for all ALC device/context calls (`alcOpenDevice`, `alcCreateContext`, `alcMakeContextCurrent`)
- Both wrappers throw `std::runtime_error` on failure. Using `alGetError()` for ALC calls is incorrect and will miss context-level failures.

## Phase 1 Stub Signature: `alcCheckError` void* Parameter

The Phase 1 `al_check.h` stub declares `alcCheckError` with a `void*` parameter instead of `ALCdevice*`:

```cpp
// al_check.h (Phase 1 stub — signature frozen permanently)
inline void alcCheckError(void* /*device*/, const char* /*op*/) { /* Phase 4: real impl in al_check.cpp */ }

// al_check.cpp (Phase 4 — includes <AL/alc.h>, never included by al_check.h)
// void alcCheckError(void* device, const char* op) {
//     ALenum err = alcGetError(reinterpret_cast<ALCdevice*>(device));
//     if (err != ALC_NO_ERROR) { /* handle */ }
// }
```

**Rationale for `void*` in the header**:

- Using `void*` for the `device` parameter avoids including `<AL/alc.h>` in `al_check.h`. If `al_check.h` included `<AL/alc.h>` directly, every translation unit (TU) that includes `al_check.h` would transitively pull in the full OpenAL ALC type definitions — leaking OpenAL types into non-audio TUs (e.g., simulation, UI, terrain) that have no need for them. The `void*` parameter keeps `al_check.h` self-contained with no OpenAL dependencies.

**Phase 4 implementation details**:

- Phase 4 creates `src/audio/al_check.cpp`, which is the only file that includes `<AL/alc.h>`. Inside the `.cpp` file, `device` is cast back to the real type: `alcGetError(reinterpret_cast<ALCdevice*>(device))`.
- The header signature stays `void*` permanently — do NOT change it to `ALCdevice*` in Phase 4 or any later phase. Changing it would require `<AL/alc.h>` to appear in the header and defeat the include-isolation purpose.

**Call sites**:

- All call sites that hold an `ALCdevice*` value pass it directly to `alcCheckError`. The implicit `ALCdevice*` to `void*` conversion at the call site is defined behavior in C++ (any pointer-to-object type converts implicitly to `void*`). No explicit cast is required at call sites.
