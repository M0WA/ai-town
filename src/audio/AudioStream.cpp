// AudioStream.cpp — OGG Vorbis streaming utilities (libvorbisfile wrapper).
#include "src/audio/AudioStream.h"

#include <vorbis/vorbisfile.h>
#include <cstring>

namespace AudioStreamUtils {

bool openOGG(const std::string& path, OggVorbis_File* vf) noexcept {
    if (!vf) return false;
    int rc = ov_fopen(path.c_str(), vf);
    return rc == 0;
}

void closeOGG(OggVorbis_File* vf) noexcept {
    if (vf) {
        ov_clear(vf);
    }
}

bool seekToStart(OggVorbis_File* vf) noexcept {
    if (!vf) return false;
    return ov_pcm_seek(vf, 0) == 0;
}

int decodeFrames(OggVorbis_File* vf, int16_t* pcmBuf, int maxFrames, int channels) noexcept {
    if (!vf || !pcmBuf || maxFrames <= 0 || channels <= 0) return 0;

    int bytesWanted = maxFrames * channels * static_cast<int>(sizeof(int16_t));
    char* buf = reinterpret_cast<char*>(pcmBuf);
    int   totalBytes = 0;
    int   bitstream  = 0;

    while (totalBytes < bytesWanted) {
        long n = ov_read(vf,
                         buf + totalBytes,
                         bytesWanted - totalBytes,
                         0,       // little-endian
                         2,       // 16-bit samples
                         1,       // signed
                         &bitstream);
        if (n == 0) {
            // EOF — signal to caller via total bytes read (may be short read).
            break;
        } else if (n < 0) {
            // Decoding error — return negative to signal error.
            return static_cast<int>(n);
        }
        totalBytes += static_cast<int>(n);
    }

    // Convert bytes to frames.
    int bytesPerFrame = channels * static_cast<int>(sizeof(int16_t));
    return totalBytes / bytesPerFrame;
}

bool getInfo(OggVorbis_File* vf, int& sampleRate, int& channels) noexcept {
    if (!vf) return false;
    vorbis_info* vi = ov_info(vf, -1);
    if (!vi) return false;
    sampleRate = static_cast<int>(vi->rate);
    channels   = static_cast<int>(vi->channels);
    return true;
}

int64_t getTotalFrames(OggVorbis_File* vf) noexcept {
    if (!vf) return -1;
    ogg_int64_t total = ov_pcm_total(vf, -1);
    return static_cast<int64_t>(total);
}

} // namespace AudioStreamUtils
