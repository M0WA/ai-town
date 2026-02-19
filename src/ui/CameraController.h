#pragma once
#include "src/platform/input_event.h"      // InputEvent definition
#include "src/interfaces/camera_state.h"   // CameraState struct — minimal header, no audio-domain types
// Do NOT include audio_types.h here: including it leaks SoundPriority, StingerType, SoundId,
// and other audio-domain types into every UI translation unit that includes CameraController.h.

// CameraController — processes pan/zoom/rotate input and maintains camera state.
// Source location: src/ui/ (locked at Phase 0 per testability-architecture.md).
// Moving to src/platform/ would break the src/ui/ coverage gate.
// Full implementation in Phase 1.
class CameraController {
public:
    bool         OnInputEvent(const InputEvent& event);   // capital O, const ref; returns true if consumed
    void         update(float dt);                         // must be called every frame before sceneManager->drawAll()
    CameraState  getCameraState() const;
};
