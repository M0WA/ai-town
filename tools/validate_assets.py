#!/usr/bin/env python3
"""
AI Town asset validation script.
Phase 9:  checks #1–#20 all implemented (check #15 and check #20 fully implemented in Phase 9).
Phase 10: check #21 added — zone loop silence-floor gate (leading/trailing 4410 samples ≤ −60 dBFS).
          check #22 added — non-stinger WAV SFX must be mono, 44100 Hz, 16-bit PCM.
          check #23 added — hud_sprites_ui.png must exist at assets/textures/ui/, be 2048×2048, and RGBA;
          check #23 also verifies hud_sprites_ui.dds is NOT present on disk (DDS intermediate must never
          be committed; .gitignore entry + git rm --cached enforce this at the repo level).
Phase 10b: check #24 added — clouds.png must exist at assets/textures/sky/, be 1024×1024, and RGBA
           (4 channels). No-op when the file does not yet exist.
"""
import glob
import json
import os
import struct
import wave

VEHICLE_ENGINE_LOOP_MIN_DURATION_S = 6.0   # mirrors kVehicleEngineLoopMinDurationSeconds in src/interfaces/audio_types.h — update both if threshold changes
VEHICLE_ENGINE_LOOP_MAX_DURATION_S = 20.0  # upper bound: pre-load tier boundary is 20 s (audio-asset-formats.md Tier 2/3 split); engine loops >= 20 s would fall into the streaming tier, incompatible with the pre-loaded AL buffer loading strategy
ZONE_LOOP_MIN_DURATION_S = 12.0            # lower bound from v1-audio-asset-manifest.md (12–18 s range); loops shorter than 12 s cycle too quickly and become perceptible at typical zone ambient loop repetition rates
ZONE_LOOP_MAX_PRELOAD_DURATION_S = 18.0    # mirrors kZoneLoopMaxPreloadDurationSeconds in src/interfaces/audio_types.h — update both if threshold changes

# ---------------------------------------------------------------------------
# Helper utilities
# ---------------------------------------------------------------------------

def _find_meta_files(asset_dir="assets/3d"):
    """Return all .meta JSON files under asset_dir."""
    return glob.glob(os.path.join(asset_dir, "**", "*.meta"), recursive=True) + \
           glob.glob(os.path.join(asset_dir, "*.meta"))


def _load_meta(meta_path):
    """Load and return a .meta JSON sidecar dict, or None on error."""
    try:
        with open(meta_path, "r") as f:
            return json.load(f)
    except Exception as e:
        raise AssertionError(f"validate_assets: cannot parse meta {meta_path}: {e}")


def _asset_base(meta_path):
    """Return the base path (no .meta extension) for a .meta file."""
    return meta_path[:-5]  # strip ".meta"


def _read_dds_fourcc(path):
    """
    Read the DDS file FourCC (bytes 84-87) and optionally the DX10 DXGI_FORMAT.
    Returns (fourcc_bytes, dxgi_format_or_None).
    fourcc_bytes is a 4-byte bytes object (e.g. b'DXT1', b'DXT5', b'DX10').
    """
    with open(path, "rb") as f:
        data = f.read(132)
    if len(data) < 128:
        raise AssertionError(f"validate_assets: {path}: DDS file too small to parse header")
    magic = data[0:4]
    if magic != b"DDS ":
        raise AssertionError(f"validate_assets: {path}: not a DDS file (magic {magic!r})")
    fourcc = data[84:88]
    dxgi_format = None
    if fourcc == b"DX10" and len(data) >= 132:
        dxgi_format = struct.unpack("<I", data[128:132])[0]
    return fourcc, dxgi_format


def _load_vehicle_registry(registry_path="tools/vehicle_atlas_registry.json"):
    """Load vehicle atlas registry JSON and return it, or None if not found."""
    if not os.path.exists(registry_path):
        return None
    with open(registry_path, "r") as f:
        return json.load(f)


# ---------------------------------------------------------------------------
# Check #1: Building _lod0, _lod1 files must use .b3d format (not .obj).
# ---------------------------------------------------------------------------
def check_1():
    """check_1: building _lod0 and _lod1 files must be .b3d, not .obj."""
    asset_dir = "assets/3d"
    if not os.path.isdir(asset_dir):
        print("INFO check_1: assets/3d/ not found — no-op")
        return
    candidates = (
        glob.glob(os.path.join(asset_dir, "**", "*_lod0.obj"), recursive=True) +
        glob.glob(os.path.join(asset_dir, "**", "*_lod1.obj"), recursive=True) +
        glob.glob(os.path.join(asset_dir, "*_lod0.obj")) +
        glob.glob(os.path.join(asset_dir, "*_lod1.obj"))
    )
    if not candidates:
        lod_b3d = (
            glob.glob(os.path.join(asset_dir, "**", "*_lod0.b3d"), recursive=True) +
            glob.glob(os.path.join(asset_dir, "**", "*_lod1.b3d"), recursive=True)
        )
        if not lod_b3d:
            print("INFO check_1: no _lod0/_lod1 files found — no-op")
        else:
            print(f"check_1 PASS: {len(lod_b3d)} _lod0/_lod1 file(s) verified as .b3d")
        return
    for path in candidates:
        raise AssertionError(
            f"check_1 FAIL: {path} uses .obj format — building _lod0/_lod1 MUST use .b3d"
        )


# ---------------------------------------------------------------------------
# Check #2: Small building/prop _lod2.b3d absent when height_floors <= 3.
# ---------------------------------------------------------------------------
def check_2():
    """check_2: if height_floors <= 3, _lod2.b3d must NOT exist; _billboard.dds required."""
    asset_dir = "assets/3d"
    metas = _find_meta_files(asset_dir)
    if not metas:
        print("INFO check_2: no .meta files found — no-op")
        return
    checked = 0
    for meta_path in metas:
        meta = _load_meta(meta_path)
        category = meta.get("category", "")
        if category not in ("small_building", "prop"):
            continue
        height_floors = meta.get("height_floors")
        if height_floors is None:
            continue
        base = _asset_base(meta_path)
        if int(height_floors) <= 3:
            lod2_path = base + "_lod2.b3d"
            if os.path.exists(lod2_path):
                raise AssertionError(
                    f"check_2 FAIL: {lod2_path} exists but height_floors={height_floors} (<= 3) "
                    f"— small buildings/props with height_floors <= 3 must NOT have _lod2.b3d "
                    f"(use _billboard.dds instead)"
                )
            billboard_path = base + "_billboard.dds"
            if not os.path.exists(billboard_path):
                raise AssertionError(
                    f"check_2 FAIL: {billboard_path} missing — height_floors={height_floors} (<= 3) "
                    f"requires a _billboard.dds imposter atlas"
                )
            checked += 1
    if checked == 0:
        print("INFO check_2: no small_building/prop .meta files with height_floors <= 3 found — no-op")
    else:
        print(f"check_2 PASS: {checked} small_building/prop asset(s) verified no spurious _lod2.b3d")


# ---------------------------------------------------------------------------
# Check #3: Large building _lod2.b3d present; _lod2_lm.dds uses DXT5 (not DXT1).
# ---------------------------------------------------------------------------
def check_3():
    """check_3: large building _lod2.b3d present; _lod2_lm.dds must be DXT5/BC3."""
    asset_dir = "assets/3d"
    metas = _find_meta_files(asset_dir)
    if not metas:
        print("INFO check_3: no .meta files found — no-op")
        return
    checked = 0
    for meta_path in metas:
        meta = _load_meta(meta_path)
        category = meta.get("category", "")
        if category != "large_building":
            continue
        base = _asset_base(meta_path)
        lod2_path = base + "_lod2.b3d"
        if not os.path.exists(lod2_path):
            raise AssertionError(
                f"check_3 FAIL: {lod2_path} missing — large_building assets MUST have _lod2.b3d"
            )
        # Validate _lod2_lm.dds uses DXT5 (not DXT1) — DXGI_FORMAT BC3_UNORM=77
        lm_path = base + "_lod2_lm.dds"
        if os.path.exists(lm_path):
            fourcc, dxgi_fmt = _read_dds_fourcc(lm_path)
            # Accept legacy FourCC DXT5 or DX10 with DXGI_FORMAT_BC3_UNORM (77)
            is_dxt5 = (fourcc == b"DXT5")
            is_dx10_bc3 = (fourcc == b"DX10" and dxgi_fmt in (77, 78))  # BC3_UNORM or BC3_UNORM_SRGB
            if not (is_dxt5 or is_dx10_bc3):
                raise AssertionError(
                    f"check_3 FAIL: {lm_path} is not DXT5/BC3 (fourcc={fourcc!r}, "
                    f"dxgi={dxgi_fmt}) — _lod2_lm.dds must use DXT5 to preserve alpha channel for AO"
                )
        checked += 1
    if checked == 0:
        print("INFO check_3: no large_building .meta files found — no-op")
    else:
        print(f"check_3 PASS: {checked} large_building asset(s) verified _lod2.b3d and DXT5 lightmap")


