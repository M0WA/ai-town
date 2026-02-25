#!/usr/bin/env python3
"""
AI Town asset validation script.
Phase 4 stub — always exits 0.
Phase 5 adds real validation.
"""
import glob
import wave

VEHICLE_ENGINE_LOOP_MIN_DURATION_S = 6.0  # mirrors kVehicleEngineLoopMinDurationSeconds in src/interfaces/audio_types.h — update both if threshold changes
ZONE_LOOP_MAX_PRELOAD_DURATION_S = 18.0   # mirrors kZoneLoopMaxPreloadDurationSeconds in src/interfaces/audio_types.h — update both if threshold changes


def check_1():
    # Valid DDS suffixes (Phase 5 will enforce):
    # _d (diffuse), _n (normal), _s (specular), _sp (specular packed — roughness/metallic/AO), _lm (lightmap), _billboard
    # TODO Phase 5: validate that every .dds file ends in one of the above suffixes
    pass


def check_2():
    # NOTE: splat maps are RGBA8 UNORM PNG files — never DDS; they are NOT in scope for the DDS suffix validation.
    # _splat suffix: splat maps are RGBA8 UNORM PNG files — NEVER DDS.
    # NOTE: _splat.png IS a valid asset suffix (RGBA8 UNORM PNG); _splat.dds is an ERROR.
    # The six-suffix DDS validation table (_d, _n, _s, _sp, _lm, _billboard) does NOT include
    # _splat — _splat files are PNG only and are validated by a separate PNG-format check.
    # TODO Phase 5: hard error if any file named *_splat.dds exists — DXT compression
    # corrupts splat map blend weight gradients (smooth 0–255 values become quantized blocks).
    pass


def check_3():
    # TODO Phase 5+: validate _billboard.dds format
    # Mandatory billboard DDS constraints:
    # - Fixed resolution: 1024x128 px
    # - Mandatory DXT format: DXT5/BC3 (not DXT1)
    # - Mandatory sRGB flag: GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT
    #   (NOTE: GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT is the GPU UPLOAD internal format —
    #   it is NOT a DDS header field. DDS FILE validation uses the DX10 DXGI_FORMAT field
    #   (value 78 = BC3_UNORM_SRGB). These are two distinct operations.)
    pass


def check_4():
    # - Mandatory mip count: exactly 4 levels
    #
    # dwMipMapCount is at byte offset 28 in the standard DDS header
    # import struct; d=open(f,'rb').read(); mip_count = struct.unpack('<I', d[28:32])[0]
    # TODO Phase 5: hard error if mip_count < 4
    pass


def check_5():
    # TODO Phase 5: hard error if billboard DDS does not contain a DX10 extended header
    #   (FourCC at bytes 84-87 != b'DX10') — sRGB colorspace indeterminate without
    #   DX10 header; file MUST be rejected
    #   If DX10 header is present, DXGI_FORMAT is at bytes 128-131:
    #   dxgi_fmt = struct.unpack('<I', d[128:132])[0]
    # TODO Phase 5: hard error if DXGI_FORMAT != 78 (BC3_UNORM_SRGB) for _billboard.dds
    # (value 78 = DXGI_FORMAT_BC3_UNORM_SRGB)
    pass


def check_6():
    # TODO Phase 5: hard error if _lod2_lm.dds is not DXT5
    #   (DXGI_FORMAT_BC3_UNORM = 77 for linear DXT5; _lod2_lm.dds is a lightmap —
    #   linear, NOT sRGB; DXGI_FORMAT_BC3_UNORM_SRGB = 78 applies to _billboard.dds only;
    #   read DXGI_FORMAT from bytes 128-131)
    pass


def check_7():
    # _billboard.dds MUST NOT co-exist with _lod2.b3d for the same base name —
    # co-existence is a critical asset authoring error (the two asset types are mutually
    # exclusive: height_floors <= 3 uses billboard DDS; height_floors >= 4 uses _lod2.b3d).
    # - Must not co-exist with _lod2.b3d for same asset base name
    # TODO Phase 5: hard error if both *_billboard.dds and *_lod2.b3d exist for the same base name.
    pass


