#pragma once
// al_check.h — AL/ALC error-checking seam for AudioSystem.
//
// Declares two inline wrappers that forward to the real implementations defined
// in AudioSystem.cpp.  The inline bodies here are intentional no-ops; they
// satisfy compilation for any TU that includes this header without needing
// <AL/al.h>.  AudioSystem.cpp provides the real non-inline definitions
// (alCheckError_real / alcCheckError_real) which throw std::runtime_error on
// ALenum != AL_NO_ERROR / ALenum != ALC_NO_ERROR.
//
// Seam contract:
//   alCheckError(op)            — call after every AL function (alSourcef, etc.)
//   alcCheckError(device, op)   — call after every ALC function (alcOpenDevice, etc.)
//
// The real implementations live in AudioSystem.cpp (not a separate al_check.cpp)
// because AudioSystem.cpp is the only TU that includes <AL/al.h> and <AL/alc.h>.
// All other TUs that include this header compile without an AL device.
//
// NOTE: void* is used for the device parameter in alcCheckError to avoid
// including <AL/alc.h> in this header (which would break the zero-AL-headers
// contract for TUs that only need the seam declaration).
void alCheckError_real(const char* op);
void alcCheckError_real(void* device, const char* op);

inline void alCheckError(const char* op)              { alCheckError_real(op); }
inline void alcCheckError(void* device, const char* op) { alcCheckError_real(device, op); }