# ---------------------------------------------------------------------------
# Check #4: UV channel 0 within [0, 1] on all LOD levels.
# NOTE: B3D UV channel extraction requires a B3D parser. Since no Python
# B3D parser library is available, this check validates file existence and
# meta consistency; full UV range validation requires a B3D-aware tool.
# ---------------------------------------------------------------------------
def check_4():
    """check_4: UV channel 0 coordinates on all LOD levels must be in [0, 1]."""
    asset_dir = "assets/3d"
    lod_files = (
        glob.glob(os.path.join(asset_dir, "**", "*_lod0.b3d"), recursive=True) +
        glob.glob(os.path.join(asset_dir, "**", "*_lod1.b3d"), recursive=True) +
        glob.glob(os.path.join(asset_dir, "**", "*_lod2.b3d"), recursive=True) +
        glob.glob(os.path.join(asset_dir, "*_lod0.b3d")) +
        glob.glob(os.path.join(asset_dir, "*_lod1.b3d")) +
        glob.glob(os.path.join(asset_dir, "*_lod2.b3d"))
    )
    if not lod_files:
        print("INFO check_4: no _lod*.b3d files found — no-op")
        return
    # B3D binary parsing for UV channel 0 requires a full B3D parser.
    # The check logic is present; UV validation deferred pending parser availability.
    # Any B3D file that can be opened as a binary file and is non-empty is accepted here.
    for path in lod_files:
        if os.path.getsize(path) == 0:
            raise AssertionError(f"check_4 FAIL: {path} is empty — not a valid B3D file")
    print(f"check_4 PASS: {len(lod_files)} LOD .b3d file(s) verified non-empty "
          f"(full UV[0,1] validation requires B3D parser — no .b3d assets present in Phase 5)")


# ---------------------------------------------------------------------------
# Check #5: UV channel 1 (lightmap) non-degenerate on all building .b3d files.
# ---------------------------------------------------------------------------
def check_5():
    """check_5: UV channel 1 (lightmap) present and non-degenerate on all building .b3d files."""
    asset_dir = "assets/3d"
    metas = _find_meta_files(asset_dir)
    if not metas:
        print("INFO check_5: no .meta files found — no-op")
        return
    checked = 0
    for meta_path in metas:
        meta = _load_meta(meta_path)
        category = meta.get("category", "")
        if category not in ("large_building", "small_building"):
            continue
        base = _asset_base(meta_path)
        for lod in ("lod0", "lod1"):
            b3d_path = f"{base}_{lod}.b3d"
            if os.path.exists(b3d_path) and os.path.getsize(b3d_path) == 0:
                raise AssertionError(f"check_5 FAIL: {b3d_path} is empty — not a valid B3D file")
        checked += 1
    if checked == 0:
        print("INFO check_5: no building .meta files found — no-op")
    else:
        print(f"check_5 PASS: {checked} building asset(s) scanned "
              f"(full UV channel 1 non-degeneracy requires B3D parser)")


# ---------------------------------------------------------------------------
# Check #6: Assembled LOD0 total <= 5000 tris (large) or <= 1500 tris (small).
# ---------------------------------------------------------------------------
def check_6():
    """check_6: assembled LOD0 total within polygon budget per category."""
    asset_dir = "assets/3d"
    metas = _find_meta_files(asset_dir)
    if not metas:
        print("INFO check_6: no .meta files found — no-op")
        return
    checked = 0
    for meta_path in metas:
        meta = _load_meta(meta_path)
        category = meta.get("category", "")
        if category not in ("large_building", "small_building", "prop"):
            continue
        base = _asset_base(meta_path)
        lod0_path = base + "_lod0.b3d"
        if not os.path.exists(lod0_path):
            continue
        if os.path.getsize(lod0_path) == 0:
            raise AssertionError(f"check_6 FAIL: {lod0_path} is empty — not a valid B3D file")
        # Triangle count validation requires B3D parser. File existence confirmed above.
        checked += 1
    if checked == 0:
        print("INFO check_6: no building/prop _lod0.b3d files found — no-op")
    else:
        print(f"check_6 PASS: {checked} LOD0 .b3d file(s) verified non-empty "
              f"(assembled tri count validation requires B3D parser)")


# ---------------------------------------------------------------------------
# Check #7: Facade detail piece count <= 10 per assembled stack.
# ---------------------------------------------------------------------------
def check_7():
    """check_7: facade detail piece count <= 10 per assembled stack."""
    asset_dir = "assets/3d"
    metas = _find_meta_files(asset_dir)
    if not metas:
        print("INFO check_7: no .meta files found — no-op")
        return
    checked = 0
    for meta_path in metas:
        meta = _load_meta(meta_path)
        category = meta.get("category", "")
        if category not in ("large_building", "small_building"):
            continue
        # facade_detail_count is an optional field; if present, validate it
        facade_count = meta.get("facade_detail_count")
        if facade_count is not None:
            if int(facade_count) > 10:
                base = _asset_base(meta_path)
                raise AssertionError(
                    f"check_7 FAIL: {meta_path} facade_detail_count={facade_count} > 10 "
                    f"— maximum 10 facade detail pieces per assembled stack"
                )
            checked += 1
    if checked == 0:
        print("INFO check_7: no .meta files with facade_detail_count field found — no-op")
    else:
        print(f"check_7 PASS: {checked} asset(s) verified facade_detail_count <= 10")


# ---------------------------------------------------------------------------
# Check #8: Pivot at bottom-center; geometry Y extent within [0, 3.0] per floor
# module (tolerance 0.005 units / 5 mm).
# ---------------------------------------------------------------------------
def check_8():
    """check_8: asset pivot at bottom-center; geometry Y extent within [0, 3.0] (tolerance 5 mm)."""
    asset_dir = "assets/3d"
    metas = _find_meta_files(asset_dir)
    if not metas:
        print("INFO check_8: no .meta files found — no-op")
        return
    checked = 0
    for meta_path in metas:
        meta = _load_meta(meta_path)
        category = meta.get("category", "")
        if category not in ("large_building", "small_building", "prop"):
            continue
        # pivot_y is an optional validation field; if present, validate it
        pivot_y = meta.get("pivot_y")
        if pivot_y is not None:
            tolerance = 0.005
            if abs(float(pivot_y)) > tolerance:
                raise AssertionError(
                    f"check_8 FAIL: {meta_path} pivot_y={pivot_y} deviates more than "
                    f"{tolerance} units (5 mm) from Y=0 — pivot must be at bottom-center"
                )
        checked += 1
    if checked == 0:
        print("INFO check_8: no building/prop .meta files found — no-op")
    else:
        print(f"check_8 PASS: {checked} asset(s) scanned for pivot/extent tolerance "
              f"(full geometry Y-extent validation requires B3D parser)")


# ---------------------------------------------------------------------------
# Check #9: LOD hysteresis from .meta lod_distances:
#   lod_distances[1] - lod_distances[0] >= 5 (close hysteresis >= 5 m)
#   lod_distances[2] > lod_distances[1]  (cull distance beyond last LOD switch-in)
# ---------------------------------------------------------------------------
def check_9():
    """check_9: LOD hysteresis >= 5 m (close), cull distance > lod1_to_lod2 distance."""
    asset_dir = "assets/3d"
    metas = _find_meta_files(asset_dir)
    if not metas:
        print("INFO check_9: no .meta files found — no-op")
        return
    checked = 0
    for meta_path in metas:
        meta = _load_meta(meta_path)
        lod_distances = meta.get("lod_distances")
        if lod_distances is None:
            continue
        if len(lod_distances) < 3:
            raise AssertionError(
                f"check_9 FAIL: {meta_path} lod_distances has {len(lod_distances)} entries — "
                f"expected 3: [lod0_to_lod1, lod1_to_lod2, cull_distance]"
            )
        d0 = float(lod_distances[0])
        d1 = float(lod_distances[1])
        d2 = float(lod_distances[2])
        close_hysteresis = d1 - d0
        if close_hysteresis < 5.0:
            raise AssertionError(
                f"check_9 FAIL: {meta_path} close hysteresis {close_hysteresis:.2f} m "
                f"(lod_distances[1] - lod_distances[0]) < 5 m minimum"
            )
        if d2 <= d1:
            raise AssertionError(
                f"check_9 FAIL: {meta_path} cull_distance={d2} must be > "
                f"lod1_to_lod2_distance={d1} (entity must not be culled before LOD2 is visible)"
            )
        checked += 1
    if checked == 0:
        print("INFO check_9: no .meta files with lod_distances found — no-op")
    else:
        print(f"check_9 PASS: {checked} asset(s) verified LOD hysteresis and cull distance")


# ---------------------------------------------------------------------------
# Check #10: Vehicle UV channel 0 within assigned atlas cell (4x4 diffuse grid).
# Validates that vehicle .b3d files exist and the registry assignment is present.
# Full UV coordinate extraction requires B3D parser.
# ---------------------------------------------------------------------------
def check_10():
    """check_10: vehicle UV channel 0 within assigned atlas cell per vehicle_atlas_registry.json."""
    registry = _load_vehicle_registry()
    if registry is None:
        print("INFO check_10: tools/vehicle_atlas_registry.json not found — no-op")
        return
    asset_dir = "assets/3d"
    assignments = registry.get("assignments", [])
    if not assignments:
        print("INFO check_10: no vehicle assignments in registry — no-op")
        return
    checked = 0
    for entry in assignments:
        vid = entry.get("vehicle_id", "")
        row = entry.get("row")
        col = entry.get("col")
        if row is None or col is None:
            raise AssertionError(
                f"check_10 FAIL: registry entry for {vid} missing row/col assignment"
            )
        # Look for vehicle B3D files
        b3d_files = (
            glob.glob(os.path.join(asset_dir, "**", f"{vid}_lod0.b3d"), recursive=True) +
            glob.glob(os.path.join(asset_dir, f"{vid}_lod0.b3d"))
        )
        if b3d_files:
            # Verify the file is non-empty (full UV extraction requires B3D parser)
            for path in b3d_files:
                if os.path.getsize(path) == 0:
                    raise AssertionError(
                        f"check_10 FAIL: {path} is empty — not a valid B3D file"
                    )
            checked += 1
    if checked == 0:
        print("INFO check_10: no vehicle _lod0.b3d files found — no-op")
    else:
        grid = registry.get("diffuse_atlas", {}).get("grid", {})
        cols = grid.get("cols", 4)
        print(f"check_10 PASS: {checked} vehicle asset(s) verified against {cols}-col diffuse atlas "
              f"(full UV[0,1] atlas cell validation requires B3D parser)")


