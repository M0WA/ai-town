#pragma once
// Phase 3 stubs — no-op. Phase 7 replaces with real AL error checking.
// NOTE: void* is used for the device parameter to avoid including AL headers.
inline void alCheckError(const char* /*op*/) { /* Phase 7: real impl */ }
inline void alcCheckError(void* /*device*/, const char* /*op*/) { /* Phase 7: real impl */ }
