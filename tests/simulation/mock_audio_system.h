#pragma once
#include "src/interfaces/IAudioSystem.h"
#include "gmock/gmock.h"

// MockAudioSystem — GMock implementation of IAudioSystem's 11 methods.
// Source location: tests/simulation/ (shared across simulation_tests, ui_tests, audio_tests).
// Header-only — no .cpp file. Uses MOCK_METHOD macros only, no definitions.
class MockAudioSystem : public IAudioSystem {
public:
    MOCK_METHOD(SoundHandle, playSound,            (SoundId id, SoundPriority priority, float gain),         (override));
    MOCK_METHOD(SoundHandle, playPositionalSound,  (SoundId id, vec3 pos, SoundPriority priority, float gain), (override));
    MOCK_METHOD(void,        stopSound,            (SoundHandle handle),                                      (override));
    MOCK_METHOD(void,        setMusicTrack,        (MusicTrackId id),                                         (override));
    MOCK_METHOD(void,        setSpeed,             (SimSpeed speed),                                          (override));
    MOCK_METHOD(void,        triggerStinger,       (StingerType type),                                        (override));
    MOCK_METHOD(void,        syncListenerToCamera, (const CameraState& cam),                                  (override));
    MOCK_METHOD(void,        setGameOverState,     (bool active),                                             (override));
    MOCK_METHOD(void,        setTimeOfDay,         (TimeOfDay tod),                                           (override));
    MOCK_METHOD(void,        transitionToGameplay, (),                                                        (override));
    MOCK_METHOD(void,        update,               (float realDeltaSeconds),                                  (override));
};