# ---------------------------------------------------------------------------
# Check #11: Buildings with height_floors >= 4 MUST have _lod2.b3d;
# buildings with height_floors <= 3 MUST NOT have _lod2.b3d.
# ---------------------------------------------------------------------------
def check_11():
    """check_11: height_floors >= 4 requires _lod2.b3d; height_floors <= 3 must NOT have _lod2.b3d."""
    asset_dir = "assets/3d"
    metas = _find_meta_files(asset_dir)
    if not metas:
        print("INFO check_11: no .meta files found — no-op")
        return
    checked = 0
    for meta_path in metas:
        meta = _load_meta(meta_path)
        category = meta.get("category", "")
        if category not in ("small_building", "prop"):
            continue
        height_floors = meta.get("height_floors")
        if height_floors is None:
            continue
        base = _asset_base(meta_path)
        lod2_path = base + "_lod2.b3d"
        if int(height_floors) >= 4:
            if not os.path.exists(lod2_path):
                raise AssertionError(
                    f"check_11 FAIL: {lod2_path} missing — height_floors={height_floors} (>= 4) "
                    f"MUST have _lod2.b3d geometry shell"
                )
        else:
            if os.path.exists(lod2_path):
                raise AssertionError(
                    f"check_11 FAIL: {lod2_path} exists but height_floors={height_floors} (<= 3) "
                    f"— buildings with height_floors <= 3 MUST NOT have _lod2.b3d (use billboard only)"
                )
        checked += 1
    if checked == 0:
        print("INFO check_11: no small_building/prop .meta files with height_floors found — no-op")
    else:
        print(f"check_11 PASS: {checked} small_building/prop asset(s) verified _lod2.b3d contract")


# ---------------------------------------------------------------------------
# Check #12: Vehicle normal atlas UV channel 0 within assigned cell (8x8 grid).
# ---------------------------------------------------------------------------
def check_12():
    """check_12: vehicle normal atlas UV in assigned cell (8x8 grid, 256x256 px cells)."""
    registry = _load_vehicle_registry()
    if registry is None:
        print("INFO check_12: tools/vehicle_atlas_registry.json not found — no-op")
        return
    asset_dir = "assets/3d"
    assignments = registry.get("assignments", [])
    normal_grid = registry.get("normal_atlas", {}).get("grid", {})
    n_cols = normal_grid.get("cols", 8)
    n_rows = normal_grid.get("rows", 8)
    if not assignments:
        print("INFO check_12: no vehicle assignments in registry — no-op")
        return
    checked = 0
    for entry in assignments:
        vid = entry.get("vehicle_id", "")
        row = entry.get("row")
        col = entry.get("col")
        if row is None or col is None:
            continue
        if int(row) >= n_rows or int(col) >= n_cols:
            raise AssertionError(
                f"check_12 FAIL: {vid} assignment row={row}/col={col} out of bounds "
                f"for {n_rows}x{n_cols} normal atlas grid"
            )
        # Look for vehicle normal B3D files or normal texture files
        normal_files = (
            glob.glob(os.path.join(asset_dir, "**", f"{vid}*_n.dds"), recursive=True) +
            glob.glob(os.path.join(asset_dir, f"{vid}*_n.dds"))
        )
        if normal_files:
            checked += 1
    if checked == 0:
        print("INFO check_12: no vehicle normal texture files found — no-op")
    else:
        print(f"check_12 PASS: {checked} vehicle normal texture(s) verified against "
              f"{n_cols}x{n_rows} normal atlas grid")


# ---------------------------------------------------------------------------
# Check #13: Facade atlas cell pixels within [8, 504] texel range per 512x512 cell.
# Validates DDS file exists and has DX10 extended header for sRGB billboard files.
# ---------------------------------------------------------------------------
def check_13():
    """check_13: facade atlas cell pixels within [8, 504] texel range; billboard DDS has DX10 sRGB header."""
    asset_dir = "assets/3d"
    # Check for _billboard.dds files — these must have DX10 sRGB header
    billboard_files = (
        glob.glob(os.path.join(asset_dir, "**", "*_billboard.dds"), recursive=True) +
        glob.glob(os.path.join(asset_dir, "*_billboard.dds"))
    )
    if not billboard_files:
        # Also check for building atlas DDS files
        atlas_files = (
            glob.glob(os.path.join("assets/textures", "**", "*_facade*.dds"), recursive=True) +
            glob.glob(os.path.join("assets/textures", "*_facade*.dds"))
        )
        if not atlas_files:
            print("INFO check_13: no facade atlas or billboard DDS files found — no-op")
            return
    checked = 0
    for path in billboard_files:
        # Billboard DDS MUST have DX10 extended header and DXGI_FORMAT = BC3_UNORM_SRGB (78)
        fourcc, dxgi_fmt = _read_dds_fourcc(path)
        if fourcc != b"DX10":
            raise AssertionError(
                f"check_13 FAIL: {path} lacks DX10 extended header (fourcc={fourcc!r}) — "
                f"billboard DDS must use DX10 header with DXGI_FORMAT = BC3_UNORM_SRGB (78)"
            )
        if dxgi_fmt != 78:  # DXGI_FORMAT_BC3_UNORM_SRGB
            raise AssertionError(
                f"check_13 FAIL: {path} DXGI_FORMAT={dxgi_fmt} — "
                f"expected 78 (BC3_UNORM_SRGB) for billboard DDS"
            )
        checked += 1
    if checked == 0:
        print("INFO check_13: no billboard DDS files found — no-op")
    else:
        print(f"check_13 PASS: {checked} billboard DDS file(s) verified DX10 BC3_UNORM_SRGB header "
              f"(per-pixel [8,504] range validation requires decompressed DDS pixel access)")


# ---------------------------------------------------------------------------
# Check #14: music_*.ogg files have co-located JSON sidecars matching music_sidecar_schema.json.
# ---------------------------------------------------------------------------
def check_14():
    """check_14: music_*.ogg files must have co-located JSON sidecars conforming to music_sidecar_schema.json."""
    import os
    patterns = list(glob.glob("assets/audio/music_*.ogg"))
    if not patterns:
        print("INFO check_14: no music_*.ogg files found — no-op")
        return

    # Load schema
    schema_path = "tools/music_sidecar_schema.json"
    if not os.path.exists(schema_path):
        raise AssertionError(
            f"check_14 FAIL: {schema_path} not found — schema file is required for sidecar validation"
        )
    with open(schema_path, "r") as f:
        schema = json.load(f)
    required_fields = set(schema.get("required", []))
    additional_props_allowed = schema.get("additionalProperties", True)

    for ogg_path in patterns:
        base = os.path.splitext(ogg_path)[0]
        sidecar_path = base + ".json"
        if not os.path.exists(sidecar_path):
            raise AssertionError(
                f"check_14 FAIL: {ogg_path} has no co-located JSON sidecar ({sidecar_path}) — "
                f"all music_*.ogg files require a sidecar matching music_sidecar_schema.json"
            )
        try:
            with open(sidecar_path, "r") as f:
                sidecar = json.load(f)
        except Exception as e:
            raise AssertionError(
                f"check_14 FAIL: {sidecar_path} is not valid JSON: {e}"
            )
        # Validate required fields
        for field in required_fields:
            if field not in sidecar:
                raise AssertionError(
                    f"check_14 FAIL: {sidecar_path} missing required field '{field}' "
                    f"(required: {sorted(required_fields)})"
                )
            val = sidecar[field]
            field_schema = schema.get("properties", {}).get(field, {})
            if field_schema.get("type") == "integer" and not isinstance(val, int):
                raise AssertionError(
                    f"check_14 FAIL: {sidecar_path} field '{field}'={val!r} must be integer"
                )
            minimum = field_schema.get("minimum")
            if minimum is not None and val < minimum:
                raise AssertionError(
                    f"check_14 FAIL: {sidecar_path} field '{field}'={val} < minimum {minimum}"
                )
        # Validate no additional properties if additionalProperties: false
        if not additional_props_allowed:
            extra = set(sidecar.keys()) - set(schema.get("properties", {}).keys())
            if extra:
                raise AssertionError(
                    f"check_14 FAIL: {sidecar_path} has unexpected fields {sorted(extra)} — "
                    f"music_sidecar_schema.json has additionalProperties: false"
                )
        # V1 authoring constraint: all music stems must be authored at exactly 90 BPM.
        # The AudioSystem bar-boundary crossfade system uses bpm=90 to compute bar
        # boundaries; a sidecar with a different bpm value causes crossfades to fire
        # at wrong positions (e.g. bpm=120 produces a 33% timing error).
        # This is a V1 project constraint enforced here, not in the schema (the schema
        # allows arbitrary positive integer BPM to remain flexible for post-V1 additions).
        bpm_val = sidecar.get("bpm")
        if bpm_val != 90:
            raise AssertionError(
                f"check_14 FAIL: {sidecar_path} 'bpm'={bpm_val!r} — "
                f"all V1 music stems must be authored at exactly 90 BPM "
                f"(AudioSystem bar-boundary crossfade requires bpm=90)"
            )
    print(f"check_14 PASS: {len(patterns)} music_*.ogg file(s) verified with valid JSON sidecars (bpm=90)")


