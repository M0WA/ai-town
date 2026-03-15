#pragma once

// IAlcFunctions — testable abstraction over ALC extension/proc-address queries.
//
// DESIGN: The primary (cross-platform) test seam for AudioSystem. Production code
// uses DefaultAlcFunctions which delegates to real ALC calls. Tests inject
// MockAlcFunctions (via a stub implementation) to simulate absent extensions
// (e.g., returning nullptr for "alcSetThreadContext") without requiring a real
// OpenAL device or hardware.
//
// AudioSystem constructor accepts IAlcFunctions* (defaults to DefaultAlcFunctions
// when nullptr is passed). This pattern works on both Linux and Windows — the
// weak-symbol override approach is Linux-only and would break the build-windows
// CI job.
//
// Interface contract:
//   isExtensionPresent(extName) — returns true if the named ALC extension is present.
//   getProcAddress(funcName)   — returns the function pointer for the named ALC
//                                function, or nullptr if not available.
//
// This header is intentionally free of all OpenAL includes so it can be included
// in test TUs that run without AL hardware.

struct IAlcFunctions {
    virtual ~IAlcFunctions() = default;

    // Returns true if the named ALC device-level extension is present.
    // Equivalent to alcIsExtensionPresent(device, extName) in the real implementation.
    virtual bool isExtensionPresent(const char* extName) = 0;

    // Returns the function pointer for the named ALC procedure, or nullptr if absent.
    // Equivalent to alcGetProcAddress(device, funcName) in the real implementation.
    virtual void* getProcAddress(const char* funcName) = 0;
};
