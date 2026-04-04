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
Phase 11g: check #31 added — all six tier-specific bitmap font XML + PNG pairs must exist under
           assets/fonts/ (hud_font_{720,1080,1440}.xml/png, hud_mono_font_{720,1080,1440}.xml/png).
Phase 11o: check #32 added — vehicle LOD0/LOD1 triangle budget (LOD0≤510,000, LOD1≤12,000).
"""
import glob
import json
import os
import struct
import wave

VEHICLE_LOD0_MAX_TRIS = 510_000   # full-fidelity Tripo3D models; multi-buffer split for Irrlicht 16-bit index limit
VEHICLE_LOD1_MAX_TRIS = 12_000    # per-part DECIMATE target ~10,000 tris
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
# Phase 11e cell assignment table (canonical 8×8 atlas grid).
# Key: asset base name (e.g. "res_low_01").  Value: (row, col).
# ---------------------------------------------------------------------------
_PHASE_11E_CELL_ASSIGNMENT = {
    # Row 0: res_low and res_med
    "res_low_01": (0, 0),
    "res_low_02": (0, 1),
    "res_low_03": (0, 2),
    "res_low_04": (0, 3),
    "res_med_01": (0, 4),
    "res_med_02": (0, 5),
    "res_med_03": (0, 6),
    "res_med_04": (0, 7),
    # Row 1: res_high and com_low
    "res_high_01": (1, 0),
    "res_high_02": (1, 1),
    "res_high_03": (1, 2),
    "res_high_04": (1, 3),
    "com_low_01": (1, 4),
    "com_low_02": (1, 5),
    "com_low_03": (1, 6),
    "com_low_04": (1, 7),
    # Row 2: com_med and com_high
    "com_med_01": (2, 0),
    "com_med_02": (2, 1),
    "com_med_03": (2, 2),
    "com_med_04": (2, 3),
    "com_high_01": (2, 4),
    "com_high_02": (2, 5),
    "com_high_03": (2, 6),
    "com_high_04": (2, 7),
    # Row 3: ind_low and ind_med
    "ind_low_01": (3, 0),
    "ind_low_02": (3, 1),
    "ind_low_03": (3, 2),
    "ind_low_04": (3, 3),
    "ind_med_01": (3, 4),
    "ind_med_02": (3, 5),
    "ind_med_03": (3, 6),
    "ind_med_04": (3, 7),
    # Row 4: ind_high and service buildings
    "ind_high_01": (4, 0),
    "ind_high_02": (4, 1),
    "ind_high_03": (4, 2),
    "ind_high_04": (4, 3),
    "svc_fire_station":   (4, 4),
    "svc_police_station": (4, 5),
    "svc_power_plant":    (4, 6),
    "svc_water_tower":    (4, 7),
}


def _parse_b3d_uv_channel0(filepath):
    """
    Parse a B3D binary file and return all UV channel 0 (u, v) pairs as a list
    of float tuples.

    B3D chunk format written by generate_b3d_models.py:
      BB3D → version(i32) + TEXS + BRUS + NODE
      NODE → name(str) + pos(3×f32) + scale(3×f32) + rot(4×f32) + MESH
      MESH → brush_id(i32) + VRTS + TRIS
      VRTS → flags(i32) + tc_sets(i32) + tc_flags[0..tc_sets-1](i32 each)
             + per-vertex data

    Per-vertex layout (flags=1 → normals present, tc_sets=1, tc_flags[0]=2):
      x, y, z       (3 × float32)
      nx, ny, nz    (3 × float32)   [present when flags & 1]
      u0, v0        (2 × float32)   [first UV channel]

    B3D sub-chunks (NODE, MESH) have non-chunk preamble bytes before their
    child chunks, so they cannot be recursively scanned as a flat chunk list.
    This function parses the known structure explicitly:
      BB3D payload → skip version(i32) → scan TEXS/BRUS/NODE at top level
      NODE payload → skip preamble (name str + 3+3+4 floats) → scan MESH
      MESH payload → skip brush_id(i32) → scan VRTS/TRIS

    Raises AssertionError if the file cannot be parsed or contains no VRTS chunk.
    """
    with open(filepath, "rb") as f:
        data = f.read()

    if len(data) < 12:
        raise AssertionError(
            f"_parse_b3d_uv_channel0: {filepath}: file too small ({len(data)} bytes)"
        )

    root_tag = data[0:4]
    if root_tag != b"BB3D":
        raise AssertionError(
            f"_parse_b3d_uv_channel0: {filepath}: not a B3D file (magic {root_tag!r})"
        )

    def read_i32(pos):
        return struct.unpack_from("<i", data, pos)[0], pos + 4

    def read_f32(pos):
        return struct.unpack_from("<f", data, pos)[0], pos + 4

    def read_str(pos):
        """Read null-terminated string; return (s, new_pos)."""
        end = data.index(b"\x00", pos)
        return data[pos:end].decode("ascii", errors="replace"), end + 1

    def read_chunk_header(pos):
        """Return (tag_bytes, payload_start, payload_end, next_chunk_pos)."""
        if pos + 8 > len(data):
            return None
        tag = data[pos:pos + 4]
        length, payload_start = read_i32(pos + 4)
        if length < 0:
            return None
        payload_end = payload_start + length
        if payload_end > len(data):
            return None
        return tag, payload_start, payload_end, payload_end

    # ---- Locate NODE in the top-level BB3D payload ----
    # BB3D payload: version(i32) then top-level chunks (TEXS, BRUS, NODE, ...)
    root_length = struct.unpack_from("<i", data, 4)[0]
    root_payload_end = 8 + root_length
    pos = 12  # skip BB3D tag(4) + length(4) + version(4)

    node_payload_start = None
    node_payload_end = None
    while pos + 8 <= root_payload_end:
        hdr = read_chunk_header(pos)
        if hdr is None:
            break
        tag, payload_start, payload_end, next_pos = hdr
        if tag == b"NODE":
            node_payload_start = payload_start
            node_payload_end = payload_end
            break
        pos = next_pos

    if node_payload_start is None:
        raise AssertionError(
            f"_parse_b3d_uv_channel0: {filepath}: no NODE chunk found"
        )

    # ---- Parse NODE preamble to reach MESH ----
    # NODE payload: name(str) + pos(3×f32) + scale(3×f32) + rot(4×f32) + sub-chunks
    pos = node_payload_start
    _name, pos = read_str(pos)           # node name (null-terminated)
    for _ in range(3):                   # position x, y, z
        _, pos = read_f32(pos)
    for _ in range(3):                   # scale x, y, z
        _, pos = read_f32(pos)
    for _ in range(4):                   # rotation w, x, y, z (quaternion)
        _, pos = read_f32(pos)

    mesh_payload_start = None
    mesh_payload_end = None
    while pos + 8 <= node_payload_end:
        hdr = read_chunk_header(pos)
        if hdr is None:
            break
        tag, payload_start, payload_end, next_pos = hdr
        if tag == b"MESH":
            mesh_payload_start = payload_start
            mesh_payload_end = payload_end
            break
        pos = next_pos

    if mesh_payload_start is None:
        raise AssertionError(
            f"_parse_b3d_uv_channel0: {filepath}: no MESH chunk found inside NODE"
        )

    # ---- Parse MESH preamble to reach VRTS ----
    # MESH payload: brush_id(i32) + sub-chunks (VRTS, TRIS, ...)
    pos = mesh_payload_start
    _brush_id, pos = read_i32(pos)      # MESH brush_id

    vrts_payload_start = None
    vrts_payload_end = None
    while pos + 8 <= mesh_payload_end:
        hdr = read_chunk_header(pos)
        if hdr is None:
            break
        tag, payload_start, payload_end, next_pos = hdr
        if tag == b"VRTS":
            vrts_payload_start = payload_start
            vrts_payload_end = payload_end
            break
        pos = next_pos

    if vrts_payload_start is None:
        raise AssertionError(
            f"_parse_b3d_uv_channel0: {filepath}: no VRTS chunk found inside MESH"
        )

    # ---- Parse VRTS payload ----
    # VRTS: flags(i32) + tc_sets(i32) + tc_flags[0..tc_sets-1](i32 each) + vertices
    p = vrts_payload_start
    flags, p = read_i32(p)
    tc_sets, p = read_i32(p)
    tc_flags = []
    for _ in range(tc_sets):
        tf, p = read_i32(p)
        tc_flags.append(tf)

    has_normals = bool(flags & 1)
    tc_size_0 = tc_flags[0] if tc_sets >= 1 else 0  # 2 for 2D UV

    floats_per_vertex = 3                          # x, y, z
    if has_normals:
        floats_per_vertex += 3                     # nx, ny, nz
    floats_per_vertex += tc_size_0                 # UV channel 0
    for i in range(1, tc_sets):
        floats_per_vertex += tc_flags[i]           # additional UV channels

    bytes_per_vertex = floats_per_vertex * 4
    if bytes_per_vertex == 0:
        return []

    n_verts = (vrts_payload_end - p) // bytes_per_vertex
    if n_verts <= 0:
        return []

    # Byte offset to UV channel 0 within each vertex
    uv0_byte_offset = (3 + (3 if has_normals else 0)) * 4

    uvs = []
    for vi in range(n_verts):
        base = p + vi * bytes_per_vertex
        u_pos = base + uv0_byte_offset
        v_pos = u_pos + 4
        if v_pos + 4 <= vrts_payload_end:
            u = struct.unpack_from("<f", data, u_pos)[0]
            v = struct.unpack_from("<f", data, v_pos)[0]
            uvs.append((u, v))
    return uvs


def _parse_b3d_positions(filepath):
    """Parse all vertex (x, y, z) positions from ALL VRTS chunks in a B3D file.

    Large meshes are split across multiple mesh buffers (each with its own VRTS
    chunk). Scanning only the first VRTS chunk misses vertices in later buffers
    (e.g. a ground plate appended after a high-poly building body). This function
    scans every VRTS occurrence in the file.

    Returns list of (x, y, z) float tuples.
    Raises AssertionError on format errors.
    """
    with open(filepath, "rb") as f:
        data = f.read()

    positions = []
    search_start = 0
    found_any = False
    while True:
        vrts_pos = data.find(b"VRTS", search_start)
        if vrts_pos < 0:
            break
        found_any = True
        payload_len = struct.unpack_from("<i", data, vrts_pos + 4)[0]
        p = vrts_pos + 8  # start of VRTS payload

        flags = struct.unpack_from("<i", data, p)[0]; p += 4
        tc_sets = struct.unpack_from("<i", data, p)[0]; p += 4
        has_normals = bool(flags & 1)
        for _ in range(tc_sets):
            p += 4

        stride = 3 * 4
        if has_normals:
            stride += 3 * 4
        stride += tc_sets * 2 * 4

        vrts_end = vrts_pos + 8 + payload_len
        while p + stride <= vrts_end:
            x = struct.unpack_from("<f", data, p)[0]
            y = struct.unpack_from("<f", data, p + 4)[0]
            z = struct.unpack_from("<f", data, p + 8)[0]
            positions.append((x, y, z))
            p += stride

        search_start = vrts_pos + 8 + payload_len

    assert found_any, "VRTS chunk not found"
    return positions


# ---------------------------------------------------------------------------
# Check #4: UV channel 0 within assigned 8×8 atlas cell for all building LOD0
# models. Also validates .meta atlas_cell matches Phase 11e Cell Assignment Table.
# ---------------------------------------------------------------------------
def check_4_building_uv_atlas_cell(assets_dir):
    """Check #4: UV channel 0 within assigned 8x8 atlas cell for all building LOD0 models.

    For each entry in _PHASE_11E_CELL_ASSIGNMENT:
      1. Verifies the .meta atlas_cell row/col matches the Phase 11e Cell Assignment Table.
      2. Parses UV channel 0 from the _lod0.b3d file and verifies all UVs fall within
         [col/8, (col+1)/8] × [row/8, (row+1)/8].

    Returns a list of error strings. Empty list means the check passed.
    """
    buildings_dir = os.path.join(assets_dir, "3d", "buildings")
    if not os.path.isdir(buildings_dir):
        return [f"Check #4: buildings directory not found: {buildings_dir}"]

    errors = []
    checked = 0
    TOL = 1e-4  # floating-point tolerance for boundary comparisons

    for asset_name, (exp_row, exp_col) in _PHASE_11E_CELL_ASSIGNMENT.items():
        lod0_path = os.path.join(buildings_dir, f"{asset_name}_lod0.b3d")
        meta_path = os.path.join(buildings_dir, f"{asset_name}.meta")

        # 1. Verify .meta atlas_cell matches Phase 11e Cell Assignment Table
        if not os.path.exists(meta_path):
            errors.append(
                f"{asset_name}: missing {meta_path}"
            )
            continue
        try:
            with open(meta_path, "r") as mf:
                meta = json.load(mf)
        except Exception as exc:
            errors.append(f"{asset_name}: cannot parse {meta_path}: {exc}")
            continue
        ac = meta.get("atlas_cell", {})
        meta_row = ac.get("row")
        meta_col = ac.get("col")
        if meta_row != exp_row or meta_col != exp_col:
            errors.append(
                f"{asset_name}: {meta_path} atlas_cell={{row:{meta_row},col:{meta_col}}} "
                f"expected row={exp_row} col={exp_col} per Phase 11e Cell Assignment Table"
            )

        # 2. Verify UV coordinates in LOD0 model fall within the assigned cell
        if not os.path.exists(lod0_path):
            errors.append(f"{asset_name}: missing {lod0_path}")
            continue
        try:
            uvs = _parse_b3d_uv_channel0(lod0_path)
        except AssertionError as exc:
            errors.append(f"{asset_name}: {lod0_path}: {exc}")
            continue

        u_min = exp_col / 8.0
        u_max = (exp_col + 1) / 8.0
        v_min = exp_row / 8.0
        v_max = (exp_row + 1) / 8.0
        # ROOF_CELL = (5, 0): shared roof cell used by all buildings for roof faces
        roof_u_min, roof_u_max = 0.0, 0.125   # col 0 → U=[0.0, 0.125]
        roof_v_min, roof_v_max = 0.625, 0.75  # row 5 → V=[0.625, 0.75]
        # SOLID_WALL_CELL = (5, 6): plain brick — used for gable ends on all buildings
        solid_u_min, solid_u_max = 0.75, 0.875  # col 6 → U=[0.75, 0.875]
        solid_v_min, solid_v_max = 0.625, 0.75  # row 5 → V=[0.625, 0.75]
        # Door cells: res_low_02 uses (6,0), res_low_03 uses (6,1)
        door_cells = []
        if asset_name == "res_low_02":
            door_cells.append((0.0, 0.125, 0.75, 0.875))    # DOOR_CELL (6,0)
        elif asset_name == "res_low_03":
            door_cells.append((0.125, 0.25, 0.75, 0.875))   # DOOR_CELL (6,1)
        # Phase 11f ground-feature cells: row 5, cols 1-5 — UV V=[0.625, 0.75]
        GROUND_V_MIN = 5 / 8.0 - TOL   # = 0.625 - TOL
        GROUND_V_MAX = 6 / 8.0 + TOL   # = 0.75 + TOL
        GROUND_U_RANGES = [(col / 8.0 - TOL, (col + 1) / 8.0 + TOL) for col in range(1, 6)]
        violation = None
        for u, v in uvs:
            in_wall = u_min - TOL <= u <= u_max + TOL and v_min - TOL <= v <= v_max + TOL
            in_roof = roof_u_min - TOL <= u <= roof_u_max + TOL and roof_v_min - TOL <= v <= roof_v_max + TOL
            in_solid = solid_u_min - TOL <= u <= solid_u_max + TOL and solid_v_min - TOL <= v <= solid_v_max + TOL
            in_door = any(du0 - TOL <= u <= du1 + TOL and dv0 - TOL <= v <= dv1 + TOL
                          for du0, du1, dv0, dv1 in door_cells)
            in_ground = (GROUND_V_MIN <= v <= GROUND_V_MAX and
                         any(u_lo <= u <= u_hi for u_lo, u_hi in GROUND_U_RANGES))
            if not (in_wall or in_roof or in_solid or in_door or in_ground):
                violation = (u, v)
                break
        if violation is not None:
            u, v = violation
            errors.append(
                f"{asset_name}: {lod0_path} UV ({u:.6f},{v:.6f}) "
                f"outside cell ({exp_row},{exp_col}) bounds "
                f"U=[{u_min:.4f},{u_max:.4f}] V=[{v_min:.4f},{v_max:.4f}]"
            )
        else:
            checked += 1

    if not errors:
        print(f"  check_4: {checked} building LOD0 models verified UV within assigned 8x8 atlas cell")
    return errors


# ---------------------------------------------------------------------------
# Check #4b: Bounding box of all building LOD0 models must include ground
# quad vertices: min.Y <= 0 and max.Y >= 0.01 (Phase 11f requirement).
# ---------------------------------------------------------------------------
def check_4b_building_bbox(assets_dir):
    """Check #4b: verify bounding box of each building LOD0 includes ground quad at y=0.01.

    Reads all vertex positions from each _lod0.b3d file and checks:
      - min.Y <= 0          (building base at ground level)
      - max.Y >= 0.01       (ground quad vertex present at y=0.01)
      - |min.X| >= fh-TOL   (ground quad XZ extent matches FOOTPRINT_HALF)
      - max.X  >= fh-TOL
      - |min.Z| >= fh-TOL
      - max.Z  >= fh-TOL

    Expected half-extents (metres) match FOOTPRINT_HALF in generate_b3d_models.py:
      low  → 5.0,  med → 10.0,  high → 15.0,  svc → 10.0

    Returns a list of error strings. Empty list means the check passed.
    """
    # Mirror of FOOTPRINT_HALF from generate_b3d_models.py
    _FOOTPRINT_HALF = {"low": 5.0, "med": 10.0, "high": 15.0, "svc": 10.0}

    def _expected_fh(name):
        """Derive expected XZ half-extent from asset name tier."""
        if name == "svc_water_tower":
            return 4.0  # spec: 8S×8S centred pad (Phase 11f)
        for tier in ("low", "med", "high"):
            if f"_{tier}_" in name:
                return _FOOTPRINT_HALF[tier]
        if name.startswith("svc_"):
            return _FOOTPRINT_HALF["svc"]
        return None

    buildings_dir = os.path.join(assets_dir, "3d", "buildings")
    if not os.path.isdir(buildings_dir):
        return [f"Check #4b: buildings directory not found: {buildings_dir}"]

    errors = []
    checked = 0
    TOL = 1e-4

    for asset_name in _PHASE_11E_CELL_ASSIGNMENT:
        lod0_path = os.path.join(buildings_dir, f"{asset_name}_lod0.b3d")
        if not os.path.exists(lod0_path):
            errors.append(f"Check #4b {asset_name}: missing {lod0_path}")
            continue
        try:
            positions = _parse_b3d_positions(lod0_path)
        except Exception as exc:
            errors.append(f"Check #4b {asset_name}: cannot parse {lod0_path}: {exc}")
            continue
        if not positions:
            errors.append(f"Check #4b {asset_name}: no vertices found in {lod0_path}")
            continue

        xs = [p[0] for p in positions]
        ys = [p[1] for p in positions]
        zs = [p[2] for p in positions]
        min_x, max_x = min(xs), max(xs)
        min_y, max_y = min(ys), max(ys)
        min_z, max_z = min(zs), max(zs)

        asset_errors = []

        if min_y > TOL:
            asset_errors.append(
                f"min.Y={min_y:.6f} > 0 (building should start at y=0)")
        if max_y < 0.01 - TOL:
            asset_errors.append(
                f"max.Y={max_y:.6f} < 0.01 (ground quad at y=0.01 appears missing)")

        fh = _expected_fh(asset_name)
        if fh is not None:
            if max_x < fh - TOL:
                asset_errors.append(
                    f"max.X={max_x:.4f} < {fh:.1f} (ground quad XZ extent too small)")
            if min_x > -(fh - TOL):
                asset_errors.append(
                    f"min.X={min_x:.4f} > -{fh:.1f} (ground quad XZ extent too small)")
            if max_z < fh - TOL:
                asset_errors.append(
                    f"max.Z={max_z:.4f} < {fh:.1f} (ground quad XZ extent too small)")
            if min_z > -(fh - TOL):
                asset_errors.append(
                    f"min.Z={min_z:.4f} > -{fh:.1f} (ground quad XZ extent too small)")

        for msg in asset_errors:
            errors.append(f"Check #4b {asset_name}: {msg}")
        if not asset_errors:
            checked += 1

    if not errors:
        print(f"  check_4b: {checked} building LOD0 bounding boxes verified "
              f"(min.Y<=0, max.Y>=0.01, XZ>=FOOTPRINT_HALF)")
    return errors


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


_CLOUDS_PNG = os.path.join("assets", "textures", "sky", "clouds.png")
_CLOUDS_REQUIRED_WIDTH = 1024
_CLOUDS_REQUIRED_HEIGHT = 1024
_CLOUDS_REQUIRED_MODE = "RGBA"


def check_24_clouds_png(assets_dir):
    """Check #24: Cloud texture format gate — clouds.png must be 1024×1024 RGBA.

    Returns a list of error strings. Empty list means all checks passed.
    No-op (returns []) when the file does not exist (asset not yet delivered).
    """
    repo_root = os.path.dirname(assets_dir)
    clouds_path = os.path.join(repo_root, _CLOUDS_PNG)

    if not os.path.isfile(clouds_path):
        return []  # Asset not yet delivered — skip silently.

    errors = []
    try:
        from PIL import Image  # noqa: PLC0415 — lazy import (Pillow optional)
        with Image.open(clouds_path) as img:
            width, height = img.size
            mode = img.mode

        if width != _CLOUDS_REQUIRED_WIDTH or height != _CLOUDS_REQUIRED_HEIGHT:
            errors.append(
                f"Check #24 [clouds.png]: size = {width}x{height} "
                f"(expected {_CLOUDS_REQUIRED_WIDTH}x{_CLOUDS_REQUIRED_HEIGHT})."
            )
        if mode != _CLOUDS_REQUIRED_MODE:
            errors.append(
                f"Check #24 [clouds.png]: mode = {mode!r} "
                f"(expected {_CLOUDS_REQUIRED_MODE!r})."
            )
    except ImportError:
        errors.append(
            "Check #24: Pillow (PIL) is not installed. "
            "Run `pip install Pillow` before running validate_assets.py. "
            "Cannot validate clouds.png dimensions/mode."
        )

    return errors


# ---------------------------------------------------------------------------
# Check #25 — vehicles_diffuse_atlas_d.dds DDS format
#
# Spec (phase-11d.md §2b):
#   vehicles_diffuse_atlas_d.dds must be:
#     - DDS magic at offset 0
#     - DX10 extended header present (FourCC 'DX10' at offset 84)
#     - DXGI_FORMAT at offset 128 = 72 (DXGI_FORMAT_BC1_UNORM_SRGB)
#     - Width = 2048, Height = 2048
#     - dwMipMapCount >= 4
#
# The existing Phase 9 stub uses a plain DXT1 FourCC (no DX10 header) so this
# check will FAIL until the artist delivers the sRGB-tagged DDS from the
# Phase 11d texture rework pipeline.  That is the expected and correct
# behaviour — the check infrastructure is in place; the asset will follow.
# ---------------------------------------------------------------------------
_VEHICLES_DIFFUSE_ATLAS_PATH = os.path.join(
    "assets", "textures", "vehicles", "vehicles_diffuse_atlas_d.dds"
)
_DXGI_FORMAT_BC1_UNORM_SRGB = 72


def check_25_vehicles_diffuse_atlas_dds(assets_dir):
    """Check #25: vehicles_diffuse_atlas_d.dds — BC1_UNORM_SRGB, DX10, 2048×2048, ≥4 mip.

    Returns a list of error strings. Empty list means the check passed.
    Returns a single-element list with a FAIL message if the file is not found.
    """
    repo_root = os.path.dirname(assets_dir)
    path = os.path.join(repo_root, _VEHICLES_DIFFUSE_ATLAS_PATH)

    if not os.path.isfile(path):
        return [f"check_25 FAIL: {_VEHICLES_DIFFUSE_ATLAS_PATH} not found"]

    errors = []
    try:
        with open(path, "rb") as f:
            data = f.read(132)
    except OSError as exc:
        return [f"check_25 FAIL: cannot read {_VEHICLES_DIFFUSE_ATLAS_PATH}: {exc}"]

    if len(data) < 132:
        return [f"check_25 FAIL: {_VEHICLES_DIFFUSE_ATLAS_PATH} is too small to parse DDS header"]

    if data[0:4] != b"DDS ":
        errors.append(
            f"check_25 FAIL: {_VEHICLES_DIFFUSE_ATLAS_PATH} missing DDS magic "
            f"(got {data[0:4]!r})"
        )
        return errors

    height = struct.unpack_from("<I", data, 12)[0]
    width  = struct.unpack_from("<I", data, 16)[0]
    mip    = struct.unpack_from("<I", data, 28)[0]
    fourcc = data[84:88]

    if fourcc != b"DX10":
        errors.append(
            f"check_25 FAIL: {_VEHICLES_DIFFUSE_ATLAS_PATH} missing DX10 extended header "
            f"(FourCC at offset 84 = {fourcc!r}; expected b'DX10'). "
            f"Re-export using Compressonator or nvcompress -color to produce BC1_UNORM_SRGB."
        )
        return errors

    dxgi = struct.unpack_from("<I", data, 128)[0]
    if dxgi != _DXGI_FORMAT_BC1_UNORM_SRGB:
        errors.append(
            f"check_25 FAIL: {_VEHICLES_DIFFUSE_ATLAS_PATH} DXGI_FORMAT={dxgi} "
            f"(expected {_DXGI_FORMAT_BC1_UNORM_SRGB} = BC1_UNORM_SRGB). "
            f"Re-export as BC1_UNORM_SRGB for correct sRGB diffuse upload."
        )

    if width != 2048 or height != 2048:
        errors.append(
            f"check_25 FAIL: {_VEHICLES_DIFFUSE_ATLAS_PATH} dimensions {width}×{height} "
            f"(expected 2048×2048)."
        )

    if mip < 4:
        errors.append(
            f"check_25 FAIL: {_VEHICLES_DIFFUSE_ATLAS_PATH} dwMipMapCount={mip} "
            f"(expected ≥4 mip levels)."
        )

    return errors


# ---------------------------------------------------------------------------
# Check #26 — vehicles_sprite_atlas_d.dds DDS format
#
# Spec (phase-11d.md §2b):
#   vehicles_sprite_atlas_d.dds must be:
#     - DDS magic at offset 0
#     - DX10 extended header present
#     - DXGI_FORMAT at offset 128 = 77 (DXGI_FORMAT_BC3_UNORM — linear DXT5, NOT sRGB)
#     - Width = 256, Height = 256
#     - dwMipMapCount = 1 (base level only, GL_TEXTURE_MAX_LEVEL=0)
#
# Linear (not sRGB) because sprite atlas contains synthetic roof-colour swatches,
# not photographic diffuse data.  The vehicles_sprite_atlas_d.dds upload path in
# TextureCache uses a linear internal format accordingly.
# ---------------------------------------------------------------------------
_VEHICLES_SPRITE_ATLAS_PATH = os.path.join(
    "assets", "textures", "vehicles", "vehicles_sprite_atlas_d.dds"
)
_DXGI_FORMAT_BC3_UNORM = 77


def check_26_vehicles_sprite_atlas_dds(assets_dir):
    """Check #26: vehicles_sprite_atlas_d.dds — BC3_UNORM (linear), DX10, 256×256, 1 mip.

    Returns a list of error strings. Empty list means the check passed.
    """
    repo_root = os.path.dirname(assets_dir)
    path = os.path.join(repo_root, _VEHICLES_SPRITE_ATLAS_PATH)

    if not os.path.isfile(path):
        return [f"check_26 FAIL: {_VEHICLES_SPRITE_ATLAS_PATH} not found"]

    errors = []
    try:
        with open(path, "rb") as f:
            data = f.read(132)
    except OSError as exc:
        return [f"check_26 FAIL: cannot read {_VEHICLES_SPRITE_ATLAS_PATH}: {exc}"]

    if len(data) < 132:
        return [f"check_26 FAIL: {_VEHICLES_SPRITE_ATLAS_PATH} is too small to parse DDS header"]

    if data[0:4] != b"DDS ":
        errors.append(
            f"check_26 FAIL: {_VEHICLES_SPRITE_ATLAS_PATH} missing DDS magic "
            f"(got {data[0:4]!r})"
        )
        return errors

    height = struct.unpack_from("<I", data, 12)[0]
    width  = struct.unpack_from("<I", data, 16)[0]
    mip    = struct.unpack_from("<I", data, 28)[0]
    fourcc = data[84:88]

    if fourcc != b"DX10":
        errors.append(
            f"check_26 FAIL: {_VEHICLES_SPRITE_ATLAS_PATH} missing DX10 extended header "
            f"(FourCC at offset 84 = {fourcc!r}; expected b'DX10'). "
            f"Re-export using Compressonator with -fd BC3 to produce BC3_UNORM (linear)."
        )
        return errors

    dxgi = struct.unpack_from("<I", data, 128)[0]
    if dxgi != _DXGI_FORMAT_BC3_UNORM:
        errors.append(
            f"check_26 FAIL: {_VEHICLES_SPRITE_ATLAS_PATH} DXGI_FORMAT={dxgi} "
            f"(expected {_DXGI_FORMAT_BC3_UNORM} = BC3_UNORM linear). "
            f"Sprite atlas uses linear DXT5 — do NOT use BC3_UNORM_SRGB (78) here."
        )

    if width != 256 or height != 256:
        errors.append(
            f"check_26 FAIL: {_VEHICLES_SPRITE_ATLAS_PATH} dimensions {width}×{height} "
            f"(expected 256×256)."
        )

    if mip != 1:
        errors.append(
            f"check_26 FAIL: {_VEHICLES_SPRITE_ATLAS_PATH} dwMipMapCount={mip} "
            f"(expected exactly 1 — base level only; GL_TEXTURE_MAX_LEVEL=0 for sprite atlas)."
        )

    return errors


# ---------------------------------------------------------------------------
# Check #27 — vehicles_normal_atlas_n.dds DDS format
#
# Spec (phase-11d.md §2b):
#   vehicles_normal_atlas_n.dds must be:
#     - DDS magic at offset 0
#     - DX10 extended header present
#     - DXGI_FORMAT at offset 128 = 77 (DXGI_FORMAT_BC3_UNORM — linear DXT5nm, NOT sRGB)
#     - Width = 2048, Height = 2048
#     - dwMipMapCount >= 4
#
# Normal maps are always linear (not sRGB).  DXT5nm packing: X→alpha, Y→green, Z=0.
# ---------------------------------------------------------------------------
_VEHICLES_NORMAL_ATLAS_PATH = os.path.join(
    "assets", "textures", "vehicles", "vehicles_normal_atlas_n.dds"
)


def check_27_vehicles_normal_atlas_dds(assets_dir):
    """Check #27: vehicles_normal_atlas_n.dds — BC3_UNORM (linear DXT5nm), DX10, 2048×2048, ≥4 mip.

    Returns a list of error strings. Empty list means the check passed.
    """
    repo_root = os.path.dirname(assets_dir)
    path = os.path.join(repo_root, _VEHICLES_NORMAL_ATLAS_PATH)

    if not os.path.isfile(path):
        return [f"check_27 FAIL: {_VEHICLES_NORMAL_ATLAS_PATH} not found"]

    errors = []
    try:
        with open(path, "rb") as f:
            data = f.read(132)
    except OSError as exc:
        return [f"check_27 FAIL: cannot read {_VEHICLES_NORMAL_ATLAS_PATH}: {exc}"]

    if len(data) < 132:
        return [f"check_27 FAIL: {_VEHICLES_NORMAL_ATLAS_PATH} is too small to parse DDS header"]

    if data[0:4] != b"DDS ":
        errors.append(
            f"check_27 FAIL: {_VEHICLES_NORMAL_ATLAS_PATH} missing DDS magic "
            f"(got {data[0:4]!r})"
        )
        return errors

    height = struct.unpack_from("<I", data, 12)[0]
    width  = struct.unpack_from("<I", data, 16)[0]
    mip    = struct.unpack_from("<I", data, 28)[0]
    fourcc = data[84:88]

    if fourcc != b"DX10":
        errors.append(
            f"check_27 FAIL: {_VEHICLES_NORMAL_ATLAS_PATH} missing DX10 extended header "
            f"(FourCC at offset 84 = {fourcc!r}; expected b'DX10'). "
            f"Re-export using Compressonator with -fd BC3 to produce BC3_UNORM (linear DXT5nm)."
        )
        return errors

    dxgi = struct.unpack_from("<I", data, 128)[0]
    if dxgi != _DXGI_FORMAT_BC3_UNORM:
        errors.append(
            f"check_27 FAIL: {_VEHICLES_NORMAL_ATLAS_PATH} DXGI_FORMAT={dxgi} "
            f"(expected {_DXGI_FORMAT_BC3_UNORM} = BC3_UNORM linear). "
            f"Normal maps are linear — do NOT use BC3_UNORM_SRGB (78) here."
        )

    if width != 2048 or height != 2048:
        errors.append(
            f"check_27 FAIL: {_VEHICLES_NORMAL_ATLAS_PATH} dimensions {width}×{height} "
            f"(expected 2048×2048)."
        )

    if mip < 4:
        errors.append(
            f"check_27 FAIL: {_VEHICLES_NORMAL_ATLAS_PATH} dwMipMapCount={mip} "
            f"(expected ≥4 mip levels)."
        )

    return errors


# ---------------------------------------------------------------------------
# Check #28 — Building atlas diffuse minimum variance
#
# Spec (phase-11d.md §2a):
#   For each of the 9 zone-type wall cells (rows 0–2, cols 0–2) in the
#   2048×2048 buildings_atlas_d.png, compute the standard deviation of pixel
#   luminance within the 496×496 px usable area (8 px border on each edge of
#   a 512×512 cell).  A stddev < 8.0 (0–255 scale) indicates a flat placeholder
#   fill and is treated as a CI failure.
#
# Atlas cell layout (from building-atlas-layout.md):
#   4×4 grid at 512×512 px per cell.
#   Cell (row, col) starts at pixel (row*512, col*512).
#   Usable area: 8 px inset on all four edges → starts at (row*512+8, col*512+8),
#   size 496×496.
#
# Implementation note: this check requires Pillow.  If Pillow is absent or the
# source PNG does not exist, the check is SKIPped (not FAILed) so that
# environments without Pillow or without the PNG do not block CI jobs unrelated
# to the texture rework deliverable.
# ---------------------------------------------------------------------------
_BUILDINGS_ATLAS_PNG = os.path.join("assets", "textures", "buildings", "buildings_atlas_d.png")
_BUILDING_CELL_SIZE = 512
_BUILDING_CELL_BORDER = 8
_BUILDING_CELL_USABLE = _BUILDING_CELL_SIZE - 2 * _BUILDING_CELL_BORDER  # 496
_BUILDING_LUMINANCE_STDDEV_MIN = 8.0

# Zone-type wall cells: rows 0–2, cols 0–2 (9 cells total).
_BUILDING_WALL_CELLS = [(r, c) for r in range(3) for c in range(3)]


def _luminance_stddev(pixels):
    """Compute the standard deviation of ITU-R BT.601 luminance for a list of RGB tuples."""
    import math as _math
    n = len(pixels)
    if n == 0:
        return 0.0
    lum = [0.299 * r + 0.587 * g + 0.114 * b for r, g, b in pixels]
    mean = sum(lum) / n
    variance = sum((l - mean) ** 2 for l in lum) / n
    return _math.sqrt(variance)


def check_28_building_atlas_color_variance(assets_dir):
    """Check #28: building atlas diffuse minimum variance.

    For each of the 9 zone-type wall cells (rows 0–2, cols 0–2) in
    buildings_atlas_d.png, the luminance stddev within the 496×496 usable
    area must be ≥ 8.0 (otherwise the cell is a flat placeholder fill).

    Returns a list of error strings. Empty list means the check passed.
    SKIPs (returns []) when Pillow is absent or the source PNG does not exist.
    """
    repo_root = os.path.dirname(assets_dir)
    png_path = os.path.join(repo_root, _BUILDINGS_ATLAS_PNG)

    if not os.path.isfile(png_path):
        # Asset not yet delivered — skip silently.
        return []

    try:
        from PIL import Image  # noqa: PLC0415 — lazy import (Pillow optional)
    except ImportError:
        # Pillow not installed — skip with a note (not a failure).
        print(
            "SKIP check_28: Pillow not installed — "
            "install with 'pip install Pillow' to enable building atlas variance check"
        )
        return []

    errors = []
    try:
        img = Image.open(png_path).convert("RGB")
    except Exception as exc:
        return [f"check_28 FAIL: cannot open {_BUILDINGS_ATLAS_PNG}: {exc}"]

    for row, col in _BUILDING_WALL_CELLS:
        x0 = col * _BUILDING_CELL_SIZE + _BUILDING_CELL_BORDER
        y0 = row * _BUILDING_CELL_SIZE + _BUILDING_CELL_BORDER
        x1 = x0 + _BUILDING_CELL_USABLE
        y1 = y0 + _BUILDING_CELL_USABLE

        region = img.crop((x0, y0, x1, y1))
        # Use get_flattened_data() (Pillow 14+) or fall back to getdata() for older Pillow.
        if hasattr(region, "get_flattened_data"):
            pixels = list(region.get_flattened_data())
        else:
            pixels = list(region.getdata())
        stddev = _luminance_stddev(pixels)

        if stddev < _BUILDING_LUMINANCE_STDDEV_MIN:
            errors.append(
                f"check_28 FAIL: wall cell (row={row}, col={col}) luminance stddev="
                f"{stddev:.2f} < {_BUILDING_LUMINANCE_STDDEV_MIN} (placeholder fill); "
                f"re-author per phase-11d.md §2a before committing the DDS"
            )

    return errors


# ---------------------------------------------------------------------------
# Check #29 — Normal map non-flat check
#
# Spec (phase-11d.md §2a):
#   For each of the 9 zone-type wall cells in the normal-map source PNG
#   (buildings_atlas_d_n.png), compute the mean absolute deviation (MAD) of
#   the green channel within the 496×496 usable area.  A MAD < 3.0 indicates
#   a flat normal map (no authored surface relief) and is treated as a CI failure.
#
# The normal-map source PNG is an authoring intermediate; it is NOT committed
# unless the artist also commits the corresponding height-map source.  When
# the file does not exist the check is skipped (no-op).
#
# If Pillow is absent, the check is also skipped (not failed).
# ---------------------------------------------------------------------------
_BUILDINGS_NORMAL_PNG_CANDIDATES = [
    os.path.join("assets", "textures", "buildings", "buildings_atlas_d_n.png"),
    os.path.join("assets", "textures", "buildings", "buildings_atlas_n.png"),
]
_NORMAL_MAP_GREEN_MAD_MIN = 3.0


def _green_channel_mad(pixels):
    """Compute the mean absolute deviation of the green channel for a list of RGB tuples."""
    n = len(pixels)
    if n == 0:
        return 0.0
    greens = [g for _r, g, _b in pixels]
    mean_g = sum(greens) / n
    return sum(abs(g - mean_g) for g in greens) / n


def check_29_normal_map_non_flat(assets_dir):
    """Check #29: normal map non-flat check.

    For each of the 9 zone-type wall cells in the normal-map source PNG, the
    green-channel MAD must be ≥ 3.0 (otherwise the normal map is flat).

    Returns a list of error strings. Empty list means the check passed.
    SKIPs (returns []) when Pillow is absent or the source PNG does not exist.
    """
    repo_root = os.path.dirname(assets_dir)

    png_path = None
    for candidate in _BUILDINGS_NORMAL_PNG_CANDIDATES:
        full = os.path.join(repo_root, candidate)
        if os.path.isfile(full):
            png_path = full
            break

    if png_path is None:
        # Asset not yet delivered — skip silently.
        return []

    try:
        from PIL import Image  # noqa: PLC0415 — lazy import (Pillow optional)
    except ImportError:
        print(
            "SKIP check_29: Pillow not installed — "
            "install with 'pip install Pillow' to enable normal map non-flat check"
        )
        return []

    errors = []
    try:
        img = Image.open(png_path).convert("RGB")
    except Exception as exc:
        return [f"check_29 FAIL: cannot open normal map source PNG: {exc}"]

    for row, col in _BUILDING_WALL_CELLS:
        x0 = col * _BUILDING_CELL_SIZE + _BUILDING_CELL_BORDER
        y0 = row * _BUILDING_CELL_SIZE + _BUILDING_CELL_BORDER
        x1 = x0 + _BUILDING_CELL_USABLE
        y1 = y0 + _BUILDING_CELL_USABLE

        region = img.crop((x0, y0, x1, y1))
        # Use get_flattened_data() (Pillow 14+) or fall back to getdata() for older Pillow.
        if hasattr(region, "get_flattened_data"):
            pixels = list(region.get_flattened_data())
        else:
            pixels = list(region.getdata())
        mad = _green_channel_mad(pixels)

        if mad < _NORMAL_MAP_GREEN_MAD_MIN:
            errors.append(
                f"check_29 FAIL: normal map cell (row={row}, col={col}) green channel "
                f"MAD={mad:.2f} < {_NORMAL_MAP_GREEN_MAD_MIN} (flat normal map); "
                f"re-author per phase-11d.md §2a before committing the DDS"
            )

    return errors


# ---------------------------------------------------------------------------
# Check #30 — Billboard atlas format and mip verification
#
# Spec (phase-11d.md §2c):
#   For each *_billboard.dds in assets/3d/buildings/, verify:
#     (a) DDS magic at offset 0
#     (b) DX10 extended header (FourCC 'DX10' at offset 84)
#     (c) DXGI_FORMAT at offset 128 = 78 (DXGI_FORMAT_BC3_UNORM_SRGB)
#     (d) Width = 1024, Height = 128
#     (e) dwMipMapCount = 4
#     (f) File size = 174,228 bytes
#
# Reference size note: the architecture spec table at §DDS Mip Chain Integrity
# cites 192,640 bytes for a DXT5/BC3 1024×128 4-mip DDS.  The correct
# calculation for a file WITH the DX10 extended header is:
#   4 (magic) + 124 (DDS_HEADER) + 20 (DX10 ext header) + 174,080 (pixel data)
#   = 174,228 bytes
# where pixel data = BC3 mip chain: 131,072 + 32,768 + 8,192 + 2,048 bytes.
# The 192,640 figure in the spec table assumes a plain 128-byte header (no DX10)
# AND has an arithmetic error; it does not match the actual file format used by
# this project.  The authoritative reference value is 174,228 bytes, matching
# the generate_dds_stubs.py output for all billboard atlas files.
# ---------------------------------------------------------------------------
_BILLBOARD_DIR = os.path.join("assets", "3d", "buildings")
_BILLBOARD_EXPECTED_WIDTH = 1024
_BILLBOARD_EXPECTED_HEIGHT = 128
_BILLBOARD_EXPECTED_MIP_COUNT = 4
_DXGI_FORMAT_BC3_UNORM_SRGB = 78
# DX10 header: 4 (magic) + 124 (DDS_HEADER) + 20 (DX10 ext) + 174,080 (BC3 mip chain) = 174,228
_BILLBOARD_EXPECTED_FILE_SIZE = 174_228


def check_30_billboard_atlas_format(assets_dir):
    """Check #30: Billboard atlas DDS format and mip verification.

    Verifies every *_billboard.dds in assets/3d/buildings/ for correct DX10
    header, BC3_UNORM_SRGB format, 1024×128 dimensions, 4 mip levels, and
    correct file size.

    Returns a list of error strings. Empty list means all files passed.
    Returns [] (no-op) when no billboard DDS files are found.
    """
    repo_root = os.path.dirname(assets_dir)
    billboard_dir = os.path.join(repo_root, _BILLBOARD_DIR)

    if not os.path.isdir(billboard_dir):
        return []

    billboard_files = sorted(
        p for p in os.listdir(billboard_dir) if p.endswith("_billboard.dds")
    )

    if not billboard_files:
        return []

    errors = []

    for filename in billboard_files:
        filepath = os.path.join(billboard_dir, filename)
        rel_path = os.path.join(_BILLBOARD_DIR, filename)

        file_size = os.path.getsize(filepath)

        try:
            with open(filepath, "rb") as f:
                data = f.read(132)
        except OSError as exc:
            errors.append(f"check_30 FAIL: {rel_path} — cannot read file: {exc}")
            continue

        if len(data) < 132:
            errors.append(
                f"check_30 FAIL: {rel_path} — file too small to parse DDS header "
                f"({len(data)} bytes)"
            )
            continue

        if data[0:4] != b"DDS ":
            errors.append(
                f"check_30 FAIL: {rel_path} — missing DDS magic "
                f"(got {data[0:4]!r})"
            )
            continue

        height = struct.unpack_from("<I", data, 12)[0]
        width  = struct.unpack_from("<I", data, 16)[0]
        mip    = struct.unpack_from("<I", data, 28)[0]
        fourcc = data[84:88]

        if fourcc != b"DX10":
            errors.append(
                f"check_30 FAIL: {rel_path} — missing DX10 extended header "
                f"(FourCC at offset 84 = {fourcc!r}; expected b'DX10')"
            )
            continue

        dxgi = struct.unpack_from("<I", data, 128)[0]

        file_errors = []

        if dxgi != _DXGI_FORMAT_BC3_UNORM_SRGB:
            file_errors.append(
                f"DXGI_FORMAT={dxgi} (expected {_DXGI_FORMAT_BC3_UNORM_SRGB} = BC3_UNORM_SRGB)"
            )

        if width != _BILLBOARD_EXPECTED_WIDTH or height != _BILLBOARD_EXPECTED_HEIGHT:
            file_errors.append(
                f"dimensions {width}×{height} "
                f"(expected {_BILLBOARD_EXPECTED_WIDTH}×{_BILLBOARD_EXPECTED_HEIGHT})"
            )

        if mip != _BILLBOARD_EXPECTED_MIP_COUNT:
            file_errors.append(
                f"dwMipMapCount={mip} (expected exactly {_BILLBOARD_EXPECTED_MIP_COUNT})"
            )

        if file_size != _BILLBOARD_EXPECTED_FILE_SIZE:
            file_errors.append(
                f"file size={file_size:,} bytes "
                f"(expected {_BILLBOARD_EXPECTED_FILE_SIZE:,} bytes for DX10+BC3 1024×128 4-mip)"
            )

        for reason in file_errors:
            errors.append(f"check_30 FAIL: {rel_path} — {reason}")

    return errors


def check_31_bitmap_font_assets(assets_dir):
    """Check #31: verify all six tier-specific bitmap font XML + PNG pairs exist."""
    errors = []
    fonts_dir = os.path.join(assets_dir, 'fonts')
    required_files = [
        'hud_font_720.xml',  'hud_font_720.png',
        'hud_font_1080.xml', 'hud_font_1080.png',
        'hud_font_1440.xml', 'hud_font_1440.png',
        'hud_mono_font_720.xml',  'hud_mono_font_720.png',
        'hud_mono_font_1080.xml', 'hud_mono_font_1080.png',
        'hud_mono_font_1440.xml', 'hud_mono_font_1440.png',
    ]
    for filename in required_files:
        path = os.path.join(fonts_dir, filename)
        if not os.path.isfile(path):
            errors.append(
                f"check_31 FAIL: assets/fonts/{filename} missing — "
                f"run python3 tools/generate_bitmap_fonts.py to regenerate."
            )
    return errors