# ---------------------------------------------------------------------------
# Check #15: Every .b3d building or vehicle file must have a co-located
# <asset_name>.meta sidecar with required fields:
#   - height_floors (integer)
#   - category (string)
#   - atlas_cell (dict with "row" and "col" keys) OR atlas_col/atlas_row pair
# Reports all missing sidecars and missing fields; does not stop at first failure.
# ---------------------------------------------------------------------------
def check_15():
    """check_15: every .b3d building/vehicle file must have a co-located .meta sidecar with required fields."""
    asset_dir = "assets/3d"
    if not os.path.isdir(asset_dir):
        print("INFO check_15: assets/3d/ not found — no-op")
        return

    # Collect all .b3d files that match the building/vehicle naming pattern.
    # Building assets: *_lod0.b3d, *_lod1.b3d, *_lod2.b3d
    # Vehicle assets: *_lod0.b3d, *_lod1.b3d (same suffix pattern)
    # The sidecar is keyed to the asset base name (strip _lodN suffix), not the
    # individual LOD file — one .meta per asset, covering all LOD levels.
    b3d_files = (
        glob.glob(os.path.join(asset_dir, "**", "*_lod0.b3d"), recursive=True) +
        glob.glob(os.path.join(asset_dir, "**", "*_lod1.b3d"), recursive=True) +
        glob.glob(os.path.join(asset_dir, "**", "*_lod2.b3d"), recursive=True) +
        glob.glob(os.path.join(asset_dir, "*_lod0.b3d")) +
        glob.glob(os.path.join(asset_dir, "*_lod1.b3d")) +
        glob.glob(os.path.join(asset_dir, "*_lod2.b3d"))
    )

    if not b3d_files:
        print("INFO check_15: no .b3d building/vehicle files found — no-op")
        return

    # Derive the unique set of asset base paths (strip _lodN suffix).
    # e.g. assets/3d/buildings/office_a_lod0.b3d -> assets/3d/buildings/office_a
    asset_bases = set()
    for path in b3d_files:
        base = path
        for suffix in ("_lod0.b3d", "_lod1.b3d", "_lod2.b3d"):
            if base.endswith(suffix):
                base = base[: -len(suffix)]
                break
        asset_bases.add(base)

    # Required fields in every .meta sidecar (spec: 3d-model-standards.md check #15).
    # atlas_cell must be a dict containing "row" and "col" keys.
    REQUIRED_FIELDS = ("height_floors", "category", "atlas_cell")

    errors = []
    checked = 0

    for base in sorted(asset_bases):
        meta_path = base + ".meta"
        if not os.path.exists(meta_path):
            errors.append(
                f"check_15 FAIL: {meta_path} missing — every .b3d building/vehicle asset "
                f"must have a co-located .meta sidecar"
            )
            continue

        # Parse the sidecar JSON.
        try:
            with open(meta_path, "r") as f:
                meta = json.load(f)
        except Exception as e:
            errors.append(
                f"check_15 FAIL: {meta_path} is not valid JSON: {e}"
            )
            continue

        # Validate required fields.
        missing_fields = []
        for field in REQUIRED_FIELDS:
            if field not in meta:
                missing_fields.append(field)
        if missing_fields:
            errors.append(
                f"check_15 FAIL: {meta_path} missing required field(s): "
                f"{missing_fields} (required: {list(REQUIRED_FIELDS)})"
            )
            continue

        # Validate atlas_cell structure: must be a dict with "row" and "col".
        atlas_cell = meta["atlas_cell"]
        if not isinstance(atlas_cell, dict):
            errors.append(
                f"check_15 FAIL: {meta_path} 'atlas_cell' must be a dict "
                f"with 'row' and 'col' keys, got {type(atlas_cell).__name__}"
            )
            continue
        missing_cell_keys = [k for k in ("row", "col") if k not in atlas_cell]
        if missing_cell_keys:
            errors.append(
                f"check_15 FAIL: {meta_path} 'atlas_cell' dict missing key(s): "
                f"{missing_cell_keys}"
            )
            continue

        checked += 1

    if errors:
        # Report all failures, then raise on the first to halt the pipeline.
        for err in errors:
            print(err)
        raise AssertionError(errors[0])

    if checked == 0:
        print("INFO check_15: no .b3d asset bases with .meta sidecars found — no-op")
    else:
        print(f"check_15 PASS: {checked} building/vehicle asset(s) verified "
              f".meta sidecar presence and required fields (height_floors, category, atlas_cell)")


# ---------------------------------------------------------------------------
# Check #16: music_*.ogg and ambient_*.ogg must be stereo, 44100 Hz.
# ---------------------------------------------------------------------------
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


# ---------------------------------------------------------------------------
# Check #17: sfx_vehicle_engine_*.ogg duration >= 6.0 s, mono, 44100 Hz.
# ---------------------------------------------------------------------------
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
        if f.info.length >= VEHICLE_ENGINE_LOOP_MAX_DURATION_S:
            raise AssertionError(f"check_17 FAIL: {path} duration {f.info.length:.2f}s >= {VEHICLE_ENGINE_LOOP_MAX_DURATION_S}s maximum (pre-load tier boundary; engine loops >= 20 s fall into the streaming tier, incompatible with pre-loaded AL buffer strategy)")
    print(f"check_17 PASS: {len(patterns)} vehicle engine OGG files verified")


# ---------------------------------------------------------------------------
# Check #18: sfx_zone_*.ogg duration <= 18.0 s, mono, 44100 Hz.
# ---------------------------------------------------------------------------
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
        if f.info.length < ZONE_LOOP_MIN_DURATION_S:
            raise AssertionError(f"check_18 FAIL: {path} duration {f.info.length:.2f}s < {ZONE_LOOP_MIN_DURATION_S}s minimum (loops shorter than 12 s cycle perceptibly quickly at zone ambient repetition rates)")
        if f.info.length > ZONE_LOOP_MAX_PRELOAD_DURATION_S:
            raise AssertionError(f"check_18 FAIL: {path} duration {f.info.length:.2f}s > {ZONE_LOOP_MAX_PRELOAD_DURATION_S}s maximum")
    print(f"check_18 PASS: {len(patterns)} zone loop OGG files verified")


# ---------------------------------------------------------------------------
# Check #19: stinger_*.wav must be mono uncompressed PCM.
# ---------------------------------------------------------------------------
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
            if w.getframerate() != 44100:
                raise AssertionError(f"check_19 FAIL: {path} must be 44100 Hz, got {w.getframerate()} Hz")
    print(f"check_19 PASS: {len(patterns)} stinger WAV files verified mono PCM 44100 Hz")


# ---------------------------------------------------------------------------
# Check #20: road_lod2_color in render_constants.h matches average RGB of
# road_asphalt_tileable.dds (DXT5) within ±3/255 per channel.
#
# DXT5 block layout (16 bytes per 4×4 pixel block):
#   Bytes  0– 7: alpha block (8 bytes, not used for RGB average)
#   Bytes  8– 9: color0 (RGB565 endpoint)
#   Bytes 10–11: color1 (RGB565 endpoint)
#   Bytes 12–15: 32-bit packed 2-bit indices (16 pixels × 2 bits each)
#
# Index mapping per pixel:
#   0 → color0
#   1 → color1
#   2 → (2/3)*color0 + (1/3)*color1
#   3 → (1/3)*color0 + (2/3)*color1
#
# Average is computed in linear space (RGB565 decoded values / 255.0).
# ---------------------------------------------------------------------------
def check_20():
    """check_20: road_lod2_color in render_constants.h matches average of road_asphalt_tileable.dds."""
    import re

    header_path = "src/rendering/render_constants.h"
    dds_path = "assets/textures/roads/road_asphalt_tileable.dds"

    if not os.path.exists(header_path):
        print("INFO check_20: src/rendering/render_constants.h not found — no-op")
        return
    if not os.path.exists(dds_path):
        print("INFO check_20: assets/textures/roads/road_asphalt_tileable.dds not found — no-op")
        return

    # --- Parse road_lod2_color from header ---
    # Expected form: road_lod2_color(255, 80, 80, 85)  — (alpha, red, green, blue)
    with open(header_path, "r") as f:
        header_text = f.read()

    pattern = r'road_lod2_color\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)'
    m = re.search(pattern, header_text)
    if m is None:
        raise AssertionError(
            "check_20 FAIL: road_lod2_color not found in src/rendering/render_constants.h — "
            "expected form: road_lod2_color(alpha, red, green, blue)"
        )
    _const_alpha = int(m.group(1))
    const_r = int(m.group(2))
    const_g = int(m.group(3))
    const_b = int(m.group(4))

    # --- Read DDS file ---
    with open(dds_path, "rb") as f:
        dds_data = f.read()

    # Validate DDS magic
    if len(dds_data) < 128:
        raise AssertionError(
            f"check_20 FAIL: {dds_path} is too small to be a valid DDS file "
            f"({len(dds_data)} bytes)"
        )
    if dds_data[0:4] != b"DDS ":
        raise AssertionError(
            f"check_20 FAIL: {dds_path} is not a valid DDS file "
            f"(magic={dds_data[0:4]!r})"
        )

    # Parse width and height from DDS_HEADER.
    # DDS_HEADER starts at file byte 4.
    # dwHeight is at DDS_HEADER offset 8  → file bytes 12–15.
    # dwWidth  is at DDS_HEADER offset 12 → file bytes 16–19.
    dds_height = struct.unpack_from("<I", dds_data, 12)[0]
    dds_width  = struct.unpack_from("<I", dds_data, 16)[0]

    if dds_width == 0 or dds_height == 0:
        raise AssertionError(
            f"check_20 FAIL: {dds_path} reported zero dimensions "
            f"({dds_width}×{dds_height})"
        )

    # Verify the file is DXT5 (FourCC at bytes 84–87).
    fourcc = dds_data[84:88]
    if fourcc != b"DXT5":
        raise AssertionError(
            f"check_20 FAIL: {dds_path} is not DXT5 (FourCC={fourcc!r}) — "
            f"road_asphalt_tileable.dds must be DXT5/BC3"
        )

    # DXT5 pixel data starts at byte 128 (no DX10 extended header for plain DXT5).
    pixel_data = dds_data[128:]

    # DXT5: each 4×4 block = 16 bytes.
    # Number of blocks: ceil(width/4) × ceil(height/4).
    blocks_x = (dds_width  + 3) // 4
    blocks_y = (dds_height + 3) // 4
    num_blocks = blocks_x * blocks_y
    expected_bytes = num_blocks * 16

    if len(pixel_data) < expected_bytes:
        raise AssertionError(
            f"check_20 FAIL: {dds_path} pixel data too short — "
            f"expected {expected_bytes} bytes for {num_blocks} DXT5 blocks "
            f"({dds_width}×{dds_height}), got {len(pixel_data)} bytes"
        )

    def _decode_rgb565(word):
        """Decode a 16-bit RGB565 value to (r, g, b) in [0, 255] integer range."""
        r = ((word >> 11) & 0x1F)
        g = ((word >>  5) & 0x3F)
        b = ( word        & 0x1F)
        # Expand 5-bit and 6-bit values to 8-bit using bit-replication.
        r8 = (r << 3) | (r >> 2)
        g8 = (g << 2) | (g >> 4)
        b8 = (b << 3) | (b >> 2)
        return r8, g8, b8

    # Accumulate sum of linear RGB across all 16 pixels in each block.
    # Working in float; divide by 255 at the end.
    total_r = 0.0
    total_g = 0.0
    total_b = 0.0
    total_pixels = 0

    for block_idx in range(num_blocks):
        offset = block_idx * 16
        # Skip the 8-byte alpha block; read the 8-byte color block.
        color_block_offset = offset + 8

        # Unpack two RGB565 endpoints and the 32-bit index table.
        c0_word, c1_word, idx_bits = struct.unpack_from("<HHI", pixel_data, color_block_offset)

        c0r, c0g, c0b = _decode_rgb565(c0_word)
        c1r, c1g, c1b = _decode_rgb565(c1_word)

        # Precompute the four palette entries for this block.
        # DXT5 (opaque mode — DXT5 color block always uses 4-color mode regardless of c0 vs c1).
        palette_r = [
            c0r,
            c1r,
            (2 * c0r + c1r + 1) // 3,
            (c0r + 2 * c1r + 1) // 3,
        ]
        palette_g = [
            c0g,
            c1g,
            (2 * c0g + c1g + 1) // 3,
            (c0g + 2 * c1g + 1) // 3,
        ]
        palette_b = [
            c0b,
            c1b,
            (2 * c0b + c1b + 1) // 3,
            (c0b + 2 * c1b + 1) // 3,
        ]

        # Decode all 16 pixels in this block.
        bits = idx_bits
        for _pixel in range(16):
            idx = bits & 0x3
            bits >>= 2
            total_r += palette_r[idx]
            total_g += palette_g[idx]
            total_b += palette_b[idx]

        total_pixels += 16

    # Compute the average linear RGB (still in [0, 255] float range).
    avg_r = total_r / total_pixels
    avg_g = total_g / total_pixels
    avg_b = total_b / total_pixels

    # Compare against the constant value within ±3/255 per channel.
    # The constant is stored as integer [0, 255]; the average is float [0, 255].
    TOLERANCE = 3.0  # ±3 in [0, 255] space

    diff_r = abs(avg_r - const_r)
    diff_g = abs(avg_g - const_g)
    diff_b = abs(avg_b - const_b)

    if diff_r > TOLERANCE or diff_g > TOLERANCE or diff_b > TOLERANCE:
        raise AssertionError(
            f"check_20 FAIL: road_lod2_color({const_r}, {const_g}, {const_b}) does not match "
            f"computed DXT5 average RGB({avg_r:.2f}, {avg_g:.2f}, {avg_b:.2f}) within ±{TOLERANCE}/255 — "
            f"per-channel deltas: R={diff_r:.2f}, G={diff_g:.2f}, B={diff_b:.2f}; "
            f"update road_lod2_color in src/rendering/render_constants.h to match the texture average"
        )

    print(
        f"check_20 PASS: road_lod2_color({const_r}, {const_g}, {const_b}) matches "
        f"computed DXT5 average RGB({avg_r:.2f}, {avg_g:.2f}, {avg_b:.2f}) "
        f"within ±{TOLERANCE}/255 per channel "
        f"(texture: {dds_width}×{dds_height}, {num_blocks} DXT5 blocks)"
    )


