// al_check.cpp — Phase 7 real implementation of alCheckError / alcCheckError.
//
// This is the ONLY translation unit that includes <AL/alc.h>.  The header
// al_check.h uses void* for the device parameter to avoid including <AL/alc.h>
// in the header itself (which would break headless-CI compilation in TUs that
// include al_check.h without an AL device).
//
// The functions defined here provide the real error-checking behaviour that the
// Phase 3 inline no-op stubs in al_check.h had deferred.  Because al_check.h
// declares inline stubs and this file defines non-inline functions with different
// linkage, linking al_check.cpp provides the real implementation that overrides
// the stubs in all TUs that also link this .o.
//
// Per error-checking.md:
//   - alCheckError(op)  calls alGetError(); throws std::runtime_error on failure.
//   - alcCheckError(device, op) calls alcGetError(device); throws on failure.
//   - Using alGetError() for ALC calls is incorrect and will miss context-level
//     failures — the two wrappers MUST remain separate.

#include "src/audio/al_check.h"

#include <AL/al.h>
#include <AL/alc.h>
#include <stdexcept>
#include <string>

// Override the inline no-op from al_check.h with the real implementation.
// The inline stubs are compiled into TUs that do NOT link al_check.cpp (e.g.,
// non-audio test TUs); linking al_check.cpp with the audio TUs provides the
// real behaviour through ODR resolution.
//
// Note: because al_check.h declares these inline, technically there is no
// redefinition conflict — the non-inline definitions here are separate symbols.
// The linker will resolve calls to alCheckError / alcCheckError from
// AudioSystem.cpp (which also includes al_check.h) to the definitions in this
// .o because AudioSystem.cpp is linked alongside al_check.cpp.
//
// However, to be safe and avoid ODR ambiguity, AudioSystem.cpp defines its own
// _real variants (alCheckError_real / alcCheckError_real) and calls them
// directly rather than routing through al_check.h.  This file is provided for
// completeness and for any future TU that uses the al_check.h macros.

void alCheckError(const char* op) {
    ALenum err = alGetError();
    if (err != AL_NO_ERROR) {
        const char* msg = alGetString(err);
        std::string s("AL error in '");
        s += op;
        s += "': ";
        s += msg ? msg : "unknown";
        throw std::runtime_error(s);
    }
}

void alcCheckError(void* device, const char* op) {
    ALenum err = alcGetError(reinterpret_cast<ALCdevice*>(device));
    if (err != ALC_NO_ERROR) {
        std::string s("ALC error in '");
        s += op;
        s += "': error code ";
        s += std::to_string(static_cast<int>(err));
        throw std::runtime_error(s);
    }
}
