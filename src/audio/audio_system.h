#pragma once
// audio_system.h — compatibility shim for Phase 7.
//
// The canonical AudioSystem header is AudioSystem.h (CamelCase, per CLAUDE.md
// C++ class naming rules).  This file exists because audio_thread_test.cpp was
// written before Phase 7 landed and uses the lowercase include path.
//
// Including this file is equivalent to including AudioSystem.h.
#include "src/audio/AudioSystem.h"