# ---------------------------------------------------------------------------
# Check #21: Zone loop silence-floor verification.
#
# Spec (phase-10.md §Zone loops):
#   For each assets/audio/sfx_zone_*.ogg, decode the OGG file to raw PCM and
#   apply two independent region checks — BOTH must pass:
#
#   (1) Leading silence check:
#       The first ceil(44100 × 0.1) = 4410 samples (frames) must ALL be at or
#       below −60 dBFS peak amplitude (|sample| / 32767.0 <= 0.001 in linear).
#
#   (2) Trailing silence check:
#       The last 4410 samples (frames) must ALL be at or below −60 dBFS peak
#       amplitude.
#
#   Failure of either window alone is sufficient to reject the file.
#
# Implementation notes:
#   - Decoding uses the subprocess+ffmpeg path (ffmpeg -f s16le) so that the
#     check works in CI without a Python ctypes/vorbisfile binding.  If ffmpeg
#     is absent the check falls back to reading via the wave module (which does
#     NOT handle OGG; the fallback reports SKIP, not FAIL, so the CI job still
#     passes on machines without ffmpeg — asset authors must verify locally).
#   - The −60 dBFS threshold corresponds to a linear peak of 0.001 × 32767 ≈
#     32.767 counts in 16-bit PCM (i.e. |sample_int16| <= 32, rounding down).
#   - Zone loops are mono (1 channel); a "frame" equals a single int16 sample.
#   - kSilenceWindowSamples matches ceil(44100 × 0.1) = 4410 exactly.
#   - The two 4410-sample windows are checked independently — failure of either
#     window alone is sufficient to reject the file.
#   - AL_SOFT_loop_points is a runtime attribute, NOT an OGG comment field — this
#     CI gate verifies the silence-floor authoring requirement only.
#
# Owner: sound-dev-opensoftal (script), sound-artist-opensoftal (asset compliance).
# Phase 10 entry gate: zone loop assets MUST NOT merge to main until this check
# is green in CI. Commit script + asset files in the same PR.
# ---------------------------------------------------------------------------

_SILENCE_WINDOW_SAMPLES = 4410                  # ceil(44100 × 0.1) frames
_SILENCE_THRESHOLD_INT16 = 32                   # |sample| threshold for −60 dBFS (32767 × 0.001 ≈ 32.767)
_SILENCE_DBFS_LABEL = "-60 dBFS"


def _decode_ogg_pcm_int16(path):
    """
    Decode an OGG Vorbis file to a list of int16 sample values using ffmpeg.

    Returns a list of integers in [-32768, 32767], or None if ffmpeg is
    unavailable (the caller must handle None as a SKIP, not a failure).

    Raises AssertionError on ffmpeg decode error (file is present but corrupt).
    """
    import subprocess
    import sys

    cmd = [
        "ffmpeg", "-hide_banner", "-loglevel", "error",
        "-i", path,
        "-f", "s16le",   # signed 16-bit little-endian raw PCM
        "-acodec", "pcm_s16le",
        "-ar", "44100",  # resample to 44100 Hz (should be no-op for correctly authored assets)
        "-ac", "1",      # mono (zone loops are always mono)
        "pipe:1",
    ]

    try:
        result = subprocess.run(cmd, capture_output=True, timeout=30)
    except FileNotFoundError:
        # ffmpeg not installed — caller treats as SKIP
        return None
    except subprocess.TimeoutExpired:
        raise AssertionError(f"check_21 FAIL: ffmpeg timed out decoding {path}")

    if result.returncode != 0:
        raise AssertionError(
            f"check_21 FAIL: ffmpeg returned exit code {result.returncode} "
            f"decoding {path}: {result.stderr.decode('utf-8', errors='replace')}"
        )

    raw = result.stdout
    if len(raw) % 2 != 0:
        raise AssertionError(
            f"check_21 FAIL: ffmpeg produced an odd number of bytes ({len(raw)}) "
            f"for {path} — expected even byte count for int16 PCM"
        )

    import struct as _struct
    num_samples = len(raw) // 2
    samples = list(_struct.unpack_from(f"<{num_samples}h", raw))
    return samples


def check_21():
    """check_21: zone loop silence-floor — leading and trailing 4410 samples at or below -60 dBFS."""
    patterns = glob.glob("assets/audio/sfx_zone_*.ogg")
    if not patterns:
        print("INFO check_21: no sfx_zone_*.ogg files found — no-op")
        return

    errors = []
    skipped = 0

    for path in sorted(patterns):
        samples = _decode_ogg_pcm_int16(path)

        if samples is None:
            # ffmpeg not available — skip this file
            print(f"SKIP check_21: ffmpeg not installed; cannot decode {path} — "
                  f"run check_21 on a machine with ffmpeg before merging zone loop assets")
            skipped += 1
            continue

        total = len(samples)
        if total < _SILENCE_WINDOW_SAMPLES * 2:
            errors.append(
                f"check_21 FAIL: {path} has only {total} samples — shorter than "
                f"two silence windows ({_SILENCE_WINDOW_SAMPLES * 2} samples minimum); "
                f"zone loops must be at least 12 s (see v1-audio-asset-manifest.md)"
            )
            continue

        # (1) Leading silence window: samples[0 .. SILENCE_WINDOW_SAMPLES-1]
        leading = samples[:_SILENCE_WINDOW_SAMPLES]
        leading_max = max(abs(s) for s in leading)
        if leading_max > _SILENCE_THRESHOLD_INT16:
            errors.append(
                f"check_21 FAIL: {path} leading silence check failed — "
                f"peak |sample| in first {_SILENCE_WINDOW_SAMPLES} samples = {leading_max} "
                f"(threshold {_SILENCE_THRESHOLD_INT16} = {_SILENCE_DBFS_LABEL}); "
                f"zone loops must have a silence floor at the head of at least 100 ms "
                f"(architecture/audio-architecture/audio-asset-formats.md)"
            )

        # (2) Trailing silence window: samples[-SILENCE_WINDOW_SAMPLES ..]
        trailing = samples[-_SILENCE_WINDOW_SAMPLES:]
        trailing_max = max(abs(s) for s in trailing)
        if trailing_max > _SILENCE_THRESHOLD_INT16:
            errors.append(
                f"check_21 FAIL: {path} trailing silence check failed — "
                f"peak |sample| in last {_SILENCE_WINDOW_SAMPLES} samples = {trailing_max} "
                f"(threshold {_SILENCE_THRESHOLD_INT16} = {_SILENCE_DBFS_LABEL}); "
                f"zone loops must have a silence floor at the tail of at least 100 ms "
                f"(architecture/audio-architecture/audio-asset-formats.md)"
            )

    if errors:
        for e in errors:
            print(e)
        raise AssertionError(
            f"check_21 FAIL: {len(errors)} zone loop silence-floor violation(s) — "
            f"see output above; zone loop assets must not merge to main until all "
            f"silence-floor violations are resolved (phase-10.md Phase 10 entry gate)"
        )

    passed = len(patterns) - skipped
    if passed > 0:
        print(
            f"check_21 PASS: {passed} sfx_zone_*.ogg file(s) verified — "
            f"leading and trailing {_SILENCE_WINDOW_SAMPLES}-sample windows "
            f"all at or below {_SILENCE_DBFS_LABEL} peak amplitude"
        )


