# Audio Asset Formats — Three-Tier Classification

| Tier | Duration | Format | Loading strategy |
|---|---|---|---|
| Short SFX / UI | < 5 s | WAV PCM | Pre-loaded AL buffer |
| Looping game SFX | 5 s ≤ duration < 20 s | OGG Vorbis | Pre-loaded (full decode at load time into AL buffer) |
| Music / long ambient | ≥ 20 s | OGG Vorbis | Streamed via buffer queue (dedicated audio thread) |

**Tier 2 / Tier 3 boundary is EXCLUSIVE**: Tier 2 covers files strictly less than 20 s (i.e., duration < 20 s). Tier 3 covers files of exactly 20 s or longer (duration ≥ 20 s). A file that is exactly 20 s long is Tier 3 (streamed), not Tier 2.

**Asset-to-tier mapping**:

- Zone loops (`sfx_zone_residential`, `sfx_zone_commercial`, `sfx_zone_industrial`): authored at 12–18 s with a hard cap of 18 s — fall in **Tier 2** (pre-loaded, full decode into AL buffer). The 18 s hard cap ensures zone loops remain safely in Tier 2 with a 2 s margin below the 20 s boundary.
- Ambient beds (`ambient_day`, `ambient_night`, `ambient_dawn`, `ambient_dusk`): authored at 90–120 s — fall in **Tier 3** (streamed, dual-buffer 64 KB queue on the dedicated audio thread).

- Sample rate: 44100 Hz; Bit depth: 16-bit signed PCM
- 3D positional sounds: **mono** (OpenAL spatialization requires mono)
- Music / UI sounds: stereo; `AL_SOURCE_RELATIVE = AL_TRUE`
- OGG decoding library: **libvorbisfile** (vcpkg port `libvorbis`, CMake target `Vorbis::vorbisfile`, header `<vorbis/vorbisfile.h>`)
- **Seamless loop requirement — two methods by content type:**
  - **Ambient beds** (`ambient_day`, `ambient_night`, `ambient_dawn`, `ambient_dusk`): Use **DAW crossfade loops**. The final 200 ms of the encoded file must be a linear crossfade between the tail content and the head content, produced in a DAW by overlapping and blending the loop endpoints before encoding. Files must NOT contain a silent floor at either boundary. the runtime seeks to sample 0 (via `ov_pcm_seek(vf, 0)`) at end-of-file; the pre-baked crossfade in the file eliminates phase discontinuity without silence artifacts. **Dual-requirement**: Ambient beds must satisfy BOTH this DAW crossfade tail AND a seamless sample-0 boundary — because the streaming runtime seeks to sample 0 (`ov_pcm_seek(vf, 0)`) BEFORE the crossfade tail is reached, bypassing it entirely. **The seamless loop the player hears is produced entirely by the sample-0 boundary, not the crossfade tail.** The 200 ms crossfade tail is a DAW-audition fallback only. See the 'Ambient bed loop authoring — dual-requirement' section below for full details. Authors must treat the sample-0 boundary click-free check as the primary quality gate.
  - **Zone loops** (`zone_residential`, `zone_commercial`, `zone_industrial`): Use **silence-boundary loops**. The final 100 ms (tail) and the first 100 ms (head) must fade to and from silence (floor: **−60 dBFS**). Both boundaries must individually reach −60 dBFS or below. These shorter rhythmically punctuated loops contain natural transient gaps that make silence-boundary technique acceptable. **Authoring**: authors must manually apply linear fade envelopes to the source audio: (1) **tail fade-out** — the final 100 ms (4410 samples at 44100 Hz) must fade linearly from full level to ≤−60 dBFS; (2) **head fade-in** — the first 100 ms (4410 samples) must fade linearly from silence to full level. The two regions are independent and **do not overlap**. DAW loop verification: import the encoded OGG, loop it in the DAW player, and confirm the 200 ms combined window (tail + head) aligns with a natural rhythmic gap in the content. The runtime seeks to sample 0 on loop — the head fade ensures a smooth restart. **Loop body clarification**: The 100 ms fade-to-silence at both the head and tail are part of the loop body itself (not extra appended material). The OGG file duration includes these fades. When the runtime seeks to sample 0 on loop (via `ov_pcm_seek(vf, 0)`), the head fade provides a natural smooth restart. The tail fade ends at exactly the last sample of the file. **Loop boundary silence budget — explicit design contract**: When a zone loop completes one cycle and the runtime seeks to sample 0 (via `ov_pcm_seek(vf, 0)`), the loop boundary presents two consecutive silence regions: the 100 ms tail fade-to-silence followed immediately by the 100 ms head fade-from-silence. This produces a total **~200 ms silence window at every loop boundary**. This is a deliberate design choice, not an authoring error. The 200 ms silence window must coincide with a **natural rhythmic gap** in the content (e.g., after a phrase ending, between rhythmic events) — the loop content must be structured so that the 200 ms silence boundary falls during a natural pause. **Mandatory DAW verification step**: Authors must verify the loop boundary by looping the file in the DAW player, listening through the boundary, and confirming that the 200 ms silence coincides with a natural rhythmic gap in the content. If the silence window cuts across active content, adjust loop start/end trim points until the boundary lands in a natural gap. Zones loops that do not pass this verification must not be delivered.
  - **Music stems** (`music_calm_*`, `music_growth_*`, `music_crisis_*`): Use a **bar-aligned seamless loop**. The stem tail must land on a beat/harmonic resolution that connects directly to bar 1 at sample 0 with no fade, silence, or pre-baked crossfade at the boundary — the musical content must be self-looping. Authors must verify the seam using the DAW loopback-audition technique (loop the file in the DAW player, listen through the boundary). The total stem length must be an exact integer number of bars at the authored BPM so that loop points align to bar boundaries and the software sample counter crossfade logic fires correctly. **JSON sidecar required**: Every music stem OGG file must be accompanied by a `<stem_name>.json` sidecar in the same directory. The sidecar must contain at minimum: `{ "bpm": 90, "beats_per_bar": 4 }`. The `AudioSystem` reads this sidecar at load time to compute bar boundary sample indices for beat-aligned crossfades (see Dynamic Soundscape spec). Delivery without a sidecar is a build error — the asset pipeline must reject stems lacking a sidecar.
