# Error Checking — Two Distinct Wrappers

- **`alCheckError(op)`**: calls `alGetError()`; used for all AL source/buffer/listener calls
- **`alcCheckError(device, op)`**: calls `alcGetError(device)`; used for all ALC device/context calls (`alcOpenDevice`, `alcCreateContext`, `alcMakeContextCurrent`)
- Both wrappers throw `std::runtime_error` on failure. Using `alGetError()` for ALC calls is incorrect and will miss context-level failures.