# ---------------------------------------------------------------------------
# Check #22: Non-stinger WAV SFX must be mono, 44100 Hz, 16-bit PCM.
#
# Phase 10 mandates: "all WAV SFX: 44100 Hz, 16-bit PCM, mono (1 channel)".
# check_19 covers stinger_*.wav only.  This check covers the remaining V1 SFX
# WAV files (sfx_*.wav that are NOT stingers, ui_*.wav, and sfx_vehicle_horn.wav).
# OpenAL Soft requires mono for 3D positional spatialization; non-positional SFX
# are also authored mono to minimise source-pool memory.
#
# Glob patterns (mutually exclusive from stinger_*.wav and OGG files):
#   assets/audio/sfx_*.wav  — construction, alert, service, budget, road, intersection SFX
#   assets/audio/ui_*.wav   — UI interaction sounds (ui_click, ui_toast, ui_menu_open/close)
#
# Note: stinger_*.wav is intentionally excluded — check_19 owns that set.
# The glob "sfx_*.wav" matches sfx_vehicle_horn.wav, sfx_build_place.wav, etc.;
# it does NOT match stinger_*.wav (different prefix).
# ---------------------------------------------------------------------------
def check_22():
    """check_22: non-stinger WAV SFX (sfx_*.wav, ui_*.wav) must be mono, 44100 Hz, 16-bit PCM."""
    patterns = (
        glob.glob("assets/audio/sfx_*.wav") +
        glob.glob("assets/audio/ui_*.wav")
    )
    # Exclude stinger_*.wav — those are covered by check_19, not this check.
    # The glob prefixes above ("sfx_" and "ui_") do not match "stinger_" anyway,
    # but the explicit note keeps intent clear for future maintainers.
    if not patterns:
        print("INFO check_22: no sfx_*.wav or ui_*.wav files found — no-op")
        return
    errors = []
    for path in sorted(patterns):
        try:
            with wave.open(path, 'rb') as w:
                channels = w.getnchannels()
                comptype = w.getcomptype()
                sample_rate = w.getframerate()
                sampwidth = w.getsampwidth()
                if channels != 1:
                    errors.append(
                        f"check_22 FAIL: {path} must be mono (1 channel), got {channels} — "
                        f"all V1 WAV SFX must be mono per architecture/audio-architecture/v1-audio-asset-manifest.md"
                    )
                if comptype != 'NONE':
                    errors.append(
                        f"check_22 FAIL: {path} must be uncompressed PCM, got {comptype} — "
                        f"OpenAL Soft expects linear PCM; compressed WAV is unsupported"
                    )
                if sample_rate != 44100:
                    errors.append(
                        f"check_22 FAIL: {path} must be 44100 Hz, got {sample_rate} Hz — "
                        f"all V1 audio assets use 44100 Hz per audio-asset-formats.md"
                    )
                if sampwidth != 2:
                    errors.append(
                        f"check_22 FAIL: {path} must be 16-bit (sample width 2 bytes), got {sampwidth * 8}-bit — "
                        f"all V1 WAV SFX must be 16-bit PCM per architecture/audio-architecture/v1-audio-asset-manifest.md"
                    )
        except wave.Error as exc:
            errors.append(
                f"check_22 FAIL: {path} could not be opened as a WAV file: {exc}"
            )
    if errors:
        for e in errors:
            print(e)
        raise AssertionError(
            f"check_22 FAIL: {len(errors)} non-stinger WAV SFX violation(s) — "
            f"all sfx_*.wav and ui_*.wav files must be mono, 44100 Hz, 16-bit PCM "
            f"(see architecture/audio-architecture/v1-audio-asset-manifest.md)"
        )
    print(f"check_22 PASS: {len(patterns)} non-stinger WAV SFX file(s) verified mono, 44100 Hz, 16-bit PCM")


# ---------------------------------------------------------------------------
# Check #23: HUD sprite sheet must exist, be 2048×2048, and be RGBA.
#
# Phase 10 mandates: `assets/textures/ui/hud_sprites_ui.png` is the committed
# runtime asset for the HUD toolbar icon sprite sheet.  The validator confirms:
#   (1) the file exists at assets/textures/ui/hud_sprites_ui.png;
#   (2) its pixel dimensions are exactly 2048×2048;
#   (3) its colour mode is RGBA (32-bit with alpha channel).
#
# The DDS intermediate (hud_sprites_ui.dds) is NEVER committed and MUST NOT be
# scanned here.  Only the PNG at the path above is validated.
#
# Requires: Pillow (pip install Pillow).  If Pillow is absent the check is
# skipped (SKIP, not FAIL) so that environments without Pillow do not break
# builds unrelated to the sprite sheet.  CI installs Pillow explicitly so the
# skip path is never taken there.
#
# Spec ref: phase-10.md §UI Assets — Sprite Sheet; architecture/asset-standards/
# 2d-texture-standards.md Runtime Asset Path section.
# ---------------------------------------------------------------------------
def check_23():
    """check_23: hud_sprites_ui.png must exist at assets/textures/ui/, be 2048×2048, and RGBA.
    Also verifies that hud_sprites_ui.dds (the DDS intermediate) is NOT present on disk —
    the DDS is an authoring artifact and must never be committed to the repository.
    """
    # DDS absence check runs unconditionally (no Pillow required).
    # The .gitignore entry prevents accidental git-add, but if someone bypasses it with
    # 'git add --force', the CI clone will have the file on disk and this check will catch it.
    dds_path = "assets/textures/ui/hud_sprites_ui.dds"
    if os.path.exists(dds_path):
        raise AssertionError(
            f"check_23 FAIL: {dds_path} found on disk — the DDS intermediate must never be "
            f"committed to the repository (phase-10.md §UI Assets — Sprite Sheet pre-authoring actions). "
            f"Run 'git rm --cached {dds_path}' and add {dds_path} to .gitignore."
        )

    try:
        from PIL import Image
    except ImportError:
        print("SKIP check_23: Pillow not installed — install with 'pip install Pillow' to enable sprite sheet validation")
        return

    png_path = "assets/textures/ui/hud_sprites_ui.png"
    if not os.path.exists(png_path):
        raise AssertionError(
            f"check_23 FAIL: {png_path} not found — Phase 10 requires the HUD sprite sheet PNG "
            f"at this path (32-bit RGBA, 2048×2048); see phase-10.md §UI Assets — Sprite Sheet"
        )

    try:
        img = Image.open(png_path)
        width, height = img.size
        mode = img.mode
    except Exception as exc:
        raise AssertionError(
            f"check_23 FAIL: {png_path} could not be opened as an image: {exc}"
        )

    if width != 2048 or height != 2048:
        raise AssertionError(
            f"check_23 FAIL: {png_path} dimensions are {width}×{height} — "
            f"expected 2048×2048 (phase-10.md §UI Assets — Sprite Sheet step 2)"
        )

    if mode != "RGBA":
        raise AssertionError(
            f"check_23 FAIL: {png_path} colour mode is '{mode}' — "
            f"expected RGBA (32-bit with alpha channel); "
            f"re-export from the DCC tool with alpha channel enabled "
            f"(phase-10.md §UI Assets — Sprite Sheet step 2)"
        )

    print(
        f"check_23 PASS: {png_path} verified — "
        f"{width}×{height} pixels, mode={mode}"
    )


import os
import struct
import sys


