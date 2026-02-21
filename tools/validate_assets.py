#!/usr/bin/env python3
"""
AI Town asset validation script.
Phase 1 stub — always exits 0.
Phase 5 adds real validation.
"""

# Valid DDS suffixes (Phase 5 will enforce):
# _d (diffuse), _n (normal), _s (specular), _sp (specular packed — roughness/metallic/AO), _lm (lightmap), _billboard
# TODO Phase 5: validate that every .dds file ends in one of the above suffixes
# NOTE: splat maps are RGBA8 UNORM PNG files — never DDS; they are NOT in scope for the DDS suffix validation.
# _splat suffix: splat maps are RGBA8 UNORM PNG files — NEVER DDS.
# NOTE: _splat.png IS a valid asset suffix (RGBA8 UNORM PNG); _splat.dds is an ERROR.
# The six-suffix DDS validation table (_d, _n, _s, _sp, _lm, _billboard) does NOT include
# _splat — _splat files are PNG only and are validated by a separate PNG-format check.
# TODO Phase 5: hard error if any file named *_splat.dds exists — DXT compression
# corrupts splat map blend weight gradients (smooth 0–255 values become quantized blocks).

# TODO Phase 5+: validate _billboard.dds format
# Mandatory billboard DDS constraints:
# - Fixed resolution: 1024x128 px
# - Mandatory DXT format: DXT5/BC3 (not DXT1)
# - Mandatory sRGB flag: GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT
#   (NOTE: GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT is the GPU UPLOAD internal format —
#   it is NOT a DDS header field. DDS FILE validation uses the DX10 DXGI_FORMAT field
#   (value 78 = BC3_UNORM_SRGB). These are two distinct operations.)
# - Mandatory mip count: exactly 4 levels
# - Must not co-exist with _lod2.b3d for same asset base name
#
# dwMipMapCount is at byte offset 28 in the standard DDS header
# import struct; d=open(f,'rb').read(); mip_count = struct.unpack('<I', d[28:32])[0]
# TODO Phase 5: hard error if mip_count < 4
# TODO Phase 5: hard error if billboard DDS does not contain a DX10 extended header
#   (FourCC at bytes 84-87 != b'DX10') — sRGB colorspace indeterminate without
#   DX10 header; file MUST be rejected
#   If DX10 header is present, DXGI_FORMAT is at bytes 128-131:
#   dxgi_fmt = struct.unpack('<I', d[128:132])[0]
# TODO Phase 5: hard error if _lod2_lm.dds is not DXT5
#   (DXGI_FORMAT_BC3_UNORM = 77 for linear DXT5; _lod2_lm.dds is a lightmap —
#   linear, NOT sRGB; DXGI_FORMAT_BC3_UNORM_SRGB = 78 applies to _billboard.dds only;
#   read DXGI_FORMAT from bytes 128-131)

# _billboard.dds MUST NOT co-exist with _lod2.b3d for the same base name —
# co-existence is a critical asset authoring error (the two asset types are mutually
# exclusive: height_floors <= 3 uses billboard DDS; height_floors >= 4 uses _lod2.b3d).
# TODO Phase 5: hard error if both *_billboard.dds and *_lod2.b3d exist for the same base name.

# CROSS-REFERENCE (ISSUE-F): Any _billboard.dds that passes the floor-count gate
# (height_floors <= 3) MUST ALSO be authored with a DX10 extended header and
# DXGI_FORMAT = BC3_UNORM_SRGB (value 78) per architecture/asset-standards/2d-texture-standards.md
# 'Validating sRGB DDS Output' section. The floor-count check and the DX10 sRGB check
# are independent validations — a billboard file that passes the floor-count check but
# lacks a DX10 extended header is still a hard validation error at Phase 5.
# TODO Phase 5: apply BOTH the floor-count check AND the DX10 DXG_FORMAT sRGB check
# to every _billboard.dds file.

# lod_distances[2] is the CULL distance (not a LOD2 switch-out) — beyond this distance
# the billboard is not rendered. No LOD2 mesh exists for billboard imposters.

# TODO Phase 5+: validate audio formats
# Rules from architecture/audio-architecture/v1-audio-asset-manifest.md:
# 1. music_*.ogg (incl. music_main_menu_*.ogg) / ambient_*.ogg: 44100 Hz stereo (2 channels)
#    NOTE: sfx_zone_*.ogg (zone loops) are EXCLUDED from the stereo check — always mono.
#    NOTE: ambient_*.ogg also require duration check: [90.0s, 120.0s]
#    TODO Phase 5: hard error if ov_time_total < 90.0 OR ov_time_total > 120.0 for ambient_*.ogg
# 2. sfx_vehicle_engine_*.ogg: duration >= 6.0s, mono, 44100 Hz
#    TODO Phase 5: hard error if ov_time_total < 6.0 OR vi->channels != 1 OR vi->rate != 44100
#    PERCEPTUAL NOTE: kVehicleEngineLoopMinDurationSeconds = 6.0s is calibrated against the
#    lowest pitch-shift ratio (0.75x) — perceived loop = 6.0/0.75 = 4.5s; any change to this
#    constant requires sound-artist-opensoftal review before committing.
# 3. sfx_zone_*.ogg: duration <= kZoneLoopMaxPreloadDurationSeconds, mono, 44100 Hz
#    TODO Phase 5: hard error if ov_time_total > kZoneLoopMaxPreloadDurationSeconds OR
#    vi->channels != 1 OR vi->rate != 44100
#    NOTE: kZoneLoopMaxPreloadDurationSeconds = 18.0f (NOT 20.0f) — 18s is the authored hard cap;
#    20s is the pre-load/streaming tier boundary; do NOT conflate these two values.
# 4. stinger_*.wav: mono WAV PCM at 44100 Hz (channels==1 AND sample_rate==44100)
#    TODO Phase 5: hard error if channels != 1 OR sample_rate != 44100
# 5. music_*.ogg (ALL music files): co-located .json sidecar required
#    TODO Phase 5: hard error if any music_*.ogg has no matching <basename>.json sidecar
#    NOTE: The sidecar check pattern is music_*.ogg ONLY — NOT sfx_*.ogg or ambient_*.ogg.
# 6. OGG encoding quality floors: music_*.ogg >= -q 8; ambient_*.ogg >= -q 7; sfx_zone_*.ogg >= -q 6

if __name__ == '__main__':
    print("validate_assets.py: Phase 1 stub — no validation performed.")
    # Phase 5 adds real validation