def check_8():
    # Check #8 per architecture/asset-standards/3d-model-standards.md:
    # Pivot/extent tolerance 5 mm
    # TODO Phase 5: hard error if any building .b3d pivot deviates more than 5 mm from expected origin
    pass


def check_9():
    # Check #9 per architecture/asset-standards/3d-model-standards.md:
    # LOD hysteresis >= 5 m (close), >= 10 m (far)
    # TODO Phase 5: hard error if LOD hysteresis band < 5 m (close) or < 10 m (far)
    pass


def check_10():
    # Check #10 per architecture/asset-standards/3d-model-standards.md:
    # Vehicle atlas UV
    # TODO Phase 5: hard error if vehicle atlas UV coordinates are out of valid range
    pass


def check_11():
    # Check #11 per architecture/asset-standards/3d-model-standards.md:
    # buildings with height_floors >= 4 MUST have _lod2.b3d;
    # buildings with height_floors <= 3 MUST NOT have _lod2.b3d (billboard only).
    # The boundary is inclusive at 4 — not > 3. This two-sided check catches both a missing
    # LOD2 shell on tall buildings AND a spurious LOD2 mesh on short buildings that should
    # use billboard-only LOD2.
    # TODO Phase 5: hard error on either missing _lod2.b3d (height_floors >= 4) or
    # spurious _lod2.b3d (height_floors <= 3)
    pass


def check_12():
    # Check #12 per architecture/asset-standards/3d-model-standards.md:
    # Vehicle normal atlas UV (8x8 grid of 256x256 px cells)
    # TODO Phase 5: hard error if vehicle normal atlas UV layout does not match 8x8 grid spec
    pass


def check_13():
    # Check #13 per architecture/asset-standards/3d-model-standards.md:
    # Facade atlas cell pixels — all non-transparent pixel content within [8, 504] texel range
    # on both U and V axes per cell; DDS fourCC determines transparency interpretation
    # (DXT1: 1-bit alpha; DXT5/BC3: alpha > 0)
    # CROSS-REFERENCE (ISSUE-F): Any _billboard.dds that passes the floor-count gate
    # (height_floors <= 3) MUST ALSO be authored with a DX10 extended header and
    # DXGI_FORMAT = BC3_UNORM_SRGB (value 78) per architecture/asset-standards/2d-texture-standards.md
    # 'Validating sRGB DDS Output' section. The floor-count check and the DX10 sRGB check
    # are independent validations — a billboard file that passes the floor-count check but
    # lacks a DX10 extended header is still a hard validation error at Phase 5.
    # TODO Phase 5: apply BOTH the floor-count check AND the DX10 DXGI_FORMAT sRGB check
    # to every _billboard.dds file.
    pass


def check_14():
    # music_*.ogg (ALL music files): co-located .json sidecar required
    # TODO Phase 5: hard error if any music_*.ogg has no matching <basename>.json sidecar
    # NOTE: The sidecar check pattern is music_*.ogg ONLY — NOT sfx_*.ogg or ambient_*.ogg.
    # ambient_*.ogg are EXEMPT from the sidecar requirement.
    # TODO Phase 5: validate sidecar JSON against tools/music_sidecar_schema.json
    # (required fields: bpm (integer >= 1), beats_per_bar (integer >= 1);
    # additionalProperties: false)
    pass


def check_15():
    # Phase 9 stub — .meta sidecar file presence check
    # TODO Phase 9: hard error if any asset file lacks a co-located .meta sidecar
    pass


