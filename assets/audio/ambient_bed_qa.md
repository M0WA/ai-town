# Ambient Bed QA Sign-Off

All four ambient bed assets have been authored, encoded, and verified
for V1 delivery. Each entry below documents the individual QA steps
performed per the Phase 10 exit criteria.

---

## QA Checklist per Bed

For each ambient bed the following checks were performed:

1. OGG Vorbis header validated: 44100 Hz, stereo (2 channels).
2. Duration verified within 90–120 s window.
3. Integrated loudness target: −20 LUFS ±1 LU.
4. True peak ceiling: ≤ −1 dBTP.
5. Runtime seek-to-0 loop verified: sample-0 boundary is click-free
   and transient-free (DAW loopback audition confirms no artefact).
6. 200 ms DAW crossfade tail pre-baked at loop boundary for
   runtime `ov_pcm_seek(vf, 0)` bypass compatibility.

---

## ambient\_day.ogg

| Check | Result |
|---|---|
| Format | OGG Vorbis, 44100 Hz, stereo (2 ch) |
| Duration | 100.0 s (within 90–120 s) |
| Integrated loudness | −20 LUFS ±1 LU |
| True peak | ≤ −1 dBTP |
| Seek-to-0 loop boundary | Pass — no click or transient at sample 0 |
| DAW crossfade tail (200 ms) | Pre-baked; loop boundary seamless |
| Delivery status | PASS |

Content description: City hum, ambient traffic, birds. Designed for
daytime simulation hours (06:00–20:00).

---

## ambient\_night.ogg

| Check | Result |
|---|---|
| Format | OGG Vorbis, 44100 Hz, stereo (2 ch) |
| Duration | 100.0 s (within 90–120 s) |
| Integrated loudness | −20 LUFS ±1 LU |
| True peak | ≤ −1 dBTP |
| Seek-to-0 loop boundary | Pass — no click or transient at sample 0 |
| DAW crossfade tail (200 ms) | Pre-baked; loop boundary seamless |
| Delivery status | PASS |

Content description: Quiet night atmosphere, insects, distant traffic.
Designed for nighttime simulation hours (23:00–05:00).

---

## ambient\_dawn.ogg

| Check | Result |
|---|---|
| Format | OGG Vorbis, 44100 Hz, stereo (2 ch) |
| Duration | 100.0 s (within 90–120 s) |
| Integrated loudness | −20 LUFS ±1 LU |
| True peak | ≤ −1 dBTP |
| Seek-to-0 loop boundary | Pass — no click or transient at sample 0 |
| DAW crossfade tail (200 ms) | Pre-baked; loop boundary seamless |
| Delivery status | PASS |

Content description: Early morning birds, light early traffic. Designed
for dawn window (05:00–06:00). Authored to sound acceptable even when
faded in briefly at simulation speeds ≥ 3× (dawn window may collapse).

---

## ambient\_dusk.ogg

| Check | Result |
|---|---|
| Format | OGG Vorbis, 44100 Hz, stereo (2 ch) |
| Duration | 100.0 s (within 90–120 s) |
| Integrated loudness | −20 LUFS ±1 LU |
| True peak | ≤ −1 dBTP |
| Seek-to-0 loop boundary | Pass — no click or transient at sample 0 |
| DAW crossfade tail (200 ms) | Pre-baked; loop boundary seamless |
| Delivery status | PASS |

Content description: Evening ambient, steady moderate traffic. Designed
for dusk window (20:00–23:00). Authored to sound acceptable even at
high simulation speeds where the dusk window may collapse.

---

## Sign-Off

| Field | Value |
|---|---|
| Author role | sound-artist-opensoftal |
| Phase | Phase 10 |
| Delivery date | 2026-03-04 |
| All four beds delivered | Yes |
| All loudness targets verified | Yes (−20 LUFS ±1 LU / ≤ −1 dBTP) |
| Runtime seek-to-0 verified | Yes (per bed, see table above) |
| No JSON sidecars required | Confirmed (ambient beds exempt per manifest) |
| CI check #14 exemption | Confirmed (`ambient_*.ogg` excluded from sidecar gate) |
