#pragma once
#include <string>
#include <vector>
#include "src/interfaces/audio_types.h"

// AudioCommandQueue — simple pre-load command container used to dispatch OGG buffer
// operations from the main thread constructor to the audio thread before the audio
// thread signals init complete.
//
// DESIGN: "Pattern B" (std::vector) from streaming-architecture.md — the main thread
// populates this vector entirely BEFORE launching m_audioThread. The std::thread
// constructor acts as a happens-before barrier for all writes performed before it,
// so no mutex is needed. The audio thread is the sole reader; after the initial
// drain loop the queue is never written or read again.
//
// The drain loop on the audio thread iterates the vector with a range-for:
//   for (auto& cmd : m_preloadQueue) { processPreloadCommand(cmd); }
//
// Post-V1 note: if runtime (post-construction) asset loading is ever required,
// a SEPARATE mutex-guarded command queue must be added to the streaming loop. This
// construction-time queue is intentionally one-shot and is NOT used after construction.

struct PreloadCommand {
    SoundId     soundId{0};
    std::string filePath;
    bool        looping{false};
};

// The queue itself is a plain std::vector — populated before thread launch
// with push_back(), drained once on the audio thread with a range-for.
using AudioPreloadQueue = std::vector<PreloadCommand>;