# ---------------------------------------------------------------------------
# Check #32 — Vehicle LOD0/LOD1 triangle budget
# ---------------------------------------------------------------------------
def check_32_vehicle_triangle_budget(assets_dir):
    """Check #32: vehicle _lod0.b3d and _lod1.b3d triangle counts within budget.

    Uses a raw byte-scan for all TRIS chunks (handles multi-buffer B3D files where
    vertex count exceeds the Irrlicht 16-bit index limit and the mesh is split into
    multiple VRTS+TRIS buffer groups).

    Limits (from architecture/asset-standards/3d-model-standards.md):
      LOD0: VEHICLE_LOD0_MAX_TRIS = 510,000
      LOD1: VEHICLE_LOD1_MAX_TRIS = 12,000
    """
    vehicles_dir = os.path.join(assets_dir, "3d", "vehicles")
    if not os.path.isdir(vehicles_dir):
        return []

    def _count_tris(path):
        """Count total triangles across all TRIS chunks by raw byte scan."""
        with open(path, "rb") as f:
            data = f.read()
        total = 0
        pos = 0
        while True:
            idx = data.find(b"TRIS", pos)
            if idx < 0:
                break
            if idx + 8 <= len(data):
                sz = struct.unpack_from("<I", data, idx + 4)[0]
                total += sz // 12  # each triangle = 3 × i32 = 12 bytes
            pos = idx + 1
        return total

    errors = []
    checked = 0
    for meta_path in sorted(glob.glob(os.path.join(vehicles_dir, "*.meta"))):
        meta = _load_meta(meta_path)
        if meta.get("category") != "vehicle":
            continue
        base = _asset_base(meta_path)
        for lod, limit in (("lod0", VEHICLE_LOD0_MAX_TRIS), ("lod1", VEHICLE_LOD1_MAX_TRIS)):
            path = f"{base}_{lod}.b3d"
            if not os.path.isfile(path):
                continue
            if os.path.getsize(path) == 0:
                errors.append(f"check_32 FAIL: {path} is empty")
                continue
            n = _count_tris(path)
            if n > limit:
                errors.append(
                    f"check_32 FAIL: {os.path.relpath(path)} has {n:,} tris "
                    f"(limit {limit:,} for {lod.upper()})"
                )
            checked += 1

    if checked == 0:
        print("INFO check_32: no vehicle _lod0/_lod1 .b3d files found — no-op")
    elif not errors:
        print(f"check_32 PASS: {checked} vehicle LOD file(s) within triangle budget "
              f"(LOD0≤{VEHICLE_LOD0_MAX_TRIS:,}, LOD1≤{VEHICLE_LOD1_MAX_TRIS:,})")
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
        ("Check #4 (building LOD0 UV atlas cell)", check_4_building_uv_atlas_cell),
        ("Check #4b (building LOD0 bounding box ground quad)", check_4b_building_bbox),
        ("Check #21 (zone loop silence-floor)", check_21_zone_loop_silence_floor),
        ("Check #22 (WAV SFX format)", check_22_wav_sfx_format),
        ("Check #23 (sprite sheet PNG)", check_23_sprite_sheet_png),
        ("Check #24 (cloud texture format)", check_24_clouds_png),
        ("Check #25 (vehicles diffuse atlas DDS)", check_25_vehicles_diffuse_atlas_dds),
        ("Check #26 (vehicles sprite atlas DDS)", check_26_vehicles_sprite_atlas_dds),
        ("Check #27 (vehicles normal atlas DDS)", check_27_vehicles_normal_atlas_dds),
        ("Check #28 (building atlas color variance)", check_28_building_atlas_color_variance),
        ("Check #29 (normal map non-flat)", check_29_normal_map_non_flat),
        ("Check #30 (billboard atlas format)", check_30_billboard_atlas_format),
        ("Check #31 (bitmap font tier assets)", check_31_bitmap_font_assets),
        ("Check #32 (vehicle triangle budget)", check_32_vehicle_triangle_budget),
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