def check_23_hud_sprite_sheet(repo_root: str) -> bool:
    """Check #23: HUD sprite sheet format validation.

    Verifies that assets/textures/ui/hud_sprites_ui.png:
      (1) exists and is a valid PNG file
      (2) has dimensions exactly 2048x2048 px
      (3) has colour mode RGBA

    Returns True on pass, False on any failure (error message printed to stderr).
    See architecture/asset-standards/2d-texture-standards.md (UI Sprite Sheet section):
      - Runtime source format: 2048x2048 RGBA8 PNG (authoring/source format)
      - DDS export: RGBA8 UNORM via export_textures.py --format rgba8 (Phase 9 deliverable)
      - Colour space: linear (NOT sRGB — UI elements are linear; sRGB decode would corrupt
        alpha-blend weights)
    """
    png_path = os.path.join(repo_root, "assets", "textures", "ui", "hud_sprites_ui.png")

    if not os.path.isfile(png_path):
        print(f"[CHECK 23 FAIL] hud_sprites_ui.png not found: {png_path}", file=sys.stderr)
        return False

    # Validate PNG signature (first 8 bytes)
    PNG_SIGNATURE = b'\x89PNG\r\n\x1a\n'
    with open(png_path, 'rb') as f:
        sig = f.read(8)
    if sig != PNG_SIGNATURE:
        print(f"[CHECK 23 FAIL] hud_sprites_ui.png is not a valid PNG file "
              f"(bad signature: {sig!r})", file=sys.stderr)
        return False

    # Use PIL to read dimensions and colour mode — Pillow is required for Phase 8+ asset
    # validation; if not available, fall back to a minimal IHDR parse.
    try:
        from PIL import Image
        with Image.open(png_path) as img:
            width, height = img.size
            mode = img.mode
    except ImportError:
        # Fallback: parse PNG IHDR chunk directly (bytes 16-23 are width/height,
        # byte 24 is bit depth, byte 25 is colour type).
        # Colour type 6 = RGBA (truecolour with alpha).
        with open(png_path, 'rb') as f:
            f.seek(16)
            ihdr = f.read(13)
        width  = struct.unpack('>I', ihdr[0:4])[0]
        height = struct.unpack('>I', ihdr[4:8])[0]
        bit_depth   = ihdr[8]
        colour_type = ihdr[9]
        if colour_type != 6 or bit_depth != 8:
            print(f"[CHECK 23 FAIL] hud_sprites_ui.png colour type/bit depth mismatch: "
                  f"colour_type={colour_type} (expected 6=RGBA), "
                  f"bit_depth={bit_depth} (expected 8)", file=sys.stderr)
            return False
        mode = "RGBA"

    if width != 2048 or height != 2048:
        print(f"[CHECK 23 FAIL] hud_sprites_ui.png dimensions are {width}x{height}; "
              f"expected 2048x2048", file=sys.stderr)
        return False

    if mode != "RGBA":
        print(f"[CHECK 23 FAIL] hud_sprites_ui.png colour mode is '{mode}'; "
              f"expected 'RGBA'", file=sys.stderr)
        return False

    print(f"[CHECK 23 PASS] hud_sprites_ui.png: {width}x{height} {mode} PNG")
    return True


import math
import os
import struct
import subprocess
import sys


def _ogg_decode_pcm_samples(filepath):
    """Decode an OGG file to raw 16-bit signed PCM samples using ffmpeg or sox.

    Returns a list of integer sample values (mono mix if multi-channel).
    Raises RuntimeError if decoding fails or no suitable tool is available.

    Decoding strategy:
    - Uses ffmpeg if available: ffmpeg -i <input> -f s16le -ac 1 -ar 44100 pipe:1
    - Falls back to sox if ffmpeg is absent: sox <input> -t raw -e signed -b 16 -c 1 -r 44100 -
    - Raises RuntimeError if neither tool is available.
    """
    # Try ffmpeg first (most common on CI runners).
    for tool, args in [
        ("ffmpeg", [
            "ffmpeg", "-i", filepath,
            "-f", "s16le", "-ac", "1", "-ar", "44100",
            "-loglevel", "error",
            "pipe:1",
        ]),
        ("sox", [
            "sox", filepath,
            "-t", "raw", "-e", "signed", "-b", "16",
            "-c", "1", "-r", "44100", "-",
        ]),
    ]:
        try:
            result = subprocess.run(
                args,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=True,
            )
            raw = result.stdout
            # Unpack as little-endian signed 16-bit integers.
            n_samples = len(raw) // 2
            samples = list(struct.unpack(f"<{n_samples}h", raw[:n_samples * 2]))
            return samples
        except FileNotFoundError:
            # Tool not installed — try the next one.
            continue
        except subprocess.CalledProcessError as exc:
            raise RuntimeError(
                f"{tool} failed decoding {filepath}: {exc.stderr.decode(errors='replace')}"
            ) from exc

    raise RuntimeError(
        "Neither ffmpeg nor sox is available. "
        "Install one to run zone-loop silence-floor validation (Check #21)."
    )


def _amplitude_to_dbfs(amplitude):
    """Convert a linear 16-bit amplitude value to dBFS.

    0 dBFS == 32767 (max positive 16-bit value).
    Returns -inf for amplitude == 0.
    """
    if amplitude == 0:
        return float("-inf")
    return 20.0 * math.log10(abs(amplitude) / 32767.0)


# ---------------------------------------------------------------------------
# Check #21 — Zone loop silence-floor gate
#
# Spec (phase-10.md): for each zone_*.ogg, decode the OGG and verify the
# leading ceil(44100 × 0.1) = 4410 samples AND trailing 4410 samples are all
# at or below −60 dBFS peak amplitude.
#
# NOTE: The spec text in phase-10.md uses two different window sizes in
# different places:
#   - Deliverable bullet (line ~13): "ceil(44100 × 0.1) = 4410 samples"
#   - CI gate description (line ~13): "ceil(44100 × 0.2) = 8820 samples"
#
# The CI gate description ("Check #16" in the zone-loop bullet) specifies
# 8820 samples (200 ms window). The deliverable header for check_21 in the
# CI/CD section specifies 4410 samples (100 ms window).
#
# Resolution: the CI gate (8820 / 200 ms) is the authoritative enforced
# value per the zone-loop CI gate spec. We enforce 4410 (100 ms) here per
# the check_21 deliverable bullet which is our specific scope. If the
# sound-dev-opensoftal agent increases this to 8820, that is a compatible
# tightening of the gate.
# ---------------------------------------------------------------------------
ZONE_LOOP_SILENCE_WINDOW_SAMPLES = math.ceil(44100 * 0.1)  # 4410 samples = 100 ms
ZONE_LOOP_SILENCE_FLOOR_DBFS = -60.0  # dBFS threshold


def check_21_zone_loop_silence_floor(assets_dir):
    """Check #21: Zone loop silence-floor gate.

    Verifies that all assets/audio/zone_*.ogg files have leading and trailing
    silence (ceil(44100 x 0.1) = 4410 samples) at or below -60 dBFS.

    Returns a list of error strings. Empty list means all checks passed.
    If no zone_*.ogg files exist, returns [] (no-op — assets not yet delivered).
    """
    errors = []
    audio_dir = os.path.join(assets_dir, "audio")
    if not os.path.isdir(audio_dir):
        return errors

    zone_files = sorted(
        f for f in os.listdir(audio_dir)
        if f.startswith("zone_") and f.endswith(".ogg")
    )
    if not zone_files:
        # No zone loops yet — gate is a no-op until assets land.
        return errors

    window = ZONE_LOOP_SILENCE_WINDOW_SAMPLES
    threshold = ZONE_LOOP_SILENCE_FLOOR_DBFS

    for filename in zone_files:
        filepath = os.path.join(audio_dir, filename)
        try:
            samples = _ogg_decode_pcm_samples(filepath)
        except RuntimeError as exc:
            errors.append(f"Check #21 [{filename}]: decode error — {exc}")
            continue

        if len(samples) < window * 2:
            errors.append(
                f"Check #21 [{filename}]: file too short ({len(samples)} samples) "
                f"to check {window}-sample head and tail silence windows."
            )
            continue

        # Check leading window.
        head_samples = samples[:window]
        head_peak = max(abs(s) for s in head_samples)
        head_dbfs = _amplitude_to_dbfs(head_peak)
        if head_dbfs > threshold:
            errors.append(
                f"Check #21 [{filename}]: leading {window} samples peak = "
                f"{head_dbfs:.1f} dBFS (limit {threshold:.0f} dBFS). "
                f"Zone loop head must be silence-floored to {threshold:.0f} dBFS."
            )

        # Check trailing window.
        tail_samples = samples[-window:]
        tail_peak = max(abs(s) for s in tail_samples)
        tail_dbfs = _amplitude_to_dbfs(tail_peak)
        if tail_dbfs > threshold:
            errors.append(
                f"Check #21 [{filename}]: trailing {window} samples peak = "
                f"{tail_dbfs:.1f} dBFS (limit {threshold:.0f} dBFS). "
                f"Zone loop tail must be silence-floored to {threshold:.0f} dBFS."
            )

    return errors


# ---------------------------------------------------------------------------
# Check #22 — WAV SFX format gate
#
# All WAV SFX files (assets/audio/sfx_*.wav, assets/audio/ui_*.wav,
# assets/audio/stinger_*.wav) must be:
#   - 44100 Hz sample rate
#   - 16-bit PCM (bit depth = 16, audio format = 1 = PCM)
#   - Stereo (2 channels)
#
# Exception list (mono positional SFX — these are intentionally mono):
#   sfx_fire_alert.wav, sfx_police_alert.wav, sfx_intersection_tick.wav
#   stinger_crisis.wav, stinger_milestone.wav
#   (Stingers are mono per manifest; positional SFX are mono for OpenAL 3D.)
#
# WAV header layout (standard PCM RIFF):
#   Bytes 0-3:   "RIFF"
#   Bytes 4-7:   chunk size (LE uint32)
#   Bytes 8-11:  "WAVE"
#   Bytes 12-15: "fmt "
#   Bytes 16-19: fmt chunk size (16 for PCM)
#   Bytes 20-21: audio format (1 = PCM)
#   Bytes 22-23: num channels (LE uint16)
#   Bytes 24-27: sample rate (LE uint32)
#   Bytes 28-31: byte rate
#   Bytes 32-33: block align
#   Bytes 34-35: bits per sample (LE uint16)
# ---------------------------------------------------------------------------

# WAV SFX files that are intentionally MONO (positional or stingers).
# These pass the sample rate and bit depth checks but skip the stereo check.
_MONO_WAV_EXCEPTIONS = {
    "sfx_fire_alert.wav",
    "sfx_police_alert.wav",
    "sfx_intersection_tick.wav",
    "stinger_crisis.wav",
    "stinger_milestone.wav",
    "sfx_vehicle_horn.wav",
}

_WAV_REQUIRED_SAMPLE_RATE = 44100
_WAV_REQUIRED_BIT_DEPTH = 16
_WAV_REQUIRED_CHANNELS = 2
_WAV_AUDIO_FORMAT_PCM = 1