def check_16():
    """check_16: music_*.ogg and ambient_*.ogg must be stereo, 44100 Hz."""
    try:
        from mutagen.oggvorbis import OggVorbis
    except ImportError:
        print("SKIP check_16: mutagen not installed")
        return
    patterns = list(glob.glob("assets/audio/music_*.ogg")) + list(glob.glob("assets/audio/ambient_*.ogg"))
    if not patterns:
        print("INFO check_16: no music/ambient OGG files found — no-op")
        return
    for path in patterns:
        f = OggVorbis(path)
        if f.info.channels != 2:
            raise AssertionError(f"check_16 FAIL: {path} must be stereo (channels=2), got {f.info.channels}")
        if f.info.sample_rate != 44100:
            raise AssertionError(f"check_16 FAIL: {path} must be 44100 Hz, got {f.info.sample_rate}")
    print(f"check_16 PASS: {len(patterns)} music/ambient OGG files verified stereo 44100 Hz")


def check_17():
    """check_17: sfx_vehicle_engine_*.ogg must be mono, 44100 Hz, duration >= VEHICLE_ENGINE_LOOP_MIN_DURATION_S."""
    try:
        from mutagen.oggvorbis import OggVorbis
    except ImportError:
        print("SKIP check_17: mutagen not installed")
        return
    patterns = glob.glob("assets/audio/sfx_vehicle_engine_*.ogg")
    if not patterns:
        print("INFO check_17: no vehicle engine OGG files found — no-op")
        return
    for path in patterns:
        f = OggVorbis(path)
        if f.info.channels != 1:
            raise AssertionError(f"check_17 FAIL: {path} must be mono (channels=1), got {f.info.channels}")
        if f.info.sample_rate != 44100:
            raise AssertionError(f"check_17 FAIL: {path} must be 44100 Hz, got {f.info.sample_rate}")
        if f.info.length < VEHICLE_ENGINE_LOOP_MIN_DURATION_S:
            raise AssertionError(f"check_17 FAIL: {path} duration {f.info.length:.2f}s < {VEHICLE_ENGINE_LOOP_MIN_DURATION_S}s minimum")
    print(f"check_17 PASS: {len(patterns)} vehicle engine OGG files verified")


def check_18():
    """check_18: sfx_zone_*.ogg must be mono, 44100 Hz, duration <= ZONE_LOOP_MAX_PRELOAD_DURATION_S."""
    try:
        from mutagen.oggvorbis import OggVorbis
    except ImportError:
        print("SKIP check_18: mutagen not installed")
        return
    patterns = glob.glob("assets/audio/sfx_zone_*.ogg")
    if not patterns:
        print("INFO check_18: no zone loop OGG files found — no-op")
        return
    for path in patterns:
        f = OggVorbis(path)
        if f.info.channels != 1:
            raise AssertionError(f"check_18 FAIL: {path} must be mono (channels=1), got {f.info.channels}")
        if f.info.sample_rate != 44100:
            raise AssertionError(f"check_18 FAIL: {path} must be 44100 Hz, got {f.info.sample_rate}")
        if f.info.length > ZONE_LOOP_MAX_PRELOAD_DURATION_S:
            raise AssertionError(f"check_18 FAIL: {path} duration {f.info.length:.2f}s > {ZONE_LOOP_MAX_PRELOAD_DURATION_S}s maximum")
    print(f"check_18 PASS: {len(patterns)} zone loop OGG files verified")


def check_19():
    """check_19: stinger_*.wav must be mono uncompressed PCM."""
    patterns = glob.glob("assets/audio/stinger_*.wav")
    if not patterns:
        print("INFO check_19: no stinger WAV files found — no-op")
        return
    for path in patterns:
        with wave.open(path, 'rb') as w:
            if w.getnchannels() != 1:
                raise AssertionError(f"check_19 FAIL: {path} must be mono, got {w.getnchannels()} channels")
            if w.getcomptype() != 'NONE':
                raise AssertionError(f"check_19 FAIL: {path} must be uncompressed PCM, got {w.getcomptype()}")
    print(f"check_19 PASS: {len(patterns)} stinger WAV files verified mono PCM")


if __name__ == '__main__':
    print("validate_assets.py: Phase 5 — audio format checks active; 3D model checks are stubs.")
    check_1(); check_2(); check_3(); check_4(); check_5()
    check_6(); check_7(); check_8(); check_9(); check_10()
    check_11(); check_12(); check_13(); check_14(); check_15()
    check_16(); check_17(); check_18(); check_19()
