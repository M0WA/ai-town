#pragma once
// AudioStream.h — OGG Vorbis streaming helpers.
//
// Provides OGG open/close/read utilities used by AudioSystem's streaming loop.
// The OggVorbis_File struct is declared here via a forward declaration to avoid
// pulling <vorbis/vorbisfile.h> into any non-audio TU.
//
// All functions in this header are implemented in AudioStream.cpp which includes
// the real libvorbisfile headers.

#include <cstdint>
#include <string>

struct OggVorbis_File;

namespace AudioStreamUtils {

// Open an OGG file at path.  The caller provides an already-heap-allocated
// OggVorbis_File struct.  Returns true on success.
bool openOGG(const std::string& path, OggVorbis_File* vf);

// Close an OGG file.  Calls ov_clear() and does NOT free the vf pointer.
void closeOGG(OggVorbis_File* vf);

// Seek to sample-frame 0 (loop restart).
bool seekToStart(OggVorbis_File* vf);

// Decode up to maxFrames PCM frames from vf into pcmBuf (interleaved int16).
// channels: 1 = mono, 2 = stereo.
// Returns number of frames decoded, 0 at EOF, negative on error.
int decodeFrames(OggVorbis_File* vf, int16_t* pcmBuf, int maxFrames, int channels);

// Query vorbis_info: fills sampleRate and channels.  Returns false if vf is
// not open or the info struct is null.
bool getInfo(OggVorbis_File* vf, int& sampleRate, int& channels);

// Return total PCM sample frames in the stream (-1 on error or unseekable).
int64_t getTotalFrames(OggVorbis_File* vf);

} // namespace AudioStreamUtils