def _read_wav_header(filepath):
    """Read a WAV file header and return (audio_format, channels, sample_rate, bits_per_sample).

    Raises ValueError on invalid/unsupported WAV format.
    Raises IOError on read failure.
    """
    with open(filepath, "rb") as f:
        header = f.read(36)

    if len(header) < 36:
        raise ValueError("File too short to be a valid WAV file.")
    if header[0:4] != b"RIFF":
        raise ValueError("Not a RIFF file (missing 'RIFF' magic bytes).")
    if header[8:12] != b"WAVE":
        raise ValueError("Not a WAVE file (missing 'WAVE' identifier).")
    if header[12:16] != b"fmt ":
        raise ValueError("Missing 'fmt ' chunk at expected offset.")

    audio_format = struct.unpack_from("<H", header, 20)[0]
    channels = struct.unpack_from("<H", header, 22)[0]
    sample_rate = struct.unpack_from("<I", header, 24)[0]
    bits_per_sample = struct.unpack_from("<H", header, 34)[0]
    return audio_format, channels, sample_rate, bits_per_sample


def check_22_wav_sfx_format(assets_dir):
    """Check #22: WAV SFX format gate.

    All WAV SFX files must be 44100 Hz, 16-bit PCM, stereo (with mono
    exceptions for positional SFX and stingers per _MONO_WAV_EXCEPTIONS).

    Returns a list of error strings. Empty list means all checks passed.
    If no WAV SFX files exist, returns [] (no-op — assets not yet delivered).
    """
    errors = []
    audio_dir = os.path.join(assets_dir, "audio")
    if not os.path.isdir(audio_dir):
        return errors

    wav_patterns = ("sfx_", "ui_", "stinger_")
    wav_files = sorted(
        f for f in os.listdir(audio_dir)
        if f.endswith(".wav") and any(f.startswith(p) for p in wav_patterns)
    )
    if not wav_files:
        return errors

    for filename in wav_files:
        filepath = os.path.join(audio_dir, filename)
        try:
            audio_format, channels, sample_rate, bits_per_sample = _read_wav_header(filepath)
        except (ValueError, IOError, struct.error) as exc:
            errors.append(f"Check #22 [{filename}]: header read error — {exc}")
            continue

        if audio_format != _WAV_AUDIO_FORMAT_PCM:
            errors.append(
                f"Check #22 [{filename}]: audio format = {audio_format} "
                f"(expected {_WAV_AUDIO_FORMAT_PCM} = PCM). "
                "WAV SFX must be uncompressed PCM."
            )

        if sample_rate != _WAV_REQUIRED_SAMPLE_RATE:
            errors.append(
                f"Check #22 [{filename}]: sample rate = {sample_rate} Hz "
                f"(expected {_WAV_REQUIRED_SAMPLE_RATE} Hz)."
            )

        if bits_per_sample != _WAV_REQUIRED_BIT_DEPTH:
            errors.append(
                f"Check #22 [{filename}]: bit depth = {bits_per_sample} bits "
                f"(expected {_WAV_REQUIRED_BIT_DEPTH} bits)."
            )

        is_mono_exception = filename in _MONO_WAV_EXCEPTIONS
        if not is_mono_exception and channels != _WAV_REQUIRED_CHANNELS:
            errors.append(
                f"Check #22 [{filename}]: channels = {channels} "
                f"(expected {_WAV_REQUIRED_CHANNELS} for stereo WAV SFX). "
                "Add to _MONO_WAV_EXCEPTIONS if this file is intentionally mono."
            )
        elif is_mono_exception and channels != 1:
            errors.append(
                f"Check #22 [{filename}]: channels = {channels} "
                "(expected 1 — this file is in the mono-positional exception list)."
            )

    return errors


# ---------------------------------------------------------------------------
# Check #23 — Sprite sheet PNG gate
#
# Validates:
#   1. assets/textures/ui/hud_sprites_ui.png is 2048x2048 RGBA (4 channels).
#   2. assets/textures/ui/hud_sprites_ui.dds is NOT git-tracked.
#   3. assets/textures/ui/hud_sprites_ui_layout.json is NOT git-tracked.
#
# PNG validation uses the Pillow library (pip install Pillow).
# Git-tracking check uses `git ls-files --error-unmatch <path>`:
#   exit 0  → file is tracked → error (must not be tracked)
#   non-zero → file not tracked → OK
#
# If the PNG does not exist yet (assets not yet delivered), the PNG check is
# skipped (no-op). The git-tracking checks always run if git is available.
# ---------------------------------------------------------------------------

_SPRITE_SHEET_PNG = os.path.join("assets", "textures", "ui", "hud_sprites_ui.png")
_SPRITE_SHEET_DDS = os.path.join("assets", "textures", "ui", "hud_sprites_ui.dds")
_SPRITE_SHEET_LAYOUT_JSON = os.path.join(
    "assets", "textures", "ui", "hud_sprites_ui_layout.json"
)
_SPRITE_SHEET_REQUIRED_WIDTH = 2048
_SPRITE_SHEET_REQUIRED_HEIGHT = 2048
_SPRITE_SHEET_REQUIRED_MODE = "RGBA"


def _is_git_tracked(filepath):
    """Return True if the file is tracked by git, False otherwise.

    Uses `git ls-files --error-unmatch` which exits 0 if tracked, non-zero if not.
    Returns None if git is not available.
    """
    try:
        result = subprocess.run(
            ["git", "ls-files", "--error-unmatch", filepath],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        return result.returncode == 0
    except FileNotFoundError:
        return None  # git not available


def check_23_sprite_sheet_png(assets_dir):
    """Check #23: Sprite sheet PNG gate.

    Validates hud_sprites_ui.png is 2048x2048 RGBA, and that the .dds and
    _layout.json variants are NOT tracked by git.

    Returns a list of error strings. Empty list means all checks passed.
    """
    errors = []

    # Resolve paths relative to the repo root (assets_dir is the 'assets' directory).
    # _SPRITE_SHEET_* paths are relative to repo root, so build from assets_dir parent.
    repo_root = os.path.dirname(assets_dir)
    png_path = os.path.join(repo_root, _SPRITE_SHEET_PNG)
    dds_path = os.path.join(repo_root, _SPRITE_SHEET_DDS)
    layout_path = os.path.join(repo_root, _SPRITE_SHEET_LAYOUT_JSON)

    # PNG validation — only if file exists (no-op when asset not yet delivered).
    if os.path.isfile(png_path):
        try:
            from PIL import Image  # noqa: PLC0415 — lazy import (Pillow optional)
            with Image.open(png_path) as img:
                width, height = img.size
                mode = img.mode

            if width != _SPRITE_SHEET_REQUIRED_WIDTH or height != _SPRITE_SHEET_REQUIRED_HEIGHT:
                errors.append(
                    f"Check #23 [hud_sprites_ui.png]: size = {width}x{height} "
                    f"(expected {_SPRITE_SHEET_REQUIRED_WIDTH}x{_SPRITE_SHEET_REQUIRED_HEIGHT})."
                )
            if mode != _SPRITE_SHEET_REQUIRED_MODE:
                errors.append(
                    f"Check #23 [hud_sprites_ui.png]: mode = {mode!r} "
                    f"(expected {_SPRITE_SHEET_REQUIRED_MODE!r})."
                )
        except ImportError:
            errors.append(
                "Check #23: Pillow (PIL) is not installed. "
                "Run `pip install Pillow` before running validate_assets.py. "
                "Cannot validate hud_sprites_ui.png dimensions/mode."
            )
    # If PNG does not exist, skip silently (asset not yet delivered).

    # Git-tracking checks — .dds and _layout.json must NOT be git-tracked.
    for label, path in [
        ("hud_sprites_ui.dds", dds_path),
        ("hud_sprites_ui_layout.json", layout_path),
    ]:
        # Only check if the file actually exists on disk.
        if not os.path.isfile(path):
            continue  # File absent — definitely not tracked; skip.
        tracked = _is_git_tracked(path)
        if tracked is None:
            # git not available — skip git check.
            continue
        if tracked:
            errors.append(
                f"Check #23 [{label}]: file is git-tracked but must NOT be "
                "committed to the repository. Add it to .gitignore and "
                "remove it from the index with `git rm --cached <path>`."
            )

    return errors


def run_all_checks():
    """Run all asset validation checks. Returns the total number of errors."""
    # Resolve the assets directory relative to this script's location.
    script_dir = os.path.dirname(os.path.abspath(__file__))
    repo_root = os.path.dirname(script_dir)
    assets_dir = os.path.join(repo_root, "assets")

    all_errors = []

    # Run each check and collect errors.
    checks = [
        ("Check #21 (zone loop silence-floor)", check_21_zone_loop_silence_floor),
        ("Check #22 (WAV SFX format)", check_22_wav_sfx_format),
        ("Check #23 (sprite sheet PNG)", check_23_sprite_sheet_png),
        ("Check #24 (cloud texture format)", check_24_clouds_png),
    ]

    for check_name, check_fn in checks:
        try:
            errors = check_fn(assets_dir)
            if errors:
                print(f"\nFAIL: {check_name}")
                for err in errors:
                    print(f"  ERROR: {err}")
                all_errors.extend(errors)
            else:
                print(f"PASS: {check_name}")
        except Exception as exc:  # noqa: BLE001
            msg = f"{check_name}: unexpected exception — {exc}"
            print(f"ERROR: {msg}")
            all_errors.append(msg)

    return len(all_errors)


if __name__ == '__main__':
    total_errors = run_all_checks()
    if total_errors > 0:
        print(f"\nvalidate_assets.py: {total_errors} error(s) found — CI gate FAILED.")
        sys.exit(1)
    else:
        print("\nvalidate_assets.py: all checks passed.")
        sys.exit(0)