- **OGG encoding pipeline**: All OGG files must be encoded with a consistent encoder and quality setting to avoid inter-file loudness variation. **Channel count by source type**: Encode at 44100 Hz. Mono for all 3D positional sources (zone loops, vehicle SFX, environmental sounds). Stereo for ambient beds and music stems. **Quality by content type**:
  - Music stems: **libvorbis quality −q 8** (approx. 256 kbps VBR) — preserves complex transients and harmonic content across full stereo frequency range.
  - Ambient beds: **libvorbis quality −q 7** (approx. 224 kbps VBR) — stereo city ambience (birds, traffic, environmental mix) requires higher quality than mono zone loops to avoid compression artifacts on complex layered content. Zone loops use −q 6; ambient beds use −q 7.
  - Zone loops: **libvorbis quality −q 6** (approx. 192 kbps VBR) — mono positional loops with simpler content; −q 6 is sufficient.
  Authors must verify the seam using a DAW loopback-audition technique (loop the file in the DAW's audio player and listen through the boundary) before delivery. No Vorbis LOOPSTART/LOOPLENGTH tags are required — the `AudioStream` implementation loops by seeking to 0.
- **Ambient bed loop authoring — dual-requirement**: Ambient beds must satisfy TWO simultaneous authoring requirements:
  1. **DAW crossfade tail** (production fallback): The final 200 ms of the encoded file is a pre-baked crossfade between tail content and head content (as described above). This is used when the file is played in non-streaming contexts.
  2. **Seamless sample-0 boundary** (streaming runtime path): The content at sample 0 must be clean and loop-friendly with no audible transient or silence — because the `AudioStream` runtime seek-to-0 approach (see Streaming Architecture) seeks to sample 0 BEFORE reaching the pre-baked crossfade tail, effectively bypassing the DAW crossfade entirely. The runtime loop relies on the seamless sample-0 boundary, not the pre-baked crossfade.

  **PRIMARY DELIVERY GATE: The sample-0 boundary must be click-free when looped in a DAW by placing a loop point exactly at sample 0 and auditioning through it. The 200 ms DAW crossfade tail is a SECONDARY authoring safeguard only. Any ambient bed that fails the sample-0 click-free gate must be rejected regardless of whether the crossfade tail sounds acceptable.**

  In practice, authors should structure the ambient bed so that: (a) bar 1 starts clean at sample 0, and (b) the file extends 200 ms past its natural loop endpoint with a pre-baked crossfade tail. The streaming runtime never decodes the crossfade tail. Non-streaming playback (e.g., preview in the DAW) hears the crossfade tail and loops cleanly through it. Both requirements can be satisfied by any well-authored ambient bed.

## Phase 10 QA Artifact Formats

QA artifacts are not runtime game assets. They are authoring verification files committed
to the repository as evidence of sound artist review. The format requirements below are
authoritative — `v1-audio-asset-manifest.md` "Phase 10 QA Delivery Artifacts" section
contains the full content specifications for each file.

### Crossfade demo WAV files

`assets/audio/crossfade_demos/crossfade_demo_calm_to_growth.wav` and
`assets/audio/crossfade_demos/crossfade_demo_mainmenu_to_calm.wav`:

- **Format**: WAV PCM (uncompressed, linear PCM encoding — audio format tag 0x0001)
- **Sample rate**: 44100 Hz
- **Bit depth**: 16-bit signed integer
- **Channels**: stereo (2 channels, interleaved L/R)
- **Duration**: exactly 15 s (660,600 stereo PCM frames)
- **True-peak ceiling on render**: apply −0.1 dBTP true-peak limiter to prevent clipping;
  do NOT loudness-normalise — the raw gain crossfade curve must be audible in the file
- **No OGG, no lossy encoding**: WAV PCM is mandatory for these demo files; OGG
  encoding would introduce a decode/encode generation loss and hide any subtle gain
  curve artifacts that the QA render is designed to surface

These files are not loaded by `AudioSystem`. They are stored in the repository solely
as auditable evidence of pre-production crossfade compatibility verification.

### Day-to-night crossfade demo OGG

`assets/audio/crossfade_demo_day_to_night.ogg`:

- **Format**: OGG Vorbis
- **Sample rate**: 44100 Hz
- **Channels**: stereo (2 channels) — matches the ambient bed format so that the
  stereo field width of the crossfade is accurately represented
- **OGG quality**: minimum `-q 7` (approx. 224 kbps VBR) — consistent with ambient
  bed encoding quality
- **Duration**: 10–15 s
- **True-peak ceiling on render**: apply −0.1 dBTP true-peak limiter before OGG
  encoding to prevent encoder clipping; do NOT loudness-normalise
- **No JSON sidecar required** — this file does not use bar-boundary metadata; the
  sidecar check (`validate_assets.py` Check #14) applies only to `music_*.ogg` files
  and does NOT apply to any file in the `crossfade_demos/` path or to this file

This file is not loaded by `AudioSystem`. It is stored alongside production ambient bed
assets solely as a day→night ambient transition QA artifact. See
`architecture/audio-architecture/dynamic-soundscape.md` "Authoring note — dawn/dusk
collapse at default simulation speed" for the full acceptance criterion.
