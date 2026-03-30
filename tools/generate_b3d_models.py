#!/usr/bin/env python3
"""
generate_b3d_models.py — Production B3D geometry generator for AI Town V1 assets.

Generates full-geometry B3D files (with normals, UV channel 0 atlas coordinates, and
a lightmap UV channel 1 placeholder) for all V1 buildings and vehicles.

Architecture specs followed:
  - architecture/asset-standards/3d-model-standards.md
  - architecture/asset-standards/building-atlas-layout.md
  - architecture/game-design/zoning-system.md

B3D coordinate system (Irrlicht left-handed):
  +X = right, +Y = up, +Z = forward (into screen)
  Front-face winding: counter-clockwise when viewed from outside.

Run from workspace root:
    python tools/generate_b3d_models.py

Overwrites all existing B3D files in assets/3d/buildings/ and assets/3d/vehicles/.
"""

import os
import struct
import math

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
WORKSPACE_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILDINGS_DIR = os.path.join(WORKSPACE_ROOT, "assets", "3d", "buildings")
VEHICLES_DIR = os.path.join(WORKSPACE_ROOT, "assets", "3d", "vehicles")

# ---------------------------------------------------------------------------
# B3D binary encoding helpers
# ---------------------------------------------------------------------------

def pack_str(s: str) -> bytes:
    """Null-terminated ASCII string."""
    return s.encode("ascii") + b"\x00"

def pack_f32(v: float) -> bytes:
    return struct.pack("<f", v)

def pack_i32(v: int) -> bytes:
    return struct.pack("<i", v)

def pack_u32(v: int) -> bytes:
    return struct.pack("<I", v)

def chunk(tag: str, payload: bytes) -> bytes:
    """Build a B3D chunk: 4-byte tag + int32 payload-length + payload."""
    assert len(tag) == 4
    return tag.encode("ascii") + pack_i32(len(payload)) + payload

# ---------------------------------------------------------------------------
# Vertex / face data structures
# ---------------------------------------------------------------------------

class Vertex:
    """Position, normal, UV0 (atlas), UV1 (lightmap placeholder)."""
    __slots__ = ("x", "y", "z", "nx", "ny", "nz", "u0", "v0", "u1", "v1")

    def __init__(self, x, y, z, nx, ny, nz, u0, v0, u1=0.0, v1=0.0):
        self.x = x;  self.y = y;  self.z = z
        self.nx = nx; self.ny = ny; self.nz = nz
        self.u0 = u0; self.v0 = v0
        self.u1 = u1; self.v1 = v1

# ---------------------------------------------------------------------------
# UV atlas helpers
# ---------------------------------------------------------------------------

def atlas_uv(row: int, col: int, face_u: float, face_v: float) -> tuple:
    """
    Map a face-local UV (0..1) to the 8×8 atlas cell at (row, col).
    Each cell occupies 0.125 of the 0..1 UV range.
    """
    u = col * 0.125 + face_u * 0.125
    v = row * 0.125 + face_v * 0.125
    return (u, v)

# ---------------------------------------------------------------------------
# Quad geometry builder
# ---------------------------------------------------------------------------

def make_quad(
    v0pos, v1pos, v2pos, v3pos,   # 4 corner positions (CCW from outside)
    normal,                        # (nx, ny, nz) shared normal for this face
    uv_row: int, uv_col: int,      # atlas cell assignment
    lm_u0=0.0, lm_v0=0.0,          # lightmap UV patch origin (placeholder)
    lm_du=1.0, lm_dv=1.0           # lightmap UV patch extent
) -> tuple:
    """
    Build a face quad as 4 vertices + 2 triangles (indices relative to the
    start of the vertex list).

    B3D winding requirement (Irrlicht left-handed):
    Vertices v0..v3 must appear counter-clockwise when viewed from outside
    (i.e. from the direction the normal points toward).

    UV corners for the atlas cell:
      v0 → (0,0) top-left of cell
      v1 → (1,0) top-right
      v2 → (1,1) bottom-right
      v3 → (0,1) bottom-left

    Returns (verts: list[Vertex], tris: list[tuple(int,int,int)])
    where triangle indices are 0-based relative to this quad's verts.
    """
    nx, ny, nz = normal
    def uv(fu, fv):
        return atlas_uv(uv_row, uv_col, fu, fv)

    u00, v00 = uv(0.0, 0.0)
    u10, v10 = uv(1.0, 0.0)
    u11, v11 = uv(1.0, 1.0)
    u01, v01 = uv(0.0, 1.0)

    verts = [
        Vertex(*v0pos, nx, ny, nz, u00, v00, lm_u0,         lm_v0),
        Vertex(*v1pos, nx, ny, nz, u10, v10, lm_u0 + lm_du, lm_v0),
        Vertex(*v2pos, nx, ny, nz, u11, v11, lm_u0 + lm_du, lm_v0 + lm_dv),
        Vertex(*v3pos, nx, ny, nz, u01, v01, lm_u0,         lm_v0 + lm_dv),
    ]
    # Two CCW triangles covering the quad: (0,1,2) and (0,2,3)
    tris = [(0, 1, 2), (0, 2, 3)]
    return verts, tris

# ---------------------------------------------------------------------------
# Box geometry builder
# ---------------------------------------------------------------------------

def box_faces(
    xmin, xmax, ymin, ymax, zmin, zmax,
    wall_row: int, wall_col: int,
    roof_row: int, roof_col: int,
    include_bottom: bool = False,
    lm_scale: float = 1.0
) -> tuple:
    """
    Build a box mesh with 5 faces (4 walls + top), each as a separate quad
    (no shared vertices — each face has its own UV mapping).

    The box sits with its pivot at Y=0 (Irrlicht convention).

    Winding: each face's vertices are ordered CCW when viewed from outside.

    Returns (all_verts, all_tris) where tris use absolute vertex indices.
    """
    all_verts = []
    all_tris = []

    def add_face(verts, tris):
        base = len(all_verts)
        all_verts.extend(verts)
        for t in tris:
            all_tris.append((base + t[0], base + t[1], base + t[2]))

    # ----- Front face (Z = zmin, normal = (0,0,-1)) -----
    # CCW from outside (from -Z direction): BL→BR→TR→TL
    v, t = make_quad(
        (xmin, ymin, zmin), (xmax, ymin, zmin),
        (xmax, ymax, zmin), (xmin, ymax, zmin),
        (0.0, 0.0, -1.0), wall_row, wall_col
    )
    add_face(v, t)

    # ----- Back face (Z = zmax, normal = (0,0,+1)) -----
    # CCW from outside (from +Z direction): BR→BL→TL→TR
    v, t = make_quad(
        (xmax, ymin, zmax), (xmin, ymin, zmax),
        (xmin, ymax, zmax), (xmax, ymax, zmax),
        (0.0, 0.0, 1.0), wall_row, wall_col
    )
    add_face(v, t)

    # ----- Left face (X = xmin, normal = (-1,0,0)) -----
    # CCW from outside (from -X direction): BR→BL→TL→TR
    # Outside of left face: BL_z→BR_z→TR_z→TL_z  → from -X: see +Z as "right"
    v, t = make_quad(
        (xmin, ymin, zmax), (xmin, ymin, zmin),
        (xmin, ymax, zmin), (xmin, ymax, zmax),
        (-1.0, 0.0, 0.0), wall_row, wall_col
    )
    add_face(v, t)

    # ----- Right face (X = xmax, normal = (+1,0,0)) -----
    # CCW from outside (from +X direction): BL_z→BR_z→TR_z→TL_z → from +X: see -Z as "right"
    v, t = make_quad(
        (xmax, ymin, zmin), (xmax, ymin, zmax),
        (xmax, ymax, zmax), (xmax, ymax, zmin),
        (1.0, 0.0, 0.0), wall_row, wall_col
    )
    add_face(v, t)

    # ----- Top face (Y = ymax, normal = (0,+1,0)) -----
    # CCW from above (+Y direction): going -Z, +X, +Z, -X  → BL→BR→TR→TL from above
    v, t = make_quad(
        (xmin, ymax, zmin), (xmax, ymax, zmin),
        (xmax, ymax, zmax), (xmin, ymax, zmax),
        (0.0, 1.0, 0.0), roof_row, roof_col
    )
    add_face(v, t)

    if include_bottom:
        # ----- Bottom face (Y = ymin, normal = (0,-1,0)) -----
        v, t = make_quad(
            (xmin, ymin, zmax), (xmax, ymin, zmax),
            (xmax, ymin, zmin), (xmin, ymin, zmin),
            (0.0, -1.0, 0.0), wall_row, wall_col
        )
        add_face(v, t)

    return all_verts, all_tris

# ---------------------------------------------------------------------------
# B3D file assembly
# ---------------------------------------------------------------------------

def build_b3d(verts: list, tris: list, texture_name: str = "buildings_atlas_d.dds") -> bytes:
    """
    Build a complete B3D binary from vertex + triangle data.

    Vertex layout: position + normal + 1 UV channel (tc_sets=1)
      - UV0: atlas diffuse

    NOTE: tc_sets=1 only.  The Irrlicht B3D loader (CB3DMeshFileLoader) reads
    exactly three header fields from VRTS: flags, tex_coord_sets,
    tex_coord_set_size.  With tc_sets=2 the loader reads tc_flags[0] as
    tex_coord_sets and tc_flags[1] as tex_coord_set_size, then misinterprets
    the first float of the first vertex as tex_coord_set_size — causing total
    vertex data misalignment and completely invisible geometry.  tc_sets=1
    emits only one tc_flags word and aligns the loader's reads correctly.

    All faces use a single brush referencing texture index 0.
    """

    # ---- TEXS chunk: one texture reference ----
    texs_payload = (
        pack_str(texture_name) +
        pack_i32(1) +           # flags
        pack_i32(0) +           # blend
        pack_f32(0.0) +         # pos_x
        pack_f32(0.0) +         # pos_y
        pack_f32(1.0) +         # scale_x
        pack_f32(1.0) +         # scale_y
        pack_f32(0.0)           # rotation
    )
    texs_chunk = chunk("TEXS", texs_payload)

    # ---- BRUS chunk: one brush ----
    # n_texs=1 → each brush has 1 texture_id int32
    brus_payload = (
        pack_i32(1) +           # n_texs per brush
        pack_str("") +          # brush name
        pack_f32(1.0) +         # r
        pack_f32(1.0) +         # g
        pack_f32(1.0) +         # b
        pack_f32(1.0) +         # a
        pack_f32(0.0) +         # shininess
        pack_i32(1) +           # blend
        pack_i32(0) +           # fx
        pack_i32(0)             # texture_id[0] = index 0
    )
    brus_chunk = chunk("BRUS", brus_payload)

    # ---- VRTS chunk ----
    # flags = bit0 (normals present) = 1
    # tc_sets = 1  (UV0 diffuse only — lightmap UV dropped, see note above)
    # tc_flags[0] = 2  (2D coords)
    vrts_header = (
        pack_i32(1) +           # flags: bit0 = normals
        pack_i32(1) +           # tc_sets = 1
        pack_i32(2)             # tc_flags[0] = 2D
    )
    vrts_data = b""
    for v in verts:
        vrts_data += (
            pack_f32(v.x) + pack_f32(v.y) + pack_f32(v.z) +
            pack_f32(v.nx) + pack_f32(v.ny) + pack_f32(v.nz) +
            pack_f32(v.u0) + pack_f32(v.v0)    # UV channel 0: atlas only
        )
    vrts_chunk = chunk("VRTS", vrts_header + vrts_data)

    # ---- TRIS chunk ----
    tris_payload = pack_i32(-1)  # brush_id = -1 (inherit from MESH)
    for tri in tris:
        tris_payload += pack_i32(tri[0]) + pack_i32(tri[1]) + pack_i32(tri[2])
    tris_chunk = chunk("TRIS", tris_payload)

    # ---- MESH chunk ----
    mesh_payload = pack_i32(0) + vrts_chunk + tris_chunk  # brush_id = 0
    mesh_chunk = chunk("MESH", mesh_payload)

    # ---- NODE chunk ----
    node_payload = (
        pack_str("") +          # node name
        pack_f32(0.0) + pack_f32(0.0) + pack_f32(0.0) +    # pos
        pack_f32(1.0) + pack_f32(1.0) + pack_f32(1.0) +    # scale
        pack_f32(1.0) + pack_f32(0.0) + pack_f32(0.0) + pack_f32(0.0) +  # rot quaternion w,x,y,z
        mesh_chunk
    )
    node_chunk = chunk("NODE", node_payload)

    # ---- BB3D root chunk ----
    bb3d_payload = (
        pack_i32(2) +           # version = 2
        texs_chunk +
        brus_chunk +
        node_chunk
    )
    return chunk("BB3D", bb3d_payload)

# ---------------------------------------------------------------------------
# Building geometry definitions
# ---------------------------------------------------------------------------

# Atlas cell assignments from building-atlas-layout.md (8×8 grid, Phase 11e)
# Walls: (row, col) per (zone, tier, variant) — unique cell per variant
# Roof: cell shared for all (outside the 5-row used range)
WALL_CELLS = {
    # Row 0: res_low and res_med
    ("res", "low",  "01"): (0, 0),
    ("res", "low",  "02"): (0, 1),
    ("res", "low",  "03"): (0, 2),
    ("res", "low",  "04"): (0, 3),
    ("res", "med",  "01"): (0, 4),
    ("res", "med",  "02"): (0, 5),
    ("res", "med",  "03"): (0, 6),
    ("res", "med",  "04"): (0, 7),
    # Row 1: res_high and com_low
    ("res", "high", "01"): (1, 0),
    ("res", "high", "02"): (1, 1),
    ("res", "high", "03"): (1, 2),
    ("res", "high", "04"): (1, 3),
    ("com", "low",  "01"): (1, 4),
    ("com", "low",  "02"): (1, 5),
    ("com", "low",  "03"): (1, 6),
    ("com", "low",  "04"): (1, 7),
    # Row 2: com_med and com_high
    ("com", "med",  "01"): (2, 0),
    ("com", "med",  "02"): (2, 1),
    ("com", "med",  "03"): (2, 2),
    ("com", "med",  "04"): (2, 3),
    ("com", "high", "01"): (2, 4),
    ("com", "high", "02"): (2, 5),
    ("com", "high", "03"): (2, 6),
    ("com", "high", "04"): (2, 7),
    # Row 3: ind_low and ind_med
    ("ind", "low",  "01"): (3, 0),
    ("ind", "low",  "02"): (3, 1),
    ("ind", "low",  "03"): (3, 2),
    ("ind", "low",  "04"): (3, 3),
    ("ind", "med",  "01"): (3, 4),
    ("ind", "med",  "02"): (3, 5),
    ("ind", "med",  "03"): (3, 6),
    ("ind", "med",  "04"): (3, 7),
    # Row 4: ind_high and service buildings
    ("ind", "high", "01"): (4, 0),
    ("ind", "high", "02"): (4, 1),
    ("ind", "high", "03"): (4, 2),
    ("ind", "high", "04"): (4, 3),
    # Service buildings (each has its own key)
    ("svc", "fire_station"):   (4, 4),
    ("svc", "police_station"): (4, 5),
    ("svc", "power_plant"):    (4, 6),
    ("svc", "water_tower"):    (4, 7),
}
ROOF_CELL = (5, 0)  # shared roof cell — row 5 (reserved range) per building-atlas-layout.md
SOLID_WALL_CELL = (5, 6)  # plain brick, no windows — used for gable ends
RES_LOW_02_DOOR_CELL = (6, 0)  # cream wall + door — used only for left unit front face of res_low_02
RES_LOW_03_DOOR_CELL = (6, 1)  # brick wall + door — used only for front face of res_low_03

# Phase 11f ground-feature atlas cells — row 5, cols 1-5
GROUND_CELLS = {
    "garden":  (5, 1),
    "pool":    (5, 2),
    "paving":  (5, 3),
    "tarmac":  (5, 4),
    "gravel":  (5, 5),
}

# Building heights per tier (in Irrlicht unit space, before setScale)
# low=2 floors → 6 m, med=4 floors → 12 m, high=8 floors → 24 m (native-scale, setScale=1)
TIER_HEIGHT = {
    "low":  6.0,
    "med":  12.0,
    "high": 24.0,
    "svc":  6.0,   # service buildings are 2-floor equivalent
}

# Half-extent on X and Z in metres (leaves slight gap between tiles, native-scale)
BUILDING_HALF_XZ = 4.5

# Ground-quad half-extent per tier (= N_tiles * kTileSize / 2).
# The ground quad must cover the full N×N tile footprint so no terrain shows through.
FOOTPRINT_HALF = {
    "low":  5.0,   # 1×1 tile  → ±5 m
    "med":  10.0,  # 2×2 tiles → ±10 m
    "high": 15.0,  # 3×3 tiles → ±15 m
    "svc":  10.0,  # 2×2 tiles → ±10 m
}


# ---------------------------------------------------------------------------
# Geometry accumulator helper
# ---------------------------------------------------------------------------

class MeshAccum:
    """Accumulates vertices and triangles for building geometry."""
    def __init__(self):
        self.verts = []
        self.tris = []

    def add(self, verts, tris):
        base = len(self.verts)
        self.verts.extend(verts)
        for t in tris:
            self.tris.append((base + t[0], base + t[1], base + t[2]))

    def add_box(self, xmin, xmax, ymin, ymax, zmin, zmax,
                wall_row, wall_col, roof_row=None, roof_col=None,
                walls_only=False):
        if roof_row is None:
            roof_row, roof_col = ROOF_CELL
        if walls_only:
            for v, t in [
                make_quad((xmin,ymin,zmin),(xmax,ymin,zmin),(xmax,ymax,zmin),(xmin,ymax,zmin),(0,0,-1),wall_row,wall_col),
                make_quad((xmax,ymin,zmax),(xmin,ymin,zmax),(xmin,ymax,zmax),(xmax,ymax,zmax),(0,0,1),wall_row,wall_col),
                make_quad((xmin,ymin,zmax),(xmin,ymin,zmin),(xmin,ymax,zmin),(xmin,ymax,zmax),(-1,0,0),wall_row,wall_col),
                make_quad((xmax,ymin,zmin),(xmax,ymin,zmax),(xmax,ymax,zmax),(xmax,ymax,zmin),(1,0,0),wall_row,wall_col),
            ]:
                self.add(v, t)
        else:
            base = len(self.verts)
            v, t = box_faces(xmin, xmax, ymin, ymax, zmin, zmax,
                             wall_row, wall_col, roof_row, roof_col)
            self.verts.extend(v)
            for tri in t:
                self.tris.append((base + tri[0], base + tri[1], base + tri[2]))

    def add_quad(self, v0, v1, v2, v3, normal, row, col):
        v, t = make_quad(v0, v1, v2, v3, normal, row, col)
        self.add(v, t)

    def add_tri(self, v0pos, v1pos, v2pos, normal, row, col):
        """Add a single triangle (for non-quad faces like roof slopes)."""
        nx, ny, nz = normal
        def uv(fu, fv):
            return atlas_uv(row, col, fu, fv)
        verts = [
            Vertex(*v0pos, nx, ny, nz, *uv(0.0, 0.0)),
            Vertex(*v1pos, nx, ny, nz, *uv(1.0, 0.0)),
            Vertex(*v2pos, nx, ny, nz, *uv(0.5, 1.0)),
        ]
        base = len(self.verts)
        self.verts.extend(verts)
        self.tris.append((base, base+1, base+2))

    def tri_count(self):
        return len(self.tris)

    def to_b3d(self, tex="buildings_atlas_d.dds"):
        return build_b3d(self.verts, self.tris, texture_name=tex)


def _add_ground_quad(m, gtype, xmin, xmax, zmin, zmax):
    """
    Add a flat upward-facing quad at y = 0.01 (1 cm above terrain) for ground
    feature type gtype. Prevents depth-buffer conflict with terrain mesh at y = 0.
    UV-mapped to GROUND_CELLS[gtype].

    Callers pass -fh/+fh where fh = FOOTPRINT_HALF[tier], which equals the
    exact tile boundary in native-scale metres — no further clamping needed.

    Args:
        m: MeshAccum to add geometry to
        gtype: ground feature type string (key of GROUND_CELLS)
        xmin, xmax, zmin, zmax: XZ extents of the ground patch in metres
    """
    if xmin >= xmax or zmin >= zmax:
        return  # degenerate quad — skip silently
    row, col = GROUND_CELLS[gtype]
    # CCW from above (+Y): BL->BR->TR->TL
    v, t = make_quad(
        (xmin, 0.01, zmin), (xmax, 0.01, zmin),
        (xmax, 0.01, zmax), (xmin, 0.01, zmax),
        (0.0, 1.0, 0.0), row, col
    )
    m.add(v, t)


# ---------------------------------------------------------------------------
# Shared small-geometry primitives
# ---------------------------------------------------------------------------

def _add_windows(m, wall_row, wall_col, n_x, n_y, y_bot, y_top,
                 x_left, x_right, z_face, z_inset, normal_sign_z,
                 win_w_frac=0.18, win_h_frac=0.55):
    """Add n_x × n_y window recesses across a wall face."""
    nz = -1.0 if normal_sign_z < 0 else 1.0
    wall_w = x_right - x_left
    wall_h = y_top - y_bot
    win_w = wall_w * win_w_frac
    win_h = wall_h * win_h_frac
    col_step = wall_w / n_x
    row_step = wall_h / n_y
    depth = 0.025
    for col_i in range(n_x):
        cx = x_left + col_step * (col_i + 0.5)
        for row_i in range(n_y):
            cy = y_bot + row_step * (row_i + 0.5)
            wx0 = cx - win_w * 0.5
            wx1 = cx + win_w * 0.5
            wy0 = cy - win_h * 0.5
            wy1 = cy + win_h * 0.5
            z_back = z_face + depth * normal_sign_z
            # Back of recess
            if normal_sign_z < 0:
                m.add_quad((wx0,wy0,z_back),(wx1,wy0,z_back),(wx1,wy1,z_back),(wx0,wy1,z_back),(0,0,nz),wall_row,wall_col)
                # Side reveals
                m.add_quad((wx0,wy0,z_face),(wx0,wy0,z_back),(wx0,wy1,z_back),(wx0,wy1,z_face),(1,0,0),wall_row,wall_col)
                m.add_quad((wx1,wy0,z_back),(wx1,wy0,z_face),(wx1,wy1,z_face),(wx1,wy1,z_back),(-1,0,0),wall_row,wall_col)
                m.add_quad((wx0,wy0,z_face),(wx1,wy0,z_face),(wx1,wy0,z_back),(wx0,wy0,z_back),(0,1,0),wall_row,wall_col)
            else:
                m.add_quad((wx1,wy0,z_back),(wx0,wy0,z_back),(wx0,wy1,z_back),(wx1,wy1,z_back),(0,0,nz),wall_row,wall_col)
                m.add_quad((wx1,wy0,z_face),(wx1,wy0,z_back),(wx1,wy1,z_back),(wx1,wy1,z_face),(-1,0,0),wall_row,wall_col)
                m.add_quad((wx0,wy0,z_back),(wx0,wy0,z_face),(wx0,wy1,z_face),(wx0,wy1,z_back),(1,0,0),wall_row,wall_col)
                m.add_quad((wx1,wy0,z_face),(wx0,wy0,z_face),(wx0,wy0,z_back),(wx1,wy0,z_back),(0,1,0),wall_row,wall_col)


def _add_dense_facade_detail(m, wall_row, wall_col,
                              xmin, xmax, ymin, ymax, z_face,
                              n_horiz_strips=20, n_vert_strips=12,
                              strip_t=0.008, strip_h=0.010,
                              normal_sign_z=-1):
    """
    Dense architectural surface detail: horizontal ledge strips + vertical pilaster strips.
    Each strip = one thin box (8 tris walls_only).
    n_horiz_strips=20 + n_vert_strips=12 → 32 boxes × 8 tris = 256 tris per call.
    Call on multiple faces to accumulate tri count efficiently.
    """
    span_y = ymax - ymin
    span_x = xmax - xmin
    # Horizontal strips (ledges across full face width)
    for i in range(n_horiz_strips):
        by = ymin + span_y * (i + 0.5) / n_horiz_strips
        if normal_sign_z < 0:
            m.add_box(xmin, xmax, by, by+strip_h, z_face-strip_t, z_face,
                      wall_row, wall_col, walls_only=True)
        else:
            m.add_box(xmin, xmax, by, by+strip_h, z_face, z_face+strip_t,
                      wall_row, wall_col, walls_only=True)
    # Vertical pilaster strips
    for j in range(n_vert_strips):
        bx = xmin + span_x * (j + 0.5) / n_vert_strips
        if normal_sign_z < 0:
            m.add_box(bx, bx+strip_h, ymin, ymax, z_face-strip_t, z_face,
                      wall_row, wall_col, walls_only=True)
        else:
            m.add_box(bx, bx+strip_h, ymin, ymax, z_face, z_face+strip_t,
                      wall_row, wall_col, walls_only=True)


def _add_tiled_wall(m, xmin, xmax, y_bot, y_top, z, normal_sign_z,
                    wall_row, wall_col, floor_h=0.30):
    """
    Add a wall face (parallel to XY plane at given Z) split into horizontal
    strips of floor_h each so the texture tiles once per strip rather than
    being stretched across the full wall height.
    normal_sign_z: -1 for face pointing -Z, +1 for face pointing +Z.
    """
    y = y_bot
    while y < y_top - 0.001:
        y1 = min(y + floor_h, y_top)
        if normal_sign_z < 0:
            m.add_quad(
                (xmin, y, z), (xmax, y, z),
                (xmax, y1, z), (xmin, y1, z),
                (0, 0, -1), wall_row, wall_col
            )
        else:
            m.add_quad(
                (xmax, y, z), (xmin, y, z),
                (xmin, y1, z), (xmax, y1, z),
                (0, 0, 1), wall_row, wall_col
            )
        y = y1


def _add_tiled_wall_x(m, zmin, zmax, y_bot, y_top, x, normal_sign_x,
                       wall_row, wall_col, floor_h=0.30):
    """
    Add a wall face (parallel to ZY plane at given X) split into horizontal
    strips of floor_h each for proper UV tiling.
    normal_sign_x: -1 for face pointing -X, +1 for face pointing +X.
    """
    y = y_bot
    while y < y_top - 0.001:
        y1 = min(y + floor_h, y_top)
        if normal_sign_x < 0:
            m.add_quad(
                (x, y, zmax), (x, y, zmin),
                (x, y1, zmin), (x, y1, zmax),
                (-1, 0, 0), wall_row, wall_col
            )
        else:
            m.add_quad(
                (x, y, zmin), (x, y, zmax),
                (x, y1, zmax), (x, y1, zmin),
                (1, 0, 0), wall_row, wall_col
            )
        y = y1


def _add_horizontal_band(m, wall_row, wall_col,
                         xmin, xmax, y, zmin, zmax, thickness, height):
    """Horizontal projecting cornice/ledge band (facing outward on Z min face)."""
    # Front face
    m.add_quad((xmin,y,zmin-thickness),(xmax,y,zmin-thickness),
               (xmax,y+height,zmin-thickness),(xmin,y+height,zmin-thickness),(0,0,-1),wall_row,wall_col)
    # Top
    m.add_quad((xmin,y+height,zmin),(xmax,y+height,zmin),
               (xmax,y+height,zmin-thickness),(xmin,y+height,zmin-thickness),(0,1,0),wall_row,wall_col)
    # Bottom
    m.add_quad((xmin,y,zmin-thickness),(xmax,y,zmin-thickness),
               (xmax,y,zmin),(xmin,y,zmin),(0,-1,0),wall_row,wall_col)


def _add_gabled_roof(m, wall_row, wall_col, roof_row, roof_col,
                     xmin, xmax, y_base, ridge_h, zmin, zmax,
                     gable_row=None, gable_col=None):
    """
    Gabled (two-slope) pitched roof.
    Ridge runs along X axis from (xmin, ridge_y, cz) to (xmax, ridge_y, cz).
    Front slope faces -Z; back slope faces +Z.
    Gable ends (triangles) on X=xmin and X=xmax sides.

    gable_row/gable_col: atlas cell for the two end triangles. Defaults to
    SOLID_WALL_CELL (plain brick, no windows) so triangular gable faces do not
    show clipped window geometry from the wall texture.
    """
    if gable_row is None:
        gable_row, gable_col = SOLID_WALL_CELL
    cz = (zmin + zmax) * 0.5
    ridge_y = y_base + ridge_h
    # Front slope quad: bottom edge at Z=zmin, ridge at centre Z
    # CCW from outside (from -Z): BL, BR, TR, TL
    m.add_quad(
        (xmin, y_base, zmin), (xmax, y_base, zmin),
        (xmax, ridge_y, cz), (xmin, ridge_y, cz),
        (0, 0.5, -1), roof_row, roof_col
    )
    # Back slope quad: bottom edge at Z=zmax, ridge at centre Z
    # CCW from outside (from +Z): BL(zmax side right→left), TR at ridge
    m.add_quad(
        (xmax, y_base, zmax), (xmin, y_base, zmax),
        (xmin, ridge_y, cz), (xmax, ridge_y, cz),
        (0, 0.5, 1), roof_row, roof_col
    )
    # Left gable triangle (X=xmin): CCW from outside (from -X)
    # Viewed from -X: zmin→zmax is left→right, base→ridge is bottom→top
    # Use gable_row/col (solid brick, no windows) so the triangular end face
    # does not show clipped window geometry from the wall texture.
    m.add_tri(
        (xmin, y_base, zmin), (xmin, y_base, zmax), (xmin, ridge_y, cz),
        (-1, 0, 0), gable_row, gable_col
    )
    # Right gable triangle (X=xmax): CCW from outside (from +X)
    # Viewed from +X: zmax→zmin is left→right
    m.add_tri(
        (xmax, y_base, zmax), (xmax, y_base, zmin), (xmax, ridge_y, cz),
        (1, 0, 0), gable_row, gable_col
    )


def _add_hipped_roof(m, wall_row, wall_col, roof_row, roof_col,
                     xmin, xmax, y_base, ridge_h, zmin, zmax):
    """
    Hipped (four-slope) pitched roof with a ridge LINE (not a single apex).
    Ridge runs along X axis at Z centre, between 25% and 75% of the X span.
    """
    cz = (zmin + zmax) * 0.5
    ridge_y = y_base + ridge_h
    ridge_x0 = xmin + (xmax - xmin) * 0.25
    ridge_x1 = xmin + (xmax - xmin) * 0.75
    # Front slope quad (base at Z=zmin, ridge between ridge_x0 and ridge_x1)
    # CCW from outside (from -Z): BL, BR, TR, TL
    m.add_quad(
        (xmin, y_base, zmin), (xmax, y_base, zmin),
        (ridge_x1, ridge_y, cz), (ridge_x0, ridge_y, cz),
        (0, 0.5, -1), roof_row, roof_col
    )
    # Back slope quad (base at Z=zmax)
    # CCW from outside (from +Z)
    m.add_quad(
        (xmax, y_base, zmax), (xmin, y_base, zmax),
        (ridge_x0, ridge_y, cz), (ridge_x1, ridge_y, cz),
        (0, 0.5, 1), roof_row, roof_col
    )
    # Left hip triangle (X=xmin end): CCW from outside (from -X)
    m.add_tri(
        (xmin, y_base, zmin), (xmin, y_base, zmax), (ridge_x0, ridge_y, cz),
        (-1, 0.5, 0), roof_row, roof_col
    )
    # Right hip triangle (X=xmax end): CCW from outside (from +X)
    m.add_tri(
        (xmax, y_base, zmax), (xmax, y_base, zmin), (ridge_x1, ridge_y, cz),
        (1, 0.5, 0), roof_row, roof_col
    )


def _add_chimney(m, wall_row, wall_col, roof_row, roof_col,
                 cx, cz, y_base, height, hw=0.03):
    """Small chimney box."""
    m.add_box(cx-hw, cx+hw, y_base, y_base+height, cz-hw, cz+hw,
              wall_row, wall_col, roof_row, roof_col)


def _add_balcony_slab(m, wall_row, wall_col,
                      xmin, xmax, y, z_face, overhang):
    """Projecting balcony slab: thin horizontal box on front face."""
    t = 0.02   # slab thickness
    m.add_box(xmin, xmax, y-t, y, z_face-overhang, z_face,
              wall_row, wall_col, walls_only=False)


def _add_parapet(m, wall_row, wall_col, xmin, xmax, y_top, zmin, zmax, pw=0.03, ph=0.06):
    """Parapet lip around roof edge."""
    # Front
    m.add_box(xmin, xmax, y_top, y_top+ph, zmin-pw, zmin, wall_row, wall_col)
    # Back
    m.add_box(xmin, xmax, y_top, y_top+ph, zmax, zmax+pw, wall_row, wall_col)
    # Left
    m.add_box(xmin-pw, xmin, y_top, y_top+ph, zmin, zmax, wall_row, wall_col)
    # Right
    m.add_box(xmax, xmax+pw, y_top, y_top+ph, zmin, zmax, wall_row, wall_col)


def _add_ac_units(m, wall_row, wall_col, roof_row, roof_col,
                  y_top, xmin, xmax, zmin, zmax, count=4):
    """Staggered AC condenser boxes on rooftop."""
    uw, uh, ud = 0.06, 0.06, 0.04
    xs = [xmin + (xmax-xmin) * (i+1) / (count+1) for i in range(count)]
    zs = [zmin + (zmax-zmin) * 0.25, zmin + (zmax-zmin) * 0.65]
    for i, bx in enumerate(xs):
        bz = zs[i % 2]
        m.add_box(bx-uw/2, bx+uw/2, y_top, y_top+uh, bz-ud/2, bz+ud/2,
                  wall_row, wall_col, roof_row, roof_col)


def _add_facade_ribs(m, wall_row, wall_col,
                     x_left, x_right, y_bot, y_top, z_face, n_ribs, rib_w=0.008, rib_d=0.015):
    """Corrugated vertical ribs on an industrial wall face (Z = z_face, normal -Z)."""
    span = x_right - x_left
    step = span / (n_ribs + 1)
    for i in range(n_ribs):
        bx = x_left + step * (i + 1)
        # thin box projecting outward (toward -Z)
        m.add_box(bx-rib_w/2, bx+rib_w/2, y_bot, y_top,
                  z_face-rib_d, z_face, wall_row, wall_col, walls_only=True)


def _add_mono_pitch_roof(m, wall_row, wall_col, roof_row, roof_col,
                         xmin, xmax, y_low, y_high, zmin, zmax):
    """Mono-pitch shed roof: flat slope from y_low at z=zmin to y_high at z=zmax."""
    # Single sloping quad top face
    m.add_quad((xmin,y_high,zmax),(xmax,y_high,zmax),(xmax,y_low,zmin),(xmin,y_low,zmin),(0,1,0.3),roof_row,roof_col)
    # Front low wall fill (from wall top to roof low edge)
    # Back high gable end
    m.add_quad((xmax,y_high,zmax),(xmin,y_high,zmax),(xmin,y_low,zmax),(xmax,y_low,zmax),(0,0,1),wall_row,wall_col)
    # Side gable left
    m.add_quad((xmin,y_low,zmin),(xmin,y_low,zmax),(xmin,y_high,zmax),(xmin,y_high,zmax),(-1,0,0),wall_row,wall_col)
    m.add_quad((xmin,y_low,zmin),(xmin,y_high,zmax),(xmin,y_high,zmax),(xmin,y_low,zmin),(-1,0,0),wall_row,wall_col)
    # Side gable right
    m.add_quad((xmax,y_low,zmax),(xmax,y_low,zmin),(xmax,y_high,zmax),(xmax,y_high,zmax),(1,0,0),wall_row,wall_col)


def _add_loading_dock(m, wall_row, wall_col,
                      cx, y_bot, z_face, dock_w, dock_h, dock_d, normal_sign_z=-1):
    """Loading dock recess on a wall face."""
    wx0 = cx - dock_w/2
    wx1 = cx + dock_w/2
    z_back = z_face + dock_d * normal_sign_z
    nz = float(normal_sign_z)
    # Back wall
    if normal_sign_z < 0:
        m.add_quad((wx0,y_bot,z_back),(wx1,y_bot,z_back),(wx1,y_bot+dock_h,z_back),(wx0,y_bot+dock_h,z_back),(0,0,-1),wall_row,wall_col)
        m.add_quad((wx0,y_bot,z_face),(wx0,y_bot,z_back),(wx0,y_bot+dock_h,z_back),(wx0,y_bot+dock_h,z_face),(1,0,0),wall_row,wall_col)
        m.add_quad((wx1,y_bot,z_back),(wx1,y_bot,z_face),(wx1,y_bot+dock_h,z_face),(wx1,y_bot+dock_h,z_back),(-1,0,0),wall_row,wall_col)
        m.add_quad((wx0,y_bot+dock_h,z_face),(wx1,y_bot+dock_h,z_face),(wx1,y_bot+dock_h,z_back),(wx0,y_bot+dock_h,z_back),(0,1,0),wall_row,wall_col)
    else:
        m.add_quad((wx1,y_bot,z_back),(wx0,y_bot,z_back),(wx0,y_bot+dock_h,z_back),(wx1,y_bot+dock_h,z_back),(0,0,1),wall_row,wall_col)
        m.add_quad((wx1,y_bot,z_face),(wx1,y_bot,z_back),(wx1,y_bot+dock_h,z_back),(wx1,y_bot+dock_h,z_face),(-1,0,0),wall_row,wall_col)
        m.add_quad((wx0,y_bot,z_back),(wx0,y_bot,z_face),(wx0,y_bot+dock_h,z_face),(wx0,y_bot+dock_h,z_back),(1,0,0),wall_row,wall_col)
        m.add_quad((wx1,y_bot+dock_h,z_face),(wx0,y_bot+dock_h,z_face),(wx0,y_bot+dock_h,z_back),(wx1,y_bot+dock_h,z_back),(0,1,0),wall_row,wall_col)
    # Floor
    m.add_quad((wx0,y_bot,z_face),(wx1,y_bot,z_face),(wx1,y_bot,z_back),(wx0,y_bot,z_back),(0,1,0),wall_row,wall_col)


def _add_sawtooth_bays(m, wall_row, wall_col, roof_row, roof_col,
                       xmin, xmax, y_wall, y_low, y_high, zmin, zmax, n_bays):
    """Sawtooth industrial roof with n_bays parallel ridges."""
    bay_w = (zmax - zmin) / n_bays
    for i in range(n_bays):
        bz0 = zmin + i * bay_w
        bz1 = bz0 + bay_w
        # Slope face (low at front, high at back of each bay)
        m.add_quad((xmin,y_high,bz1),(xmax,y_high,bz1),(xmax,y_low,bz0),(xmin,y_low,bz0),(0,1,0.4),roof_row,roof_col)
        # Vertical clerestory face at back of bay
        if i < n_bays - 1:
            m.add_quad((xmin,y_low,bz1),(xmax,y_low,bz1),(xmax,y_high,bz1),(xmin,y_high,bz1),(0,0,1),wall_row,wall_col)
    # Gable ends on left and right
    for bx, nx_val in [(xmin,-1),(xmax,1)]:
        for i in range(n_bays):
            bz0 = zmin + i * bay_w
            bz1 = bz0 + bay_w
            nv = (-1,0,0) if nx_val<0 else (1,0,0)
            m.add_quad(
                (bx,y_wall,bz0),(bx,y_wall,bz1),(bx,y_high,bz1),(bx,y_low,bz0),
                nv, wall_row, wall_col
            )


def _fill_to_budget(m, wall_row, wall_col,
                    xmin, xmax, ymin, ymax, zmin, zmax,
                    target_tris, normal_sign_z_front=-1):
    """
    Add dense facade detail strips to bring total tri count close to target_tris.
    Distributes detail across all 4 vertical faces (front, back, left, right).
    Each strip box = 8 tris (4 wall faces * 2 tris each, walls_only=True).
    """
    current = m.tri_count()
    if current >= target_tris:
        return
    needed = target_tris - current
    # 4 faces, 8 tris per strip
    strips_per_face = max(1, needed // (4 * 8))
    n_h = max(1, int(strips_per_face * 0.62))
    n_v = max(1, int(strips_per_face * 0.38))
    # Front face (Z=zmin)
    _add_dense_facade_detail(m, wall_row, wall_col,
                             xmin, xmax, ymin, ymax, zmin,
                             n_horiz_strips=n_h, n_vert_strips=n_v,
                             normal_sign_z=-1)
    # Back face (Z=zmax)
    _add_dense_facade_detail(m, wall_row, wall_col,
                             xmin, xmax, ymin, ymax, zmax,
                             n_horiz_strips=n_h, n_vert_strips=n_v,
                             normal_sign_z=1)
    # Left face (X=xmin) — use Z extents as "x" range
    _add_dense_facade_detail(m, wall_row, wall_col,
                             zmin, zmax, ymin, ymax, xmin,
                             n_horiz_strips=n_h, n_vert_strips=n_v,
                             normal_sign_z=-1)
    # Right face (X=xmax)
    _add_dense_facade_detail(m, wall_row, wall_col,
                             zmin, zmax, ymin, ymax, xmax,
                             n_horiz_strips=n_h, n_vert_strips=n_v,
                             normal_sign_z=1)


def _add_curtain_wall_mullions(m, wall_row, wall_col,
                               xmin, xmax, ymin, ymax, z_face,
                               n_vert, n_horiz, mw=0.008, md=0.012, normal_sign_z=-1):
    """Curtain-wall mullion grid: vertical + horizontal strips proud of wall face."""
    # Vertical mullions
    for i in range(n_vert+1):
        bx = xmin + (xmax-xmin) * i / n_vert
        if normal_sign_z < 0:
            m.add_box(bx-mw/2, bx+mw/2, ymin, ymax, z_face-md, z_face, wall_row, wall_col, walls_only=True)
        else:
            m.add_box(bx-mw/2, bx+mw/2, ymin, ymax, z_face, z_face+md, wall_row, wall_col, walls_only=True)
    # Horizontal transoms
    for j in range(n_horiz+1):
        by = ymin + (ymax-ymin) * j / n_horiz
        if normal_sign_z < 0:
            m.add_box(xmin, xmax, by-mw/2, by+mw/2, z_face-md, z_face, wall_row, wall_col, walls_only=True)
        else:
            m.add_box(xmin, xmax, by-mw/2, by+mw/2, z_face, z_face+md, wall_row, wall_col, walls_only=True)


# ---------------------------------------------------------------------------
# NEW HELPER FUNCTIONS (added for spec-compliant per-variant geometry)
# ---------------------------------------------------------------------------

def _add_sawtooth_roof(m, wall_row, wall_col, roof_row, roof_col,
                       xmin, xmax, y_base, y_high, zmin, zmax, n_ridges):
    """
    Sawtooth roof: n_ridges asymmetric triangular prisms. Each ridge is a
    right-triangular prism with a steep north-light face and a shallow slope.
    The ridge axis runs along X; ridges repeat along Z.
    """
    bay_w = (zmax - zmin) / n_ridges
    for i in range(n_ridges):
        bz0 = zmin + i * bay_w
        bz1 = bz0 + bay_w
        bz_mid = bz0 + bay_w * 0.85  # ridge peak near back of bay
        # Sloped south-pitch face (wide, ~30 deg) from bz0→bz_mid
        m.add_quad((xmin, y_base, bz0), (xmax, y_base, bz0),
                   (xmax, y_high, bz_mid), (xmin, y_high, bz_mid),
                   (0, 0.87, -0.5), roof_row, roof_col)
        # Steep north-light face (nearly vertical, ~80 deg) from bz_mid→bz1
        m.add_quad((xmin, y_high, bz_mid), (xmax, y_high, bz_mid),
                   (xmax, y_base, bz1), (xmin, y_base, bz1),
                   (0, 0.17, 0.98), wall_row, wall_col)
        # Left gable triangle
        m.add_quad((xmin, y_base, bz0), (xmin, y_high, bz_mid),
                   (xmin, y_high, bz_mid), (xmin, y_base, bz1),
                   (-1, 0, 0), wall_row, wall_col)
        # Right gable triangle
        m.add_quad((xmax, y_base, bz1), (xmax, y_high, bz_mid),
                   (xmax, y_high, bz_mid), (xmax, y_base, bz0),
                   (1, 0, 0), wall_row, wall_col)


def _add_cylinder(m, wall_row, wall_col, cx, cz, y_base, y_top, radius, n_sides=8):
    """Approximate cylinder as n_sides-sided prism. Walls only (no caps)."""
    for i in range(n_sides):
        a0 = 2 * math.pi * i / n_sides
        a1 = 2 * math.pi * (i + 1) / n_sides
        x0 = cx + radius * math.sin(a0)
        z0 = cz + radius * math.cos(a0)
        x1 = cx + radius * math.sin(a1)
        z1 = cz + radius * math.cos(a1)
        nx = math.sin((a0 + a1) * 0.5)
        nz = math.cos((a0 + a1) * 0.5)
        m.add_quad((x0, y_base, z0), (x1, y_base, z1),
                   (x1, y_top, z1), (x0, y_top, z0),
                   (nx, 0, nz), wall_row, wall_col)


def _add_cylinder_cap(m, wall_row, wall_col, cx, cz, y, radius, n_sides=8, face_up=True):
    """Flat cap (disk) for a cylinder."""
    ny = 1.0 if face_up else -1.0
    center_v = Vertex(cx, y, cz, 0, ny, 0, *atlas_uv(wall_row, wall_col, 0.5, 0.5))
    base_idx = len(m.verts)
    m.verts.append(center_v)
    for i in range(n_sides):
        a0 = 2 * math.pi * i / n_sides
        a1 = 2 * math.pi * (i + 1) / n_sides
        x0 = cx + radius * math.sin(a0); z0 = cz + radius * math.cos(a0)
        x1 = cx + radius * math.sin(a1); z1 = cz + radius * math.cos(a1)
        u0, v0 = atlas_uv(wall_row, wall_col, 0.5 + 0.5 * math.sin(a0), 0.5 + 0.5 * math.cos(a0))
        u1, v1 = atlas_uv(wall_row, wall_col, 0.5 + 0.5 * math.sin(a1), 0.5 + 0.5 * math.cos(a1))
        vi = Vertex(x0, y, z0, 0, ny, 0, u0, v0)
        vj = Vertex(x1, y, z1, 0, ny, 0, u1, v1)
        ei = len(m.verts); m.verts.append(vi)
        ej = len(m.verts); m.verts.append(vj)
        if face_up:
            m.tris.append((base_idx, ei, ej))
        else:
            m.tris.append((base_idx, ej, ei))


def _add_balcony_slabs(m, wall_row, wall_col, x0, x1, y_base, y_top, floor_h, depth):
    """Add thin projecting balcony slab at every floor between y_base and y_top."""
    t = 0.02  # slab thickness
    fl = 1
    y = y_base + fl * floor_h
    while y <= y_top + 0.001:
        m.add_box(x0, x1, y - t, y, -abs(depth) - 0.001, 0.001,
                  wall_row, wall_col, walls_only=False)
        fl += 1
        y = y_base + fl * floor_h


def _add_spire(m, wall_row, wall_col, cx, cz, y_base, y_top, base_hw, n_sides=4):
    """Tapered spire: n_sides-sided pyramid from base square to apex."""
    apex = (cx, y_top, cz)
    for i in range(n_sides):
        a0 = 2 * math.pi * i / n_sides + math.pi / n_sides
        a1 = 2 * math.pi * (i + 1) / n_sides + math.pi / n_sides
        x0 = cx + base_hw * math.sin(a0); z0 = cz + base_hw * math.cos(a0)
        x1 = cx + base_hw * math.sin(a1); z1 = cz + base_hw * math.cos(a1)
        nx = math.sin((a0 + a1) * 0.5); nz = math.cos((a0 + a1) * 0.5)
        m.add_quad((x0, y_base, z0), (x1, y_base, z1), apex, apex,
                   (nx, 0.5, nz), wall_row, wall_col)


def _add_columns(m, wall_row, wall_col, x0, x1, y_base, y_top, z_face,
                 n_cols, col_w=0.04, col_d=0.04):
    """Row of vertical column boxes proud of wall face (toward -Z)."""
    span = x1 - x0
    step = span / n_cols
    for i in range(n_cols):
        cx = x0 + step * (i + 0.5)
        m.add_box(cx - col_w / 2, cx + col_w / 2, y_base, y_top,
                  z_face - col_d, z_face, wall_row, wall_col, walls_only=True)


def _add_fence_posts(m, wall_row, wall_col, x0, x1, y_base, y_top, z_val, n_posts,
                     post_w=0.015, post_d=0.015):
    """Row of thin vertical fence posts at Z=z_val."""
    if n_posts < 2:
        return
    step = (x1 - x0) / (n_posts - 1)
    for i in range(n_posts):
        px = x0 + i * step
        m.add_box(px - post_w / 2, px + post_w / 2, y_base, y_top,
                  z_val - post_d / 2, z_val + post_d / 2, wall_row, wall_col)


def _add_dormer(m, wall_row, wall_col, roof_row, roof_col,
                cx, z_face, y_base, dormer_w=0.12, dormer_h=0.14, dormer_d=0.10):
    """Small dormer window box projecting from a roof slope face."""
    m.add_box(cx - dormer_w / 2, cx + dormer_w / 2,
              y_base, y_base + dormer_h,
              z_face - dormer_d, z_face,
              wall_row, wall_col, roof_row, roof_col)
    # Gabled dormer roof
    _add_gabled_roof(m, wall_row, wall_col, roof_row, roof_col,
                     cx - dormer_w / 2, cx + dormer_w / 2,
                     y_base + dormer_h, dormer_h * 0.5,
                     z_face - dormer_d, z_face)


#!/usr/bin/env python3
"""
New geometry builder functions for generate_b3d_models.py.
This file is a staging area -- its content replaces lines 961..2697 in the main script.

Scale: 0.1 model units = 1 meter.
Coordinate system: +X right, +Y up, +Z forward. Pivot at base centre (Y=0).
"""

# ---------------------------------------------------------------------------
# RESIDENTIAL LOW  (small residential, 2-3 story, 2000-3000 tris LOD0)
# ---------------------------------------------------------------------------

def _build_res_low(zone, tier, variant, lod):
    """Residential low tier -- 4 distinct house forms."""
    wr, wc = WALL_CELLS[(zone, tier, variant)]
    rr, rc = ROOF_CELL
    m = MeshAccum()

    S = 1.0
    fh = FOOTPRINT_HALF[tier]

    if variant == "01":
        # Detached house: box + gabled roof + chimney box + porch slab
        # Phase-11d: flat-roof block, no garden, tarmac forecourt
        bw, bd, bh = 8*S, 10*S, 6*S
        hx, hz = bw/2, bd/2
        ridge_h = (10 - 6) * S
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            _add_gabled_roof(m, wr, wc, rr, rc, -hx, hx, bh, ridge_h, -hz, hz)
            # Ground quad ±5 m covers the full 10×10 m tile at native scale (setScale=1).
            _add_ground_quad(m, "garden", -fh, fh, -fh, fh)
            return m.to_b3d()
        # LOD0
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        _add_gabled_roof(m, wr, wc, rr, rc, -hx, hx, bh, ridge_h, -hz, hz)
        # Chimney box
        chw = 0.8*S/2
        _add_chimney(m, wr, wc, rr, rc, 0.0, 0.0, bh + ridge_h*0.5, 2*S, hw=chw)
        # Entrance canopy/step removed: building front (hz=5 m) is at the tile edge
        # (tile half-extent = 5 m at native scale), leaving no space to extend outward.
        # Ground quad ±5 m covers the full 10×10 m tile at native scale.
        _add_ground_quad(m, "garden", -fh, fh, -fh, fh)
        return m.to_b3d()

    elif variant == "02":
        # Semi-detached pair: two unit boxes + two hipped roofs
        # Matches res_low_01 footprint: total_w=8S, depth=10S, height=6S
        unit_w, unit_d, unit_h = 4*S, 10*S, 6*S
        total_w = 8*S
        hx, hz = total_w/2, unit_d/2
        ridge_h_val = 3*S
        dr, dc = RES_LOW_02_DOOR_CELL  # door cell for left unit front face only

        def _add_left_unit(mesh):
            """Left unit: door on front face, plain wall (wr,wc) on all other faces."""
            xn, xp = -hx, 0.0
            yn, yp = 0.0, unit_h
            zn, zp = -hz, hz
            # Front face (normal -Z): door cell
            v, t = make_quad((xn,yn,zn),(xp,yn,zn),(xp,yp,zn),(xn,yp,zn),(0,0,-1),dr,dc)
            mesh.add(v, t)
            # Back face (normal +Z)
            v, t = make_quad((xp,yn,zp),(xn,yn,zp),(xn,yp,zp),(xp,yp,zp),(0,0,1),wr,wc)
            mesh.add(v, t)
            # Left face (normal -X)
            v, t = make_quad((xn,yn,zp),(xn,yn,zn),(xn,yp,zn),(xn,yp,zp),(-1,0,0),wr,wc)
            mesh.add(v, t)
            # Right face / party wall (normal +X)
            v, t = make_quad((xp,yn,zn),(xp,yn,zp),(xp,yp,zp),(xp,yp,zn),(1,0,0),wr,wc)
            mesh.add(v, t)
            # Top face (roof)
            v, t = make_quad((xn,yp,zn),(xp,yp,zn),(xp,yp,zp),(xn,yp,zp),(0,1,0),rr,rc)
            mesh.add(v, t)

        if lod == 1:
            _add_left_unit(m)
            m.add_box(0, hx, 0, unit_h, -hz, hz, wr, wc, rr, rc)
            _add_hipped_roof(m, wr, wc, rr, rc, -hx, 0, unit_h, ridge_h_val, -hz, hz)
            _add_hipped_roof(m, wr, wc, rr, rc, 0, hx, unit_h, ridge_h_val, -hz, hz)
            _add_ground_quad(m, "garden", -fh, fh, -fh, fh)
            return m.to_b3d()
        # LOD0
        _add_left_unit(m)
        m.add_box(0, hx, 0, unit_h, -hz, hz, wr, wc, rr, rc)
        _add_hipped_roof(m, wr, wc, rr, rc, -hx, 0, unit_h, ridge_h_val, -hz, hz)
        _add_hipped_roof(m, wr, wc, rr, rc, 0, hx, unit_h, ridge_h_val, -hz, hz)
        _add_ground_quad(m, "garden", -fh, fh, -fh, fh)
        return m.to_b3d()

    elif variant == "03":
        # Cottage: single box + mono-pitch roof + chimney + door on front face only
        # Matches res_low_01/02 footprint: 8S wide, 10S deep, 6S tall
        bw, bd, bh = 8*S, 10*S, 6*S
        hx, hz = bw/2, bd/2
        dr, dc = RES_LOW_03_DOOR_CELL

        def _add_cottage_box(mesh):
            """Box with door cell on front face, plain wall on other faces."""
            xn, xp = -hx, hx
            yn, yp = 0.0, bh
            zn, zp = -hz, hz
            # Front face: door cell
            v, t = make_quad((xn,yn,zn),(xp,yn,zn),(xp,yp,zn),(xn,yp,zn),(0,0,-1),dr,dc)
            mesh.add(v, t)
            # Back face
            v, t = make_quad((xp,yn,zp),(xn,yn,zp),(xn,yp,zp),(xp,yp,zp),(0,0,1),wr,wc)
            mesh.add(v, t)
            # Left face
            v, t = make_quad((xn,yn,zp),(xn,yn,zn),(xn,yp,zn),(xn,yp,zp),(-1,0,0),wr,wc)
            mesh.add(v, t)
            # Right face
            v, t = make_quad((xp,yn,zn),(xp,yn,zp),(xp,yp,zp),(xp,yp,zn),(1,0,0),wr,wc)
            mesh.add(v, t)
            # Top face
            v, t = make_quad((xn,yp,zn),(xp,yp,zn),(xp,yp,zp),(xn,yp,zp),(0,1,0),rr,rc)
            mesh.add(v, t)

        if lod == 1:
            _add_cottage_box(m)
            _add_mono_pitch_roof(m, wr, wc, rr, rc, -hx, hx, bh, bh+2*S, -hz, hz)
            _add_ground_quad(m, "garden", -fh, fh, -fh, fh)
            return m.to_b3d()
        # LOD0
        _add_cottage_box(m)
        _add_mono_pitch_roof(m, wr, wc, rr, rc, -hx, hx, bh, bh+2*S, -hz, hz)
        chw = 0.8*S/2
        _add_chimney(m, wr, wc, rr, rc, -2*S, 0.0, bh + 0.5*S, 2*S, hw=chw)
        _add_ground_quad(m, "garden", -fh, fh, -fh, fh)
        return m.to_b3d()

    elif variant == "04":
        # Bungalow: box + low hipped roof
        # Phase-11d: red-brick, low brick boundary wall at plot edge, no garden
        # Depth reduced to 10S (was 12S) so the building fills but does not exceed
        # the 10×10 m tile at native scale (tile half-extent = 5 m).
        # Veranda removed: building already fills the full tile depth.
        bw, bd, bh = 8*S, 8*S, 3*S
        hx, hz = bw/2, bd/2
        ridge_h = (5-3)*S
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            _add_hipped_roof(m, wr, wc, rr, rc, -hx, hx, bh, ridge_h, -hz, hz)
            _add_ground_quad(m, "garden", -fh, fh, -fh, fh)
            return m.to_b3d()
        # LOD0
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        _add_hipped_roof(m, wr, wc, rr, rc, -hx, hx, bh, ridge_h, -hz, hz)
        _add_ground_quad(m, "garden", -fh, fh, -fh, fh)
        return m.to_b3d()

    return m.to_b3d()


# ---------------------------------------------------------------------------
# RESIDENTIAL MED  (medium residential, 3-5 story, 3000-5000 tris LOD0)
# ---------------------------------------------------------------------------

def _build_res_med(zone, tier, variant, lod):
    """Residential med tier -- 4 distinct mid-rise forms."""
    wr, wc = WALL_CELLS[(zone, tier, variant)]
    rr, rc = ROOF_CELL
    m = MeshAccum()
    S = 1.0
    fh = FOOTPRINT_HALF[tier]

    if variant == "01":
        # Box + flat roof (add_box to parapet height)
        # Phase-11d: 2-storey block, tarmac apron
        bw, bd, bh = 16*S, 12*S, 12*S
        hx, hz = bw/2, bd/2
        par_h = 0.5*S
        if lod == 1:
            m.add_box(-hx, hx, 0, bh+par_h, -hz, hz, wr, wc, rr, rc)
            _add_ground_quad(m, "garden", -fh, fh, -fh, fh)
            return m.to_b3d()
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        _add_parapet(m, wr, wc, -hx, hx, bh, -hz, hz, pw=0.03, ph=par_h*0.1)
        _add_ground_quad(m, "garden", -fh, fh, -fh, fh)
        return m.to_b3d()

    elif variant == "02":
        # Four unit boxes + alternating gabled roofs
        # Phase-11d: 2-storey villa, kidney-pool geometry in garden
        unit_w, unit_d, unit_h = 4*S, 10*S, 10*S
        total_w = 16*S
        hx, hz = total_w/2, unit_d/2
        if lod == 1:
            m.add_box(-hx, hx, 0, unit_h, -hz, hz, wr, wc, rr, rc)
            for i in range(4):
                x0 = -hx + i*unit_w
                x1 = x0 + unit_w
                rh = 1.0*S if (i%2==0) else 0.8*S
                _add_gabled_roof(m, wr, wc, rr, rc, x0, x1, unit_h, rh, -hz, hz)
            _add_ground_quad(m, "garden", -fh, fh, -fh, fh)
            return m.to_b3d()
        # LOD0: four units with staggered setbacks
        for i in range(4):
            x0 = -hx + i*unit_w
            x1 = x0 + unit_w
            setback = 0.5*S if (i%2==0) else 0.0
            m.add_box(x0, x1, 0, unit_h, -hz-setback, hz, wr, wc, rr, rc)
            rh = 1.0*S if (i%2==0) else 0.8*S
            _add_gabled_roof(m, wr, wc, rr, rc, x0, x1, unit_h, rh, -hz-setback, hz)
        _add_ground_quad(m, "garden", -fh, fh, -fh, fh)
        # Kidney pool in rear garden (LOD0 only)
        _add_ground_quad(m, "pool", -2*S, 2*S, -hz-1*S-4*S, -hz-1*S)
        return m.to_b3d()

    elif variant == "03":
        # L-shaped plan (two boxes forming L) + flat roofs
        h = 9*S
        if lod == 1:
            m.add_box(-7*S, 7*S, 0, h, -6*S, 6*S, wr, wc, rr, rc)
            _add_ground_quad(m, "garden", -fh, fh, -fh, fh)
            return m.to_b3d()
        # L-shape: main block + leg
        m.add_box(-7*S, 7*S, 0, h, -6*S, 0, wr, wc, rr, rc)
        m.add_box(-7*S, -1*S, 0, h, 0, 6*S, wr, wc, rr, rc)
        _add_ground_quad(m, "garden", -fh, fh, -fh, fh)
        return m.to_b3d()

    elif variant == "04":
        # Box + mansard roof sides
        bw, bd, bh = 18*S, 14*S, 10*S
        hx, hz = bw/2, bd/2
        mansard_h = 3*S
        total_h = bh + mansard_h
        if lod == 1:
            m.add_box(-hx, hx, 0, total_h, -hz, hz, wr, wc, rr, rc)
            _add_ground_quad(m, "garden", -fh, fh, -fh, fh)
            return m.to_b3d()
        # LOD0: main body
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        # Mansard roof: steep sides + flat top
        inset = 1.0*S
        m.add_quad((-hx, bh, -hz), (hx, bh, -hz),
                   (hx-inset, total_h, -hz+inset), (-hx+inset, total_h, -hz+inset),
                   (0, 0.3, -0.95), wr, wc)
        m.add_quad((hx, bh, hz), (-hx, bh, hz),
                   (-hx+inset, total_h, hz-inset), (hx-inset, total_h, hz-inset),
                   (0, 0.3, 0.95), wr, wc)
        m.add_quad((-hx, bh, hz), (-hx, bh, -hz),
                   (-hx+inset, total_h, -hz+inset), (-hx+inset, total_h, hz-inset),
                   (-0.95, 0.3, 0), wr, wc)
        m.add_quad((hx, bh, -hz), (hx, bh, hz),
                   (hx-inset, total_h, hz-inset), (hx-inset, total_h, -hz+inset),
                   (0.95, 0.3, 0), wr, wc)
        # Flat top
        m.add_quad((-hx+inset, total_h, -hz+inset), (hx-inset, total_h, -hz+inset),
                   (hx-inset, total_h, hz-inset), (-hx+inset, total_h, hz-inset),
                   (0, 1, 0), rr, rc)
        _add_ground_quad(m, "garden", -fh, fh, -fh, fh)
        return m.to_b3d()

    return m.to_b3d()


# ---------------------------------------------------------------------------
# RESIDENTIAL HIGH  (tall residential, 8-15 story, 6000-10000 tris LOD0)
# ---------------------------------------------------------------------------

def _build_res_high(zone, tier, variant, lod):
    """Residential high -- 4 distinct tower forms."""
    wr, wc = WALL_CELLS[(zone, tier, variant)]
    rr, rc = ROOF_CELL
    m = MeshAccum()
    S = 1.0
    fh = FOOTPRINT_HALF[tier]
    floor_h = 3*S

    if variant == "01":
        # Tall shaft box + flat roof + balcony slabs every 3 floors
        # Phase-11d: flat-roof concrete, AC condensers on parapet, no pool
        # Footprint clamped to ±4*S (0.4 units) to stay within BUILDING_HALF_XZ
        bw, bd, bh = 8*S, 8*S, 40*S
        hx, hz = bw/2, bd/2
        floors = 13
        if lod == 2:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            _add_ground_quad(m, "garden", -fh, fh, -fh, fh)
            return m.to_b3d()
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            m.add_box(-2*S, 2*S, bh, bh+3*S, -2*S, 2*S, wr, wc, rr, rc)
            _add_ground_quad(m, "garden", -fh, fh, -fh, fh)
            return m.to_b3d()
        # LOD0
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        # Plant room box
        m.add_box(-2*S, 2*S, bh, bh+3*S, -2*S, 2*S, wr, wc, rr, rc)
        # Balcony slabs every 3 floors — overhang clamped so tip stays within ±4*S
        for fl in range(3, floors, 3):
            fy = fl * floor_h
            _add_balcony_slab(m, wr, wc, -3*S, 3*S, fy, -hz, 0.8*S)
        _add_ground_quad(m, "garden", -fh, fh, -fh, fh)
        return m.to_b3d()

    elif variant == "02":
        # Slab box + flat roof
        # Phase-11d: stepped-setback, pool basin geometry in walled courtyard
        # Footprint clamped: main slab 8*S wide, staircases tucked to ±4*S total
        bw, bd, bh = 8*S, 8*S, 30*S
        hx, hz = bw/2, bd/2
        if lod == 2:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            _add_ground_quad(m, "garden", -fh, fh, -fh, fh)
            return m.to_b3d()
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            # Staircase projections stay within ±4*S: offset kept within hx
            m.add_box(-hx, -hx+1*S, 0, bh, -1*S, 1*S, wr, wc, rr, rc)
            m.add_box(hx-1*S, hx, 0, bh, -1*S, 1*S, wr, wc, rr, rc)
            _add_ground_quad(m, "garden", -fh, fh, -fh, fh)
            return m.to_b3d()
        # LOD0
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        # End staircase boxes — inset so total stays within ±4*S
        m.add_box(-hx, -hx+1*S, 0, bh, -1*S, 1*S, wr, wc, rr, rc)
        m.add_box(hx-1*S, hx, 0, bh, -1*S, 1*S, wr, wc, rr, rc)
        _add_ground_quad(m, "garden", -fh, fh, -fh, fh)
        # Pool basin in walled courtyard (LOD0 only)
        _add_ground_quad(m, "pool", -2*S, 2*S, -hz-1*S-4*S, -hz-1*S)
        return m.to_b3d()

    elif variant == "03":
        # Stepped shaft (3 stacked boxes decreasing in plan) + antenna box
        # Phase-11d: full-height curtain-wall tower, cantilevered balconies
        # Footprint clamped: base_hw reduced to 4*S to stay within BUILDING_HALF_XZ
        base_hw = 4*S
        bh = 36*S
        sb1_y, sb2_y = 15*S, 25*S
        sb = 2*S
        if lod == 2:
            m.add_box(-base_hw, base_hw, 0, bh, -base_hw, base_hw, wr, wc, rr, rc)
            _add_ground_quad(m, "garden", -fh, fh, -fh, fh)
            return m.to_b3d()
        if lod == 1:
            m.add_box(-base_hw, base_hw, 0, sb1_y, -base_hw, base_hw, wr, wc, rr, rc)
            m.add_box(-base_hw+sb, base_hw-sb, sb1_y, sb2_y, -base_hw+sb, base_hw-sb, wr, wc, rr, rc)
            m.add_box(-base_hw+2*sb, base_hw-2*sb, sb2_y, bh, -base_hw+2*sb, base_hw-2*sb, wr, wc, rr, rc)
            _add_ground_quad(m, "garden", -fh, fh, -fh, fh)
            return m.to_b3d()
        # LOD0: three stacked boxes
        m.add_box(-base_hw, base_hw, 0, sb1_y, -base_hw, base_hw, wr, wc, rr, rc)
        hw1 = base_hw - sb
        m.add_box(-hw1, hw1, sb1_y, sb2_y, -hw1, hw1, wr, wc, rr, rc)
        hw2 = base_hw - 2*sb
        m.add_box(-hw2, hw2, sb2_y, bh, -hw2, hw2, wr, wc, rr, rc)
        # Antenna box
        m.add_box(-0.5*S, 0.5*S, bh, bh+4*S, -0.5*S, 0.5*S, wr, wc)
        _add_ground_quad(m, "garden", -fh, fh, -fh, fh)
        return m.to_b3d()

    elif variant == "04":
        # Slab box + flat roof (curved front as approximation)
        # Footprint clamped: bw reduced to 8*S, bow reduced to 0.4*S so tip = hz+bow ≤ 4*S
        bw, bd, bh = 8*S, 7*S, 28*S
        hx, hz = bw/2, bd/2
        bow = 0.4*S
        n_seg = 8
        if lod == 2:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            _add_ground_quad(m, "garden", -fh, fh, -fh, fh)
            return m.to_b3d()
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            _add_ground_quad(m, "garden", -fh, fh, -fh, fh)
            return m.to_b3d()
        # LOD0: main box + curved front face segments
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        # Curved front face: 8-segment barrel
        for i in range(n_seg):
            frac0 = i / n_seg
            frac1 = (i+1) / n_seg
            x0 = -hx + bw * frac0
            x1 = -hx + bw * frac1
            z0 = -hz - bow * math.sin(frac0 * math.pi)
            z1 = -hz - bow * math.sin(frac1 * math.pi)
            m.add_quad((x0, 0, z0), (x1, 0, z1), (x1, bh, z1), (x0, bh, z0),
                       (0, 0, -1.0), wr, wc)
        _add_ground_quad(m, "garden", -fh, fh, -fh, fh)
        return m.to_b3d()

    return m.to_b3d()


# ---------------------------------------------------------------------------
# COMMERCIAL LOW  (small commercial, 1-2 story, 2000-3000 tris LOD0)
# ---------------------------------------------------------------------------

def _build_com_low(zone, tier, variant, lod):
    """Commercial low -- 4 distinct shop forms."""
    wr, wc = WALL_CELLS[(zone, tier, variant)]
    rr, rc = ROOF_CELL
    m = MeshAccum()
    S = 1.0
    fh = FOOTPRINT_HALF[tier]

    if variant == "01":
        # Box + flat roof + canopy slab over entrance
        # Phase-11d: convenience store, 3-bay parking apron
        bw, bd, bh = 8*S, 6*S, 4*S
        hx, hz = bw/2, bd/2
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            _add_ground_quad(m, "paving", -fh, fh, -fh, fh)
            return m.to_b3d()
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        # Canopy slab
        can_w, can_d = 4*S, 2*S
        m.add_box(-can_w/2, can_w/2, 3*S, 3*S+0.2*S, -hz-can_d, -hz, wr, wc)
        _add_ground_quad(m, "paving", -fh, fh, -fh, fh)
        return m.to_b3d()

    elif variant == "02":
        # Box + flat roof (plain box)
        bw, bd, bh = 8*S, 8*S, 4*S
        hx, hz = bw/2, bd/2
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            _add_ground_quad(m, "paving", -fh, fh, -fh, fh)
            return m.to_b3d()
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        _add_ground_quad(m, "paving", -fh, fh, -fh, fh)
        return m.to_b3d()

    elif variant == "03":
        # Box + flat roof + 4 simple pilaster boxes on front
        # Phase-11d: auto garage, open forecourt, no awning
        # Footprint clamped: bw reduced to 8*S so hx = 4*S ≤ BUILDING_HALF_XZ
        bw, bd, bh = 8*S, 6*S, 5*S
        hx, hz = bw/2, bd/2
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            _add_ground_quad(m, "paving", -fh, fh, -fh, fh)
            return m.to_b3d()
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        # 4 simple thin pilaster boxes on front only
        for i in range(4):
            px = -hx + bw * (i + 0.5) / 4
            m.add_box(px-0.1*S, px+0.1*S, 0, bh, -hz-0.2*S, -hz, wr, wc, walls_only=True)
        _add_ground_quad(m, "paving", -fh, fh, -fh, fh)
        return m.to_b3d()

    elif variant == "04":
        # Box + sawtooth roof
        # Phase-11d: supermarket, freestanding trolley-bay shelter in parking apron
        # Footprint clamped: bw reduced to 8*S so hx = 4*S ≤ BUILDING_HALF_XZ
        bw, bd, bh = 8*S, 8*S, 4*S
        hx, hz = bw/2, bd/2
        tooth_h = 2.5*S
        if lod == 1:
            m.add_box(-hx, hx, 0, bh+tooth_h*0.5, -hz, hz, wr, wc, rr, rc)
            _add_ground_quad(m, "paving", -fh, fh, -fh, fh)
            return m.to_b3d()
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        _add_sawtooth_roof(m, wr, wc, rr, rc, -hx, hx, bh, bh+tooth_h, -hz, hz, 4)
        _add_ground_quad(m, "paving", -fh, fh, -fh, fh)
        return m.to_b3d()

    return m.to_b3d()


# ---------------------------------------------------------------------------
# COMMERCIAL MED  (medium commercial, 3-6 story, 3000-5000 tris LOD0)
# ---------------------------------------------------------------------------

def _build_com_med(zone, tier, variant, lod):
    """Commercial med -- 4 distinct mid-rise office forms."""
    wr, wc = WALL_CELLS[(zone, tier, variant)]
    rr, rc = ROOF_CELL
    m = MeshAccum()
    S = 1.0
    fh = FOOTPRINT_HALF[tier]

    if variant == "01":
        # Box + smaller set-back top box
        # Phase-11d: strip mall, large parking apron with bay markings
        bw, bd, bh = 20*S, 16*S, 15*S
        hx, hz = bw/2, bd/2
        top_h = 18*S
        sb = 1*S
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            m.add_box(-hx+sb, hx-sb, bh, top_h, -hz+sb, hz-sb, wr, wc, rr, rc)
            _add_ground_quad(m, "paving", -fh, fh, -fh, fh)
            return m.to_b3d()
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        m.add_box(-hx+sb, hx-sb, bh, top_h, -hz+sb, hz-sb, wr, wc, rr, rc)
        _add_ground_quad(m, "paving", -fh, fh, -fh, fh)
        return m.to_b3d()

    elif variant == "02":
        # Wide podium box + narrower tower box on top
        pod_w, pod_d, pod_h = 20*S, 16*S, 6*S
        tow_w, tow_d, tow_h = 10*S, 10*S, 24*S
        hx, hz = pod_w/2, pod_d/2
        if lod == 1:
            m.add_box(-hx, hx, 0, pod_h, -hz, hz, wr, wc, rr, rc)
            m.add_box(-tow_w/2, tow_w/2, pod_h, pod_h+tow_h, -tow_d/2, tow_d/2, wr, wc, rr, rc)
            _add_ground_quad(m, "paving", -fh, fh, -fh, fh)
            return m.to_b3d()
        m.add_box(-hx, hx, 0, pod_h, -hz, hz, wr, wc, rr, rc)
        m.add_box(-tow_w/2, tow_w/2, pod_h, pod_h+tow_h, -tow_d/2, tow_d/2, wr, wc, rr, rc)
        _add_ground_quad(m, "paving", -fh, fh, -fh, fh)
        return m.to_b3d()

    elif variant == "03":
        # Box + flat roof (atrium as shallow box recess in front face)
        bw, bd, bh = 18*S, 16*S, 15*S
        hx, hz = bw/2, bd/2
        atrium_w, atrium_d = 8*S, 4*S
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            _add_ground_quad(m, "paving", -fh, fh, -fh, fh)
            return m.to_b3d()
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        # Atrium slot recess box
        m.add_box(-atrium_w/2, atrium_w/2, 0, bh, -hz, -hz+atrium_d, wr, wc, rr, rc)
        _add_ground_quad(m, "paving", -fh, fh, -fh, fh)
        return m.to_b3d()

    elif variant == "04":
        # Box + flat roof (arcade = 6 simple arch boxes on ground floor)
        bw, bd, bh = 20*S, 12*S, 12*S
        hx, hz = bw/2, bd/2
        arcade_h = 3*S
        n_arches = 6
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            _add_ground_quad(m, "paving", -fh, fh, -fh, fh)
            return m.to_b3d()
        # Upper floors solid
        m.add_box(-hx, hx, arcade_h, bh, -hz, hz, wr, wc, rr, rc)
        # Ground floor: 6 simple arch column boxes
        for i in range(n_arches):
            cx = -hx + bw * (i + 0.5) / n_arches
            m.add_box(cx-0.15*S, cx+0.15*S, 0, arcade_h, -hz-0.3*S, -hz, wr, wc)
        # Ground floor back wall
        m.add_box(-hx, hx, 0, arcade_h, hz-0.1*S, hz, wr, wc, rr, rc)
        _add_ground_quad(m, "paving", -fh, fh, -fh, fh)
        return m.to_b3d()

    return m.to_b3d()


# ---------------------------------------------------------------------------
# COMMERCIAL HIGH  (tall commercial, 10-20 story, 6000-10000 tris LOD0)
# ---------------------------------------------------------------------------

def _build_com_high(zone, tier, variant, lod):
    """Commercial high -- 4 distinct skyscraper forms."""
    wr, wc = WALL_CELLS[(zone, tier, variant)]
    rr, rc = ROOF_CELL
    m = MeshAccum()
    S = 1.0
    fh = FOOTPRINT_HALF[tier]

    if variant == "01":
        # Tapered shaft (main box + two smaller setback boxes at top) -- NO mullion grid
        # Footprint clamped: bw=8*S, bd=8*S so hx=hz=4*S ≤ BUILDING_HALF_XZ
        bw, bd, bh = 8*S, 8*S, 60*S
        hx, hz = bw/2, bd/2
        taper1_y, taper2_y = 48*S, 55*S
        if lod == 2:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            _add_ground_quad(m, "paving", -fh, fh, -fh, fh)
            return m.to_b3d()
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            _add_ground_quad(m, "paving", -fh, fh, -fh, fh)
            return m.to_b3d()
        # LOD0: main shaft + two taper steps
        m.add_box(-hx, hx, 0, taper1_y, -hz, hz, wr, wc, rr, rc)
        sb1 = 1*S
        m.add_box(-hx+sb1, hx-sb1, taper1_y, taper2_y, -hz+sb1, hz-sb1, wr, wc, rr, rc)
        sb2 = 2*S
        m.add_box(-hx+sb2, hx-sb2, taper2_y, bh, -hz+sb2, hz-sb2, wr, wc, rr, rc)
        _add_ground_quad(m, "paving", -fh, fh, -fh, fh)
        return m.to_b3d()

    elif variant == "02":
        # Main box + pediment fins (two triangular prisms at top) + entry canopy slab
        # Footprint clamped: bw=8*S, bd=8*S; canopy depth reduced to stay within ±4*S
        bw, bd, bh = 8*S, 8*S, 42*S
        hx, hz = bw/2, bd/2
        crown_h = 6*S
        pod_h = 8*S
        if lod == 2:
            m.add_box(-hx, hx, 0, bh+crown_h, -hz, hz, wr, wc, rr, rc)
            _add_ground_quad(m, "paving", -fh, fh, -fh, fh)
            return m.to_b3d()
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            _add_ground_quad(m, "paving", -fh, fh, -fh, fh)
            return m.to_b3d()
        # LOD0
        m.add_box(-hx, hx, 0, pod_h, -hz, hz, wr, wc, rr, rc)
        m.add_box(-hx, hx, pod_h, bh, -hz, hz, wr, wc, rr, rc)
        # Two triangular pediment fins
        ped_w = 2*S
        m.add_tri((-ped_w/2, bh, -hz), (ped_w/2, bh, -hz), (0, bh+crown_h, -hz),
                  (0, 0, -1), wr, wc)
        m.add_tri((ped_w/2, bh, hz), (-ped_w/2, bh, hz), (0, bh+crown_h, hz),
                  (0, 0, 1), wr, wc)
        # Entry canopy slab — tip recessed flush with front face, no outboard projection
        m.add_box(-2*S, 2*S, 4*S, 4*S+0.15*S, -hz, -hz+1*S, wr, wc)
        _add_ground_quad(m, "paving", -fh, fh, -fh, fh)
        return m.to_b3d()

    elif variant == "03":
        # Main shaft box + chamfered octagonal top section + spire box
        # Footprint clamped: base_hw reduced to 4*S to stay within BUILDING_HALF_XZ
        base_hw = 4*S
        bh = 55*S
        chamfer_y = 47*S
        n_sides = 8
        if lod == 2:
            m.add_box(-base_hw, base_hw, 0, bh, -base_hw, base_hw, wr, wc, rr, rc)
            _add_ground_quad(m, "paving", -fh, fh, -fh, fh)
            return m.to_b3d()
        if lod == 1:
            m.add_box(-base_hw, base_hw, 0, bh, -base_hw, base_hw, wr, wc, rr, rc)
            m.add_box(-1.5*S, 1.5*S, bh, bh+8*S, -1.5*S, 1.5*S, wr, wc)
            _add_ground_quad(m, "paving", -fh, fh, -fh, fh)
            return m.to_b3d()
        # LOD0: square shaft to chamfer_y
        m.add_box(-base_hw, base_hw, 0, chamfer_y, -base_hw, base_hw, wr, wc, rr, rc)
        # Octagonal top (~8 faces)
        oct_r = base_hw
        for i in range(n_sides):
            a0 = 2*math.pi*i/n_sides + math.pi/n_sides
            a1 = 2*math.pi*(i+1)/n_sides + math.pi/n_sides
            x0 = oct_r * math.sin(a0); z0 = oct_r * math.cos(a0)
            x1 = oct_r * math.sin(a1); z1 = oct_r * math.cos(a1)
            nx = math.sin((a0+a1)*0.5); nz = math.cos((a0+a1)*0.5)
            m.add_quad((x0, chamfer_y, z0), (x1, chamfer_y, z1),
                       (x1, bh, z1), (x0, bh, z0),
                       (nx, 0, nz), wr, wc)
        _add_cylinder_cap(m, rr, rc, 0, 0, bh, oct_r, n_sides=n_sides, face_up=True)
        # Spire box
        _add_spire(m, wr, wc, 0, 0, bh, bh+8*S, 1.5*S, n_sides=4)
        _add_ground_quad(m, "paving", -fh, fh, -fh, fh)
        return m.to_b3d()

    elif variant == "04":
        # Main slab box + 4 corner mega-column boxes + 3 cross-brace slab boxes
        # Footprint clamped: bw=7*S, bd=7*S; columns at hx±0.5*S stay within 4*S
        bw, bd, bh = 7*S, 7*S, 50*S
        hx, hz = bw/2, bd/2
        if lod == 2:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            _add_ground_quad(m, "paving", -fh, fh, -fh, fh)
            return m.to_b3d()
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            for cx, cz in [(-hx,-hz),(-hx,hz),(hx,-hz),(hx,hz)]:
                m.add_box(cx-0.5*S, cx+0.5*S, 0, bh, cz-0.5*S, cz+0.5*S, wr, wc, walls_only=True)
            _add_ground_quad(m, "paving", -fh, fh, -fh, fh)
            return m.to_b3d()
        # LOD0
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        # 4 corner mega-column boxes
        for cx, cz in [(-hx,-hz),(-hx,hz),(hx,-hz),(hx,hz)]:
            m.add_box(cx-0.5*S, cx+0.5*S, 0, bh, cz-0.5*S, cz+0.5*S, wr, wc, walls_only=True)
        # 3 cross-brace slab boxes
        beam_h = 0.5*S
        for band_y in [15*S, 30*S, 45*S]:
            m.add_box(-hx, hx, band_y, band_y+beam_h, -hz-0.2*S, -hz, wr, wc, walls_only=True)
            m.add_box(-hx, hx, band_y, band_y+beam_h, hz, hz+0.2*S, wr, wc, walls_only=True)
            m.add_box(-hx-0.2*S, -hx, band_y, band_y+beam_h, -hz, hz, wr, wc, walls_only=True)
            m.add_box(hx, hx+0.2*S, band_y, band_y+beam_h, -hz, hz, wr, wc, walls_only=True)
        _add_ground_quad(m, "paving", -fh, fh, -fh, fh)
        # Phase-11d: stepped ziggurat — no pool
        return m.to_b3d()

    return m.to_b3d()


# ---------------------------------------------------------------------------
# INDUSTRIAL LOW  (small industrial, 1 story, 2000-3000 tris LOD0)
# ---------------------------------------------------------------------------

def _build_ind_low(zone, tier, variant, lod):
    """Industrial low -- 4 distinct shed forms."""
    wr, wc = WALL_CELLS[(zone, tier, variant)]
    rr, rc = ROOF_CELL
    m = MeshAccum()
    S = 1.0
    fh = FOOTPRINT_HALF[tier]

    if variant == "01":
        # Box + pitched roof (portal frame gabled)
        # Footprint clamped: bd reduced to 8*S so hz = 4*S ≤ BUILDING_HALF_XZ
        bw, bd, bh = 8*S, 8*S, 5*S
        hx, hz = bw/2, bd/2
        ridge_h = (7-5)*S
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            _add_gabled_roof(m, wr, wc, rr, rc, -hx, hx, bh, ridge_h, -hz, hz)
            _add_ground_quad(m, "tarmac", -fh, fh, -fh, fh)
            return m.to_b3d()
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        _add_gabled_roof(m, wr, wc, rr, rc, -hx, hx, bh, ridge_h, -hz, hz)
        _add_ground_quad(m, "tarmac", -fh, fh, -fh, fh)
        return m.to_b3d()

    elif variant == "02":
        # Box + twin parallel pitched roofs
        # Footprint clamped: bw=8*S, bd=8*S so hx=hz=4*S ≤ BUILDING_HALF_XZ
        bw, bd, bh = 8*S, 8*S, 5*S
        hx, hz = bw/2, bd/2
        ridge_h = (7-5)*S
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            _add_gabled_roof(m, wr, wc, rr, rc, -hx, 0, bh, ridge_h, -hz, hz)
            _add_gabled_roof(m, wr, wc, rr, rc, 0, hx, bh, ridge_h, -hz, hz)
            _add_ground_quad(m, "tarmac", -fh, fh, -fh, fh)
            return m.to_b3d()
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        _add_gabled_roof(m, wr, wc, rr, rc, -hx, 0, bh, ridge_h, -hz, hz)
        _add_gabled_roof(m, wr, wc, rr, rc, 0, hx, bh, ridge_h, -hz, hz)
        _add_ground_quad(m, "tarmac", -fh, fh, -fh, fh)
        return m.to_b3d()

    elif variant == "03":
        # Main box + lean-to box + mono-pitch roofs on each
        # Footprint clamped: combined footprint centred within ±4*S.
        # Main box 5*S wide, lean-to 3*S wide → total 8*S, centred: main [-4*S,+1*S], lean-to [+1*S,+4*S]
        mw, md, mh = 5*S, 8*S, 6*S
        ridge_h = (8-6)*S
        lw, lh_top, lh_bot = 3*S, 5*S, 4*S
        x_main_min = -4*S
        hx_main = x_main_min + mw  # = -4*S + 5*S = +1*S (join line)
        hz = md/2
        if lod == 1:
            m.add_box(x_main_min, hx_main, 0, mh, -hz, hz, wr, wc, rr, rc)
            _add_gabled_roof(m, wr, wc, rr, rc, x_main_min, hx_main, mh, ridge_h, -hz, hz)
            m.add_box(hx_main, hx_main+lw, 0, lh_top, -hz, hz, wr, wc, rr, rc)
            _add_ground_quad(m, "tarmac", -fh, fh, -fh, fh)
            return m.to_b3d()
        # Main shed
        m.add_box(x_main_min, hx_main, 0, mh, -hz, hz, wr, wc, rr, rc)
        _add_gabled_roof(m, wr, wc, rr, rc, x_main_min, hx_main, mh, ridge_h, -hz, hz)
        # Lean-to box — extends to hx_main+lw = 4*S ≤ BUILDING_HALF_XZ
        m.add_box(hx_main, hx_main+lw, 0, lh_bot, -hz, hz, wr, wc, rr, rc)
        _add_mono_pitch_roof(m, wr, wc, rr, rc, hx_main, hx_main+lw, lh_bot, lh_top, -hz, hz)
        _add_ground_quad(m, "tarmac", -fh, fh, -fh, fh)
        return m.to_b3d()

    elif variant == "04":
        # Full box geometry + mono-pitch roof
        # Phase-11d: storage yard, two-high shipping container stacks, chain-link fence, floodlight mast
        # Footprint clamped: bw=8*S, bd=8*S so hx=hz=4*S ≤ BUILDING_HALF_XZ
        bw, bd = 8*S, 8*S
        hx, hz = bw/2, bd/2
        front_h, rear_h = 6*S, 4*S
        if lod == 1:
            m.add_box(-hx, hx, 0, front_h, -hz, hz, wr, wc, rr, rc)
            _add_ground_quad(m, "tarmac", -fh, fh, -fh, fh)
            return m.to_b3d()
        m.add_box(-hx, hx, 0, rear_h, -hz, hz, wr, wc, rr, rc)
        _add_mono_pitch_roof(m, wr, wc, rr, rc, -hx, hx, rear_h, front_h, -hz, hz)
        _add_ground_quad(m, "tarmac", -fh, fh, -fh, fh)
        return m.to_b3d()

    return m.to_b3d()


# ---------------------------------------------------------------------------
# INDUSTRIAL MED  (medium industrial, 1-3 story, 3000-5000 tris LOD0)
# ---------------------------------------------------------------------------

def _build_ind_med(zone, tier, variant, lod):
    """Industrial med -- 4 distinct warehouse/factory forms."""
    wr, wc = WALL_CELLS[(zone, tier, variant)]
    rr, rc = ROOF_CELL
    m = MeshAccum()
    S = 1.0
    fh = FOOTPRINT_HALF[tier]

    if variant == "01":
        # Box + flat roof (dock leveller recesses = 3 simple shallow box recesses on front)
        # Footprint clamped: bw=8*S, bd=8*S so hx=hz=4*S ≤ BUILDING_HALF_XZ
        bw, bd, bh = 8*S, 8*S, 8*S
        hx, hz = bw/2, bd/2
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            _add_ground_quad(m, "tarmac", -fh, fh, -fh, fh)
            return m.to_b3d()
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        # 3 dock leveller recesses (simple box recesses)
        for i in range(3):
            dcx = -hx + bw*(i+0.5)/3
            _add_loading_dock(m, wr, wc, dcx, 0, -hz, 3*S, 1.2*S, 0.5*S, normal_sign_z=-1)
        _add_ground_quad(m, "tarmac", -fh, fh, -fh, fh)
        return m.to_b3d()

    elif variant == "02":
        # Box + sawtooth north-light roof
        # Footprint clamped: bw=8*S, bd=8*S so hx=hz=4*S ≤ BUILDING_HALF_XZ
        bw, bd, bh = 8*S, 8*S, 10*S
        hx, hz = bw/2, bd/2
        saw_h = 2.5*S
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            _add_ground_quad(m, "tarmac", -fh, fh, -fh, fh)
            return m.to_b3d()
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        _add_sawtooth_roof(m, wr, wc, rr, rc, -hx, hx, bh, bh+saw_h, -hz, hz, 4)
        _add_ground_quad(m, "tarmac", -fh, fh, -fh, fh)
        return m.to_b3d()

    elif variant == "03":
        # Box + flat roof + optional 6 simple thin pilaster boxes on front
        # Footprint clamped: bw=8*S, bd=8*S so hx=hz=4*S ≤ BUILDING_HALF_XZ
        bw, bd, bh = 8*S, 8*S, 14*S
        hx, hz = bw/2, bd/2
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            _add_ground_quad(m, "tarmac", -fh, fh, -fh, fh)
            return m.to_b3d()
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        # 6 simple thin pilaster boxes on front face only
        for i in range(6):
            px = -hx + bw * (i + 0.5) / 6
            m.add_box(px-0.1*S, px+0.1*S, 0, bh, -hz-0.15*S, -hz, wr, wc, walls_only=True)
        _add_ground_quad(m, "tarmac", -fh, fh, -fh, fh)
        return m.to_b3d()

    elif variant == "04":
        # Box + flat roof
        # Footprint clamped: bw=8*S, bd=8*S so hx=hz=4*S ≤ BUILDING_HALF_XZ
        bw, bd, bh = 8*S, 8*S, 9*S
        hx, hz = bw/2, bd/2
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            _add_ground_quad(m, "tarmac", -fh, fh, -fh, fh)
            return m.to_b3d()
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        _add_ground_quad(m, "tarmac", -fh, fh, -fh, fh)
        return m.to_b3d()

    return m.to_b3d()


# ---------------------------------------------------------------------------
# INDUSTRIAL HIGH  (large industrial, 3-6 story, 6000-10000 tris LOD0)
# ---------------------------------------------------------------------------

def _build_ind_high(zone, tier, variant, lod):
    """Industrial high -- 4 distinct large industrial forms."""
    wr, wc = WALL_CELLS[(zone, tier, variant)]
    rr, rc = ROOF_CELL
    m = MeshAccum()
    S = 1.0
    fh = FOOTPRINT_HALF[tier]

    if variant == "01":
        # 3 cylinder approximations (12 segments) + conical tops + small base shed box
        # Footprint clamped: silo_r=1.5*S, centres repositioned so all extents ≤ ±4*S
        silo_r = 1.5*S
        silo_h = 20*S
        cone_h = 2*S
        n_seg = 12
        cx_list = [(-2*S, -1*S), (2*S, -1*S), (0, 2*S)]
        if lod == 2:
            for cx, cz in cx_list:
                _add_cylinder(m, wr, wc, cx, cz, 0, silo_h, silo_r, n_sides=8)
            _add_ground_quad(m, "tarmac", -fh, fh, -fh, fh)
            return m.to_b3d()
        if lod == 1:
            for cx, cz in cx_list:
                _add_cylinder(m, wr, wc, cx, cz, 0, silo_h, silo_r, n_sides=n_seg)
                _add_cylinder_cap(m, wr, wc, cx, cz, silo_h, silo_r, n_sides=n_seg, face_up=True)
            _add_ground_quad(m, "tarmac", -fh, fh, -fh, fh)
            return m.to_b3d()
        # LOD0
        for cx, cz in cx_list:
            _add_cylinder(m, wr, wc, cx, cz, 0, silo_h, silo_r, n_sides=n_seg)
            _add_cylinder_cap(m, wr, wc, cx, cz, silo_h, silo_r, n_sides=n_seg, face_up=True)
            # Conical roof
            _add_spire(m, rr, rc, cx, cz, silo_h, silo_h+cone_h, silo_r, n_sides=n_seg)
        # Loading shed box — clamped to ±4*S in X
        m.add_box(-4*S, 4*S, 0, 4*S, -3*S, -1*S, wr, wc, rr, rc)
        _add_ground_quad(m, "tarmac", -fh, fh, -fh, fh)
        return m.to_b3d()

    elif variant == "02":
        # Main box + 2 cylinder stack approximations (12 segs) + pipe run box
        # Footprint clamped: bw=8*S, bd=8*S; stacks inset so extent ≤ ±4*S
        bw, bd, bh = 8*S, 8*S, 24*S
        hx, hz = bw/2, bd/2
        stack_r = 0.5*S
        stack_h = 30*S
        n_seg = 12
        if lod == 2:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            _add_cylinder(m, wr, wc, -hx+1*S, hz-1*S, 0, stack_h, stack_r, n_sides=8)
            _add_cylinder(m, wr, wc, hx-1*S, hz-1*S, 0, stack_h, stack_r, n_sides=8)
            _add_ground_quad(m, "tarmac", -fh, fh, -fh, fh)
            return m.to_b3d()
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            _add_cylinder(m, wr, wc, -hx+1*S, hz-1*S, 0, stack_h, stack_r, n_sides=n_seg)
            _add_cylinder(m, wr, wc, hx-1*S, hz-1*S, 0, stack_h, stack_r, n_sides=n_seg)
            _add_ground_quad(m, "tarmac", -fh, fh, -fh, fh)
            return m.to_b3d()
        # LOD0
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        for sx in [-hx+1*S, hx-1*S]:
            _add_cylinder(m, wr, wc, sx, hz-1*S, 0, stack_h, stack_r, n_sides=n_seg)
            _add_cylinder_cap(m, wr, wc, sx, hz-1*S, stack_h, stack_r, n_sides=n_seg, face_up=True)
        # Pipe run box
        m.add_box(-hx+1*S, hx-1*S, 8*S-0.3*S, 8*S+0.3*S, hz-1*S-0.2*S, hz-1*S+0.2*S, wr, wc)
        _add_ground_quad(m, "tarmac", -fh, fh, -fh, fh)
        return m.to_b3d()

    elif variant == "03":
        # L-shaped plan (two boxes forming L) + flat roof
        # Footprint redesigned to fit within ±4*S × ±4*S bounding box
        # Horizontal bar: X [-4*S, +4*S] × Z [-4*S, 0]
        # Vertical stem:  X [-4*S,  0  ] × Z [  0, +4*S]
        h = 10*S
        if lod == 2:
            m.add_box(-4*S, 4*S, 0, h, -4*S, 4*S, wr, wc, rr, rc)
            _add_ground_quad(m, "tarmac", -fh, fh, -fh, fh)
            return m.to_b3d()
        if lod == 1:
            m.add_box(-4*S, 4*S, 0, h, -4*S, 0, wr, wc, rr, rc)
            m.add_box(-4*S, 0, 0, h, 0, 4*S, wr, wc, rr, rc)
            _add_ground_quad(m, "tarmac", -fh, fh, -fh, fh)
            return m.to_b3d()
        # LOD0: L-shape within ±4*S bounding box
        m.add_box(-4*S, 4*S, 0, h, -4*S, 0, wr, wc, rr, rc)
        m.add_box(-4*S, 0, 0, h, 0, 4*S, wr, wc, rr, rc)
        _add_ground_quad(m, "tarmac", -fh, fh, -fh, fh)
        return m.to_b3d()

    elif variant == "04":
        # Main box + 2 transformer pad boxes + lightning rod box
        # Footprint clamped: bw=8*S, bd=8*S; transformer pads placed within main Z range
        bw, bd, bh = 8*S, 8*S, 16*S
        hx, hz = bw/2, bd/2
        if lod == 2:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            _add_ground_quad(m, "tarmac", -fh, fh, -fh, fh)
            return m.to_b3d()
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            # Transformer pads inset on front face (do not extend beyond -hz)
            m.add_box(-hx+0.5*S, -hx+2.5*S, 0, 3*S, -hz, -hz+1*S, wr, wc, rr, rc)
            m.add_box(hx-2.5*S, hx-0.5*S, 0, 3*S, -hz, -hz+1*S, wr, wc, rr, rc)
            _add_ground_quad(m, "tarmac", -fh, fh, -fh, fh)
            return m.to_b3d()
        # LOD0
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        # Two transformer pad boxes — placed against front wall, inside footprint
        for tx in [-hx+1.5*S, hx-1.5*S]:
            m.add_box(tx-1*S, tx+1*S, 0, 3*S, -hz, -hz+1.5*S, wr, wc, rr, rc)
        # Lightning rod box
        m.add_box(-1*S, 1*S, bh, bh+8*S, -1*S, 1*S, wr, wc, walls_only=True)
        _add_ground_quad(m, "tarmac", -fh, fh, -fh, fh)
        return m.to_b3d()

    return m.to_b3d()


# ---------------------------------------------------------------------------
# SERVICE BUILDINGS  (2500-4000 tris LOD0)
# ---------------------------------------------------------------------------

def build_svc_fire_station(lod):
    """Fire station: main hall box + watch tower box + canopy slab over bay doors."""
    wr, wc = WALL_CELLS[("svc", "fire_station")]
    rr, rc = ROOF_CELL
    m = MeshAccum()
    S = 1.0
    fh = FOOTPRINT_HALF["svc"]

    hall_w, hall_d, hall_h = 14*S, 12*S, 6*S
    hx, hz = hall_w/2, hall_d/2
    tow_w, tow_h = 4*S, 10*S

    if lod == 1:
        m.add_box(-hx, hx, 0, hall_h, -hz, hz, wr, wc, rr, rc)
        m.add_box(hx, hx+tow_w, 0, tow_h, -tow_w/2, tow_w/2, wr, wc, rr, rc)
        _add_ground_quad(m, "paving", -fh, fh, -fh, fh)
        return m.to_b3d()

    # LOD0: main hall box
    m.add_box(-hx, hx, 0, hall_h, -hz, hz, wr, wc, rr, rc)
    # Watch tower box
    m.add_box(hx, hx+tow_w, 0, tow_h, -tow_w/2, tow_w/2, wr, wc, rr, rc)
    # Simple canopy slab over bay doors
    m.add_box(-hx, 0, hall_h*0.75, hall_h*0.75+0.15*S, -hz-1.5*S, -hz, wr, wc)
    _add_ground_quad(m, "paving", -fh, fh, -fh, fh)
    return m.to_b3d()


def build_svc_police_station(lod):
    """Police station: main box + projecting entrance box + triangular pediment."""
    wr, wc = WALL_CELLS[("svc", "police_station")]
    rr, rc = ROOF_CELL
    m = MeshAccum()
    S = 1.0
    fh = FOOTPRINT_HALF["svc"]

    bw, bd, bh = 12*S, 10*S, 9*S
    hx, hz = bw/2, bd/2
    ent_w, ent_d = 4*S, 2*S

    if lod == 1:
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        m.add_box(-ent_w/2, ent_w/2, 0, bh, -hz-ent_d, -hz, wr, wc, rr, rc)
        _add_ground_quad(m, "paving", -fh, fh, -fh, fh)
        return m.to_b3d()

    # LOD0: main box
    m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
    # Projecting entrance box
    m.add_box(-ent_w/2, ent_w/2, 0, bh, -hz-ent_d, -hz, wr, wc, rr, rc)
    # Triangular pediment (2 triangles)
    ped_proj = 0.3*S
    m.add_tri((-ent_w/2, bh, -hz-ent_d-ped_proj), (ent_w/2, bh, -hz-ent_d-ped_proj),
              (0, bh+2*S, -hz-ent_d-ped_proj), (0, 0, -1), wr, wc)
    m.add_tri((ent_w/2, bh, -hz-ent_d), (-ent_w/2, bh, -hz-ent_d),
              (0, bh+2*S, -hz-ent_d), (0, 0, 1), wr, wc)
    _add_ground_quad(m, "paving", -fh, fh, -fh, fh)
    return m.to_b3d()


def build_svc_power_plant(lod):
    """Power plant: main box + pitched roof + 2 cooling tower frustums.

    All geometry fits within the 2×2 tile footprint (±10 m).
    Main building: 16 m wide × 10 m deep (hx=8, hz=5).
    Cooling towers: radius 2 m, centred at z=7.5 m → far edge 9.5 m ≤ 10 m.
    """
    wr, wc = WALL_CELLS[("svc", "power_plant")]
    rr, rc = ROOF_CELL
    m = MeshAccum()
    S = 1.0
    fh = FOOTPRINT_HALF["svc"]

    bw, bd, bh = 16*S, 10*S, 12*S
    hx, hz = bw/2, bd/2
    ridge_h = 2*S
    ct_base_r = 2*S
    ct_top_r  = 1.5*S
    ct_h  = 10*S
    n_seg = 12
    # Tower centres: z = hz + ct_base_r + 0.5*S = 7.5 m; far edge = 9.5 m (within ±10 m)
    ct_z = hz + ct_base_r + 0.5*S

    if lod == 1:
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        _add_gabled_roof(m, wr, wc, rr, rc, -hx, hx, bh, ridge_h, -hz, hz)
        for tcx in [-3*S, 3*S]:
            _add_cylinder(m, wr, wc, tcx, ct_z, 0, ct_h, ct_base_r*0.8, n_sides=8)
        _add_ground_quad(m, "gravel", -fh, fh, -fh, fh)
        return m.to_b3d()

    # LOD0: main box + pitched roof
    m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
    _add_gabled_roof(m, wr, wc, rr, rc, -hx, hx, bh, ridge_h, -hz, hz)

    # Two cooling tower frustums
    for tcx in [-3*S, 3*S]:
        for i in range(n_seg):
            a0 = 2*math.pi*i/n_seg
            a1 = 2*math.pi*(i+1)/n_seg
            bx0 = tcx + ct_base_r*math.sin(a0); bz0 = ct_z + ct_base_r*math.cos(a0)
            bx1 = tcx + ct_base_r*math.sin(a1); bz1 = ct_z + ct_base_r*math.cos(a1)
            tx0 = tcx + ct_top_r*math.sin(a0); tz0 = ct_z + ct_top_r*math.cos(a0)
            tx1 = tcx + ct_top_r*math.sin(a1); tz1 = ct_z + ct_top_r*math.cos(a1)
            nx = math.sin((a0+a1)*0.5); nz = math.cos((a0+a1)*0.5)
            m.add_quad((bx0, 0, bz0), (bx1, 0, bz1), (tx1, ct_h, tz1), (tx0, ct_h, tz0),
                       (nx, 0.2, nz), wr, wc)
        _add_cylinder_cap(m, rr, rc, tcx, ct_z, ct_h, ct_top_r, n_sides=n_seg, face_up=True)

    _add_ground_quad(m, "gravel", -fh, fh, -fh, fh)
    return m.to_b3d()


def build_svc_water_tower(lod):
    """Water tower: 4 solid leg columns + cylindrical tank (16 segs) + conical roof."""
    wr, wc = WALL_CELLS[("svc", "water_tower")]
    rr, rc = ROOF_CELL
    m = MeshAccum()
    S = 1.0
    fh = FOOTPRINT_HALF["svc"]

    tank_r  = 4*S
    tank_h  = 7*S
    leg_h   = 8*S
    tank_bot = leg_h
    tank_top = tank_bot + tank_h
    cone_r  = 4.5*S
    cone_h  = 2*S
    n_seg   = 16
    # Legs positioned inside the tank radius so the tank overhangs them visibly.
    leg_spread = 2.5*S   # distance from centre to leg centre
    leg_hw     = 1.5*S   # half-width of each square leg column

    if lod == 1:
        _add_cylinder(m, wr, wc, 0, 0, tank_bot, tank_top, tank_r, n_sides=8)
        _add_cylinder_cap(m, wr, wc, 0, 0, tank_top, tank_r, n_sides=8, face_up=True)
        for sx, sz in [(-1, -1), (1, -1), (-1, 1), (1, 1)]:
            lx, lz = sx * leg_spread, sz * leg_spread
            m.add_box(lx - leg_hw, lx + leg_hw, 0, tank_bot,
                      lz - leg_hw, lz + leg_hw, wr, wc, walls_only=True)
        _add_ground_quad(m, "gravel", -4*S, 4*S, -4*S, 4*S)
        return m.to_b3d()

    # LOD0: cylindrical tank with top + bottom caps
    _add_cylinder(m, wr, wc, 0, 0, tank_bot, tank_top, tank_r, n_sides=n_seg)
    _add_cylinder_cap(m, wr, wc, 0, 0, tank_bot, tank_r, n_sides=n_seg, face_up=False)
    _add_cylinder_cap(m, wr, wc, 0, 0, tank_top, tank_r, n_sides=n_seg, face_up=True)

    # Conical roof
    _add_spire(m, rr, rc, 0, 0, tank_top, tank_top + cone_h, cone_r, n_sides=n_seg)

    # Four solid square leg columns
    for sx, sz in [(-1, -1), (1, -1), (-1, 1), (1, 1)]:
        lx, lz = sx * leg_spread, sz * leg_spread
        m.add_box(lx - leg_hw, lx + leg_hw, 0, leg_h,
                  lz - leg_hw, lz + leg_hw, wr, wc, walls_only=True)

    _add_ground_quad(m, "gravel", -4*S, 4*S, -4*S, 4*S)
    return m.to_b3d()


# ---------------------------------------------------------------------------
# Dispatcher: build_box_building replacement
# ---------------------------------------------------------------------------

def build_box_building(zone: str, tier: str, variant: str, lod: int) -> bytes:
    """Route to zone/tier-specific detailed geometry builder."""
    if zone == "res" and tier == "low":
        return _build_res_low(zone, tier, variant, lod)
    elif zone == "res" and tier == "med":
        return _build_res_med(zone, tier, variant, lod)
    elif zone == "res" and tier == "high":
        return _build_res_high(zone, tier, variant, lod)
    elif zone == "com" and tier == "low":
        return _build_com_low(zone, tier, variant, lod)
    elif zone == "com" and tier == "med":
        return _build_com_med(zone, tier, variant, lod)
    elif zone == "com" and tier == "high":
        return _build_com_high(zone, tier, variant, lod)
    elif zone == "ind" and tier == "low":
        return _build_ind_low(zone, tier, variant, lod)
    elif zone == "ind" and tier == "med":
        return _build_ind_med(zone, tier, variant, lod)
    elif zone == "ind" and tier == "high":
        return _build_ind_high(zone, tier, variant, lod)
    else:
        # Fallback: simple box
        wr, wc = WALL_CELLS[(zone, tier, variant)]
        rr, rc = ROOF_CELL
        h = TIER_HEIGHT[tier]
        hx = hz = BUILDING_HALF_XZ
        all_verts, all_tris = [], []
        v, t = box_faces(-hx, hx, 0, h, -hz, hz, wr, wc, rr, rc)
        all_verts.extend(v); all_tris.extend(t)
        return build_b3d(all_verts, all_tris)


# ---------------------------------------------------------------------------
# Vehicle geometry
# ---------------------------------------------------------------------------

# Vehicle atlas cells (vehicles_diffuse_atlas_d.dds 4×4 grid)
VEHICLE_CELLS = {
    "car_sedan":    (0, 0),
    "car_hatchback":(0, 1),
    "car_suv":      (0, 2),
    "bus_standard": (1, 0),
    "truck_cargo":  (1, 1),
}

# Vehicle body dimensions in BUILD-space (before the Z-forward reorientation).
# In build-space: X is the front-to-back axis (+X = front), Z is side-to-side.
# A post-build rotation in build_vehicle() converts these to Irrlicht's
# +Z-forward convention: (x,z) → (-z,x).
# (xmin, xmax, ymin, ymax, zmin, zmax)
VEHICLE_BODY = {
    "car_sedan":     (-2.0,  2.0, 0.0,  1.40, -0.85, 0.85),
    "car_hatchback": (-1.9,  1.9, 0.0,  1.50, -0.85, 0.85),
    "car_suv":       (-2.2,  2.2, 0.0,  1.70, -0.95, 0.95),
    "bus_standard":  (-5.5,  5.5, 0.0,  2.80, -1.10, 1.10),
    "truck_cargo":   (-4.0,  4.0, 0.0,  2.50, -1.00, 1.00),
}

# Roof box (sits on top of body, recessed slightly for silhouette)
# (x_fraction, y_fraction of body dims — how much smaller the roof is)
VEHICLE_ROOF_PARAMS = {
    # (x_shrink, z_shrink, roof_h_fraction of total height)
    "car_sedan":     (0.30, 0.10, 0.45),
    "car_hatchback": (0.20, 0.10, 0.50),  # hatchback is squarer/taller roof
    "car_suv":       (0.25, 0.05, 0.45),
    "bus_standard":  (0.05, 0.05, 0.30),
    "truck_cargo":   (0.20, 0.05, 0.35),
}


def build_vehicle(vtype: str, lod: int) -> bytes:
    """
    Per-variant silhouettes:
      car_sedan    : body + pronounced boot/trunk step + roof box + wheel arches + side mirrors + exhaust stub
      car_hatchback: body + near-vertical rear (no boot step) + taller squarer roof + wheel arches + mirrors
      car_suv      : body + raised ride-height + flat full-length roof + roof rack rails + wheel arches + mirrors
      bus_standard : body + destination blind recess + folding-door frame + wheel arch skirts + roof AC box + rear vent louvres
      truck_cargo  : cab box + cargo box + cab/cargo gap + grille + exhaust stack + rear door inset + side step
    LOD0 = full detail (1,800-2,000 tris cars; 2,500-3,000 tris bus/truck).
    LOD1 = simplified silhouette (cars >=300 tris; bus/truck >=400 tris).
    """
    row, col = VEHICLE_CELLS[vtype]

    all_verts: list = []
    all_tris:  list = []

    def _add(verts, tris):
        base = len(all_verts)
        all_verts.extend(verts)
        for t in tris:
            all_tris.append((base + t[0], base + t[1], base + t[2]))

    def _box(x0, x1, y0, y1, z0, z1, nr=None, nc=None):
        nr2 = row if nr is None else nr
        nc2 = col if nc is None else nc
        v, t = box_faces(x0, x1, y0, y1, z0, z1, nr2, nc2, nr2, nc2)
        _add(v, t)

    def _disk_n(wx, wy, wz_centre, wheel_r, wheel_w, n_seg):
        """Two flat n-sided disks (inner + outer face) approximating a wheel."""
        wz_in  = wz_centre - wheel_w * 0.5
        wz_out = wz_centre + wheel_w * 0.5
        for z_val, normal, reverse in [
            (wz_out, (0, 0, 1) if wz_out > 0 else (0, 0, -1), False),
            (wz_in,  (0, 0, -1) if wz_out > 0 else (0, 0, 1),  True),
        ]:
            verts_disk = []
            for i in range(n_seg):
                angle = 2 * math.pi * i / n_seg
                px = wx + wheel_r * math.sin(angle)
                py = wy + wheel_r * math.cos(angle)
                u2, v2 = atlas_uv(row, col,
                                  0.5 + 0.5 * math.sin(angle),
                                  0.5 + 0.5 * math.cos(angle))
                verts_disk.append(Vertex(px, py, z_val, *normal, u2, v2))
            center_v = Vertex(wx, wy, z_val, *normal,
                              *atlas_uv(row, col, 0.5, 0.5))
            base = len(all_verts)
            all_verts.extend(verts_disk)
            all_verts.append(center_v)
            c_idx = base + n_seg
            for i in range(n_seg):
                ni = (i + 1) % n_seg
                if reverse:
                    all_tris.append((c_idx, base + ni, base + i))
                else:
                    all_tris.append((c_idx, base + i, base + ni))

    def _sidewall_ring(wx, wy, wz_centre, wheel_r, wheel_w, n_seg):
        """Quad strip around the tire circumference (sidewall)."""
        wz_in  = wz_centre - wheel_w * 0.5
        wz_out = wz_centre + wheel_w * 0.5
        for i in range(n_seg):
            a0 = 2 * math.pi * i / n_seg
            a1 = 2 * math.pi * (i + 1) / n_seg
            p0x = wx + wheel_r * math.sin(a0); p0y = wy + wheel_r * math.cos(a0)
            p1x = wx + wheel_r * math.sin(a1); p1y = wy + wheel_r * math.cos(a1)
            nx0 = math.sin(a0); ny0 = math.cos(a0)
            u0, vv0 = atlas_uv(row, col, i / n_seg, 0.2)
            u1, vv1 = atlas_uv(row, col, (i + 1) / n_seg, 0.2)
            u0b, vv0b = atlas_uv(row, col, i / n_seg, 0.8)
            u1b, vv1b = atlas_uv(row, col, (i + 1) / n_seg, 0.8)
            base = len(all_verts)
            all_verts.append(Vertex(p0x, p0y, wz_in,  nx0, ny0, 0, u0,  vv0))
            all_verts.append(Vertex(p1x, p1y, wz_in,  nx0, ny0, 0, u1,  vv1))
            all_verts.append(Vertex(p1x, p1y, wz_out, nx0, ny0, 0, u1b, vv1b))
            all_verts.append(Vertex(p0x, p0y, wz_out, nx0, ny0, 0, u0b, vv0b))
            all_tris.append((base, base + 1, base + 2))
            all_tris.append((base, base + 2, base + 3))

    def _spoke_ring(wx, wy, wz_centre, wheel_r, wheel_w, n_spokes):
        """Flat spoke bars radiating from hub centre on outer disk face."""
        z_val  = wz_centre + wheel_w * 0.5 + 0.005
        nz     = 1.0 if z_val > 0 else -1.0
        spoke_w = 0.03
        inner_r = wheel_r * 0.18
        for i in range(n_spokes):
            angle = 2 * math.pi * i / n_spokes
            sa = math.sin(angle); ca = math.cos(angle)
            pa = math.sin(angle + 0.5); pb = math.cos(angle + 0.5)
            ix = wx + inner_r * sa; iy = wy + inner_r * ca
            ox = wx + wheel_r * 0.85 * sa; oy = wy + wheel_r * 0.85 * ca
            perp_x = -ca * spoke_w * 0.5; perp_y = sa * spoke_w * 0.5
            base = len(all_verts)
            uci, vci = atlas_uv(row, col, 0.5 + 0.5 * sa * 0.2, 0.5 + 0.5 * ca * 0.2)
            uoi, voi = atlas_uv(row, col, 0.5 + 0.5 * sa * 0.85, 0.5 + 0.5 * ca * 0.85)
            all_verts.append(Vertex(ix + perp_x, iy + perp_y, z_val, 0, 0, nz, uci, vci))
            all_verts.append(Vertex(ix - perp_x, iy - perp_y, z_val, 0, 0, nz, uci, vci))
            all_verts.append(Vertex(ox + perp_x, oy + perp_y, z_val, 0, 0, nz, uoi, voi))
            all_verts.append(Vertex(ox - perp_x, oy - perp_y, z_val, 0, 0, nz, uoi, voi))
            all_tris.append((base, base + 1, base + 3))
            all_tris.append((base, base + 3, base + 2))

    def _full_wheel(wx, wy, wz_c, wheel_r, wheel_w, n_seg, n_spokes):
        """Disk + sidewall ring + spokes."""
        _disk_n(wx, wy, wz_c, wheel_r, wheel_w, n_seg)
        _sidewall_ring(wx, wy, wz_c, wheel_r, wheel_w, n_seg)
        _spoke_ring(wx, wy, wz_c, wheel_r, wheel_w, n_spokes)

    def _wheel_arch(wx, wy, wz_centre, wheel_r, wheel_w):
        """Thin rectangular arch skirt sitting above wheel position."""
        arch_h  = wheel_r * 1.2
        arch_hw = wheel_r * 1.1
        arch_t  = 0.02
        wz0 = wz_centre - wheel_w * 0.5 - arch_t
        wz1 = wz_centre + wheel_w * 0.5 + arch_t
        _box(wx - arch_hw, wx + arch_hw, wy, wy + arch_h, wz0 - arch_t, wz0)
        _box(wx - arch_hw, wx + arch_hw, wy, wy + arch_h, wz1, wz1 + arch_t)

    def _side_mirror(wx, wy, wz, facing_z):
        """L-shaped mirror housing: arm box + head box."""
        arm_w = 0.04; arm_h = 0.03; arm_d = 0.09
        head_w = 0.10; head_h = 0.07; head_d = 0.03
        if facing_z > 0:
            arm_z0 = wz; arm_z1 = wz + arm_d
            head_z0 = arm_z1; head_z1 = arm_z1 + head_d
        else:
            arm_z1 = wz; arm_z0 = wz - arm_d
            head_z1 = arm_z0; head_z0 = arm_z0 - head_d
        _box(wx - arm_w * 0.5, wx + arm_w * 0.5, wy, wy + arm_h, arm_z0, arm_z1)
        _box(wx - head_w * 0.5, wx + head_w * 0.5, wy, wy + head_h, head_z0, head_z1)

    # ==========================================================================
    #  LOD0 shared car helper — call after body/roof/boot geometry is added
    # ==========================================================================
    def _car_lod0_common(body_x0, body_x1, body_y0, body_y1, body_z0, body_z1,
                         roof_x0, roof_x1, roof_y0, roof_y1,
                         wheel_r, wheel_w, wheel_y,
                         front_axle_x, rear_axle_x,
                         mirror_x):
        # ---- Wheels (16-seg disks + sidewalls + 8 spokes) ----
        for wx in [front_axle_x, rear_axle_x]:
            for wz_c in [body_z0 - wheel_w * 0.35, body_z1 + wheel_w * 0.35]:
                _full_wheel(wx, wheel_y, wz_c, wheel_r, wheel_w, 16, 8)
                _wheel_arch(wx, wheel_y - wheel_r * 0.12, wz_c, wheel_r, wheel_w)

        # ---- Headlights (2 boxes proud of front face) ----
        hl_w = 0.28; hl_h = 0.12; hl_d = 0.04
        hl_y  = body_y0 + (body_y1 - body_y0) * 0.55
        for hz in [body_z0 + 0.18, body_z1 - 0.18 - hl_w]:
            _box(body_x1 - hl_d, body_x1 + hl_d, hl_y, hl_y + hl_h, hz, hz + hl_w)

        # ---- Taillights (2 boxes proud of rear face) ----
        tl_w = 0.24; tl_h = 0.10; tl_d = 0.04
        tl_y = body_y0 + (body_y1 - body_y0) * 0.50
        for tz in [body_z0 + 0.15, body_z1 - 0.15 - tl_w]:
            _box(body_x0 - tl_d, body_x0 + tl_d, tl_y, tl_y + tl_h, tz, tz + tl_w)

        # ---- Front bumper bar ----
        _box(body_x1 - 0.04, body_x1 + 0.06,
             body_y0, body_y0 + 0.16,
             body_z0 + 0.05, body_z1 - 0.05)

        # ---- Rear bumper bar ----
        _box(body_x0 - 0.06, body_x0 + 0.04,
             body_y0, body_y0 + 0.14,
             body_z0 + 0.05, body_z1 - 0.05)

        # ---- Rocker panels / side skirts (lower body sides) ----
        sk_h = 0.08; sk_d = 0.025
        sk_y0 = body_y0; sk_y1 = body_y0 + sk_h
        _box(body_x0 + 0.30, body_x1 - 0.30, sk_y0, sk_y1,
             body_z0 - sk_d, body_z0)
        _box(body_x0 + 0.30, body_x1 - 0.30, sk_y0, sk_y1,
             body_z1, body_z1 + sk_d)

        # ---- Wheel well lips (inner arch sill strip) ----
        lip_h = 0.06; lip_t = 0.018
        for wx in [front_axle_x, rear_axle_x]:
            arch_hw = wheel_r * 1.1
            for wz_c in [body_z0 - wheel_w * 0.35, body_z1 + wheel_w * 0.35]:
                wz0 = wz_c - wheel_w * 0.5 - 0.02
                wz1 = wz_c + wheel_w * 0.5 + 0.02
                _box(wx - arch_hw, wx + arch_hw,
                     wheel_y - wheel_r * 0.12,
                     wheel_y - wheel_r * 0.12 + lip_h,
                     wz0 - lip_t, wz0)
                _box(wx - arch_hw, wx + arch_hw,
                     wheel_y - wheel_r * 0.12,
                     wheel_y - wheel_r * 0.12 + lip_h,
                     wz1, wz1 + lip_t)

        # ---- Window frame boxes (4 side windows) ----
        win_y0 = body_y1 * 0.72
        win_h  = (roof_y0 - win_y0) * 0.85
        win_t  = 0.018
        # front side windows
        fw_x0 = body_x1 - 0.85; fw_x1 = body_x1 - 0.05
        for wz_face, wz_in in [(body_z0, body_z0 - win_t),
                                (body_z1, body_z1 + win_t)]:
            _box(fw_x0, fw_x1, win_y0, win_y0 + win_h, wz_in, wz_face)
        # rear side windows
        rw_x0 = body_x0 + 0.15; rw_x1 = body_x0 + 0.95
        for wz_face, wz_in in [(body_z0, body_z0 - win_t),
                                (body_z1, body_z1 + win_t)]:
            _box(rw_x0, rw_x1, win_y0, win_y0 + win_h, wz_in, wz_face)

        # ---- Windshield A-pillar strips ----
        ap_w = 0.06; ap_h = roof_y0 - body_y1 + 0.05
        for wz_ap in [body_z0 + 0.04, body_z1 - 0.04 - ap_w]:
            _box(body_x1 - 0.65, body_x1 - 0.50,
                 body_y1, body_y1 + ap_h,
                 wz_ap, wz_ap + ap_w)

        # ---- B-pillars (vertical strips mid-body each side) ----
        bp_x_c = (body_x0 + body_x1) * 0.5 - 0.10
        bp_w_x = 0.07; bp_d = 0.022
        for wz_bp_out, wz_bp_in in [(body_z0 - bp_d, body_z0),
                                     (body_z1, body_z1 + bp_d)]:
            _box(bp_x_c - bp_w_x, bp_x_c + bp_w_x,
                 body_y1 * 0.55, roof_y0,
                 wz_bp_out, wz_bp_in)

        # ---- Door handles (4 handles: front-left, front-right, rear-left, rear-right) ----
        dh_w = 0.14; dh_h = 0.04; dh_d = 0.025
        dh_y = body_y0 + (body_y1 - body_y0) * 0.68
        for dh_x in [body_x1 - 0.55, body_x0 + 0.55]:
            for dh_z_out, dh_z_in in [(body_z0 - dh_d, body_z0),
                                       (body_z1, body_z1 + dh_d)]:
                _box(dh_x - dh_w * 0.5, dh_x + dh_w * 0.5,
                     dh_y, dh_y + dh_h,
                     dh_z_out, dh_z_in)

        # ---- Hood/bonnet panel (slightly raised box on top-front) ----
        _box(body_x1 - 0.85, body_x1 - 0.02,
             body_y1, body_y1 + 0.022,
             body_z0 + 0.06, body_z1 - 0.06)

        # ---- Undercarriage frame ----
        _box(body_x0 + 0.35, body_x1 - 0.35,
             0.02, 0.08,
             body_z0 + 0.08, body_z1 - 0.08)

        # ---- Front face vertical bands (grille area) ----
        band_h = body_y0 + (body_y1 - body_y0) * 0.50
        bz_span = (body_z1 - body_z0)
        for bz_off in [body_z0 + 0.05,
                       body_z0 + bz_span * 0.35,
                       body_z0 + bz_span * 0.68]:
            bz_w = bz_span * 0.25
            _box(body_x1 - 0.03, body_x1 + 0.03,
                 body_y0, band_h,
                 bz_off, bz_off + bz_w)

        # ---- Rear face vertical bands ----
        for bz_off in [body_z0 + 0.05,
                       body_z0 + bz_span * 0.35,
                       body_z0 + bz_span * 0.68]:
            bz_w = bz_span * 0.25
            _box(body_x0 - 0.03, body_x0 + 0.03,
                 body_y0, band_h,
                 bz_off, bz_off + bz_w)

        # ---- Top surface bands (hood + roof as separate surfaces) ----
        _box(body_x1 - 0.85, body_x1 - 0.02,
             body_y1 - 0.01, body_y1 + 0.01,
             body_z0 + 0.04, body_z1 - 0.04)

        # ---- Belt-line chrome strip (thin flat strip on body sides) ----
        belt_y = body_y0 + (body_y1 - body_y0) * 0.78
        belt_h = 0.025; belt_d = 0.008
        _box(body_x0 + 0.20, body_x1 - 0.20,
             belt_y, belt_y + belt_h,
             body_z0 - belt_d, body_z0)
        _box(body_x0 + 0.20, body_x1 - 0.20,
             belt_y, belt_y + belt_h,
             body_z1, body_z1 + belt_d)

        # ---- Roof panel detail strip (sunroof recess approximation) ----
        _box(roof_x0 + 0.15, roof_x1 - 0.15,
             roof_y1 - 0.02, roof_y1 + 0.008,
             body_z0 + 0.22, body_z1 - 0.22)

        # ---- Side mirrors ----
        mirror_y = body_y0 + (body_y1 - body_y0) * 0.75
        _side_mirror(mirror_x, mirror_y, body_z0, -1)
        _side_mirror(mirror_x, mirror_y, body_z1,  1)

        # ---- Hood lip (front edge lip) ----
        _box(body_x1 - 0.06, body_x1 + 0.02,
             body_y1 - 0.02, body_y1 + 0.04,
             body_z0 + 0.04, body_z1 - 0.04)

        # ---- Additional body-curve patches (trunk lip, roof-to-rear transition) ----
        for patch_x0, patch_x1 in [(body_x0 - 0.02, body_x0 + 0.04),   # rear lip
                                     (body_x0 + 0.30, body_x0 + 0.36)]:  # rear quarter
            _box(patch_x0, patch_x1,
                 body_y1 - 0.03, body_y1 + 0.03,
                 body_z0 + 0.05, body_z1 - 0.05)

        # ---- Additional A-pillar geometry (both sides, 2 strips each) ----
        for ap_x0, ap_x1 in [(body_x1 - 0.62, body_x1 - 0.52),
                               (body_x1 - 0.52, body_x1 - 0.42)]:
            ap_d = 0.016
            _box(ap_x0, ap_x1, body_y1, body_y1 + 0.28,
                 body_z0 - ap_d, body_z0)
            _box(ap_x0, ap_x1, body_y1, body_y1 + 0.28,
                 body_z1, body_z1 + ap_d)

        # ---- C-pillar strips (rear greenhouse-to-bootlid) ----
        cp_d = 0.016
        cp_x0 = body_x0 + 0.15; cp_x1 = body_x0 + 0.38
        _box(cp_x0, cp_x1, body_y1 - 0.05, roof_y0 + 0.08,
             body_z0 - cp_d, body_z0)
        _box(cp_x0, cp_x1, body_y1 - 0.05, roof_y0 + 0.08,
             body_z1, body_z1 + cp_d)

        # ---- Fender flares (front + rear, each side) ----
        ff_h = 0.06; ff_d = 0.025
        for ff_x0, ff_x1 in [(front_axle_x - 0.38, front_axle_x + 0.38),
                               (rear_axle_x  - 0.38, rear_axle_x  + 0.38)]:
            _box(ff_x0, ff_x1, body_y0, body_y0 + ff_h,
                 body_z0 - ff_d, body_z0 - 0.002)
            _box(ff_x0, ff_x1, body_y0, body_y0 + ff_h,
                 body_z1 + 0.002, body_z1 + ff_d)

        # ---- Windscreen wiper park strip (thin box across bonnet base) ----
        _box(body_x1 - 0.90, body_x1 - 0.12,
             body_y1 + 0.01, body_y1 + 0.05,
             body_z0 + 0.10, body_z1 - 0.10)

        # ---- Rear diffuser strip ----
        _box(body_x0 - 0.04, body_x0 + 0.02,
             body_y0 + 0.03, body_y0 + 0.14,
             body_z0 + 0.10, body_z1 - 0.10)

        # ---- Front lip spoiler ----
        _box(body_x1 - 0.03, body_x1 + 0.05,
             body_y0, body_y0 + 0.06,
             body_z0 + 0.04, body_z1 - 0.04)

        # ---- Tow hitch recess (rear underside) ----
        _box(body_x0 - 0.02, body_x0 + 0.03,
             body_y0 + 0.04, body_y0 + 0.12,
             -0.10, 0.10)

        # ---- Side panel additional horizontal ribs (2 per side) ----
        for rib_y_frac in [0.25, 0.48, 0.70]:
            rib_y = body_y0 + (body_y1 - body_y0) * rib_y_frac
            rib_d2 = 0.010; rib_h2 = 0.018
            _box(body_x0 + 0.18, body_x1 - 0.18,
                 rib_y, rib_y + rib_h2,
                 body_z0 - rib_d2, body_z0)
            _box(body_x0 + 0.18, body_x1 - 0.18,
                 rib_y, rib_y + rib_h2,
                 body_z1, body_z1 + rib_d2)

        # ---- Fog light pods (lower front corners) ----
        fl_w = 0.14; fl_h = 0.08; fl_d = 0.035
        fl_y = body_y0 + 0.04
        for fz in [body_z0 + 0.06, body_z1 - 0.06 - fl_w]:
            _box(body_x1 - fl_d, body_x1 + fl_d, fl_y, fl_y + fl_h, fz, fz + fl_w)

        # ---- Rear fog light (single, left side) ----
        _box(body_x0 - 0.03, body_x0 + 0.02,
             body_y0 + 0.10, body_y0 + 0.18,
             body_z0 + 0.06, body_z0 + 0.18)

        # ---- Grille mesh horizontal bars (4 bars in front face lower area) ----
        for gi in range(4):
            gy = body_y0 + 0.04 + gi * 0.10
            _box(body_x1 - 0.02, body_x1 + 0.02,
                 gy, gy + 0.05,
                 body_z0 + 0.10, body_z1 - 0.10)

        # ---- Roof-gutter drip rail (each side) ----
        gr_d = 0.014; gr_h = 0.025
        _box(roof_x0 + 0.05, roof_x1 - 0.05,
             roof_y1 - gr_h, roof_y1,
             body_z0 + 0.06, body_z0 + 0.06 + gr_d)
        _box(roof_x0 + 0.05, roof_x1 - 0.05,
             roof_y1 - gr_h, roof_y1,
             body_z1 - 0.06 - gr_d, body_z1 - 0.06)

        # ---- Headlight surround bezels ----
        hl_bz_w = 0.32; hl_bz_h = 0.16; hl_bz_d = 0.018
        hl_y = body_y0 + (body_y1 - body_y0) * 0.55 - 0.02
        for hz in [body_z0 + 0.14, body_z1 - 0.14 - hl_bz_w]:
            _box(body_x1 - hl_bz_d, body_x1 + hl_bz_d,
                 hl_y, hl_y + hl_bz_h,
                 hz, hz + hl_bz_w)

        # ---- Taillight surround bezels ----
        tl_bz_w = 0.28; tl_bz_h = 0.14; tl_bz_d = 0.018
        tl_y = body_y0 + (body_y1 - body_y0) * 0.50 - 0.02
        for tz in [body_z0 + 0.12, body_z1 - 0.12 - tl_bz_w]:
            _box(body_x0 - tl_bz_d, body_x0 + tl_bz_d,
                 tl_y, tl_y + tl_bz_h,
                 tz, tz + tl_bz_w)

        # ---- Sill step plates (under each door) ----
        sill_h = 0.04; sill_d = 0.030
        sill_y = body_y0 + 0.02
        for sill_x0, sill_x1 in [(body_x1 - 0.80, body_x1 - 0.10),
                                   (body_x0 + 0.10, body_x0 + 0.80)]:
            _box(sill_x0, sill_x1, sill_y, sill_y + sill_h,
                 body_z0 - sill_d, body_z0)
            _box(sill_x0, sill_x1, sill_y, sill_y + sill_h,
                 body_z1, body_z1 + sill_d)

        # ---- Lower front air dam slots (2 rectangular cutouts as inset boxes) ----
        for ad_z in [body_z0 + 0.22, body_z0 + 0.52]:
            _box(body_x1 - 0.02, body_x1 + 0.03,
                 body_y0 + 0.02, body_y0 + 0.12,
                 ad_z, ad_z + 0.22)

        # ---- Cabin cross-brace visible at base of windscreen ----
        _box(body_x1 - 0.64, body_x1 - 0.52,
             body_y1, body_y1 + 0.06,
             body_z0 + 0.08, body_z1 - 0.08)

        # ---- Number plate recess (front + rear) ----
        _box(body_x1 - 0.02, body_x1 + 0.03,
             body_y0 + 0.16, body_y0 + 0.32,
             -0.22, 0.22)
        _box(body_x0 - 0.03, body_x0 + 0.02,
             body_y0 + 0.14, body_y0 + 0.30,
             -0.20, 0.20)

        # ---- Inner door panel detail (4 door panels, each with upper+lower subdivision) ----
        dp_d = 0.015
        for dp_x0, dp_x1 in [(body_x1 - 0.88, body_x1 - 0.08),
                               (body_x0 + 0.08, body_x0 + 0.88)]:
            mid_y = body_y0 + (body_y1 - body_y0) * 0.45
            for cz_pair in [(body_z0 - dp_d, body_z0), (body_z1, body_z1 + dp_d)]:
                _box(dp_x0, dp_x1, body_y0 + 0.05, mid_y, *cz_pair)
                _box(dp_x0, dp_x1, mid_y, body_y1 - 0.05, *cz_pair)

        # ---- Quarter panel detail strips (front/rear fender transitions) ----
        qp_d = 0.015; qp_h = 0.12
        for qz_pair in [(body_z0 - qp_d, body_z0), (body_z1, body_z1 + qp_d)]:
            _box(body_x1 - 0.12, body_x1 - 0.04,
                 body_y0 + 0.20, body_y0 + 0.20 + qp_h, *qz_pair)
            _box(body_x0 + 0.04, body_x0 + 0.12,
                 body_y0 + 0.20, body_y0 + 0.20 + qp_h, *qz_pair)

        # ---- Rear screen wiper park strip ----
        _box(body_x0 - 0.03, body_x0 + 0.03,
             body_y1 + 0.01, body_y1 + 0.05,
             body_z0 + 0.12, body_z1 - 0.12)

        # ---- Headlight DRL strip (thin strip just below main headlight) ----
        drl_h = 0.04; drl_d = 0.015
        drl_y = body_y0 + (body_y1 - body_y0) * 0.52
        for dz in [body_z0 + 0.12, body_z1 - 0.12 - 0.42]:
            _box(body_x1 - drl_d, body_x1 + drl_d, drl_y, drl_y + drl_h, dz, dz + 0.42)

        # ---- Rear LED tail strip (full-width thin strip) ----
        _box(body_x0 - 0.015, body_x0 + 0.015,
             body_y0 + (body_y1 - body_y0) * 0.48,
             body_y0 + (body_y1 - body_y0) * 0.56,
             body_z0 + 0.08, body_z1 - 0.08)

        # ---- Windscreen seal strip (rubber around windscreen perimeter) ----
        ws_d = 0.014
        _box(body_x1 - 0.68, body_x1 - 0.04,
             body_y1 - ws_d, body_y1 + ws_d,
             body_z0 + 0.06, body_z1 - 0.06)
        _box(body_x1 - ws_d, body_x1 + ws_d,
             body_y1 - 0.02, body_y1 + 0.32,
             body_z0 + 0.06, body_z1 - 0.06)

        # ---- Rear screen seal strip ----
        _box(body_x0 - ws_d, body_x0 + ws_d,
             body_y1 - 0.02, body_y1 + 0.28,
             body_z0 + 0.10, body_z1 - 0.10)

        # ---- Stepped underfloor tray ----
        _box(body_x0 + 0.40, body_x1 - 0.40,
             0.01, 0.05,
             body_z0 + 0.10, body_z1 - 0.10)

        # ---- Rear over-rider strips (2 rubber bumper pads) ----
        for rz in [body_z0 + 0.14, body_z1 - 0.34]:
            _box(body_x0 - 0.04, body_x0 + 0.02,
                 body_y0 + 0.02, body_y0 + 0.12,
                 rz, rz + 0.18)

        # ---- Bonnet shut lines (2 thin strips along bonnet sides) ----
        bon_d = 0.012
        for bz_pair in [(body_z0 + 0.06, body_z0 + 0.06 + bon_d),
                         (body_z1 - 0.06 - bon_d, body_z1 - 0.06)]:
            _box(body_x1 - 0.88, body_x1 - 0.03,
                 body_y1, body_y1 + 0.022,
                 *bz_pair)

        # ---- Boot shut lines (2 thin strips along boot sides) ----
        for bz_pair in [(body_z0 + 0.08, body_z0 + 0.08 + bon_d),
                         (body_z1 - 0.08 - bon_d, body_z1 - 0.08)]:
            _box(body_x0 + 0.02, body_x0 + 0.95,
                 body_y1, body_y1 + 0.018,
                 *bz_pair)

        # ---- Rear lower apron (below bumper) ----
        _box(body_x0 - 0.03, body_x0 + 0.03,
             body_y0, body_y0 + 0.08,
             body_z0 + 0.08, body_z1 - 0.08)

        # ---- Side repeater indicator strips (each side, front quarter) ----
        rp_d = 0.012; rp_h = 0.06; rp_w = 0.14
        for rz in [body_z0 - rp_d, body_z1]:
            _box(body_x1 - 0.75, body_x1 - 0.75 + rp_w,
                 body_y0 + (body_y1 - body_y0) * 0.55,
                 body_y0 + (body_y1 - body_y0) * 0.55 + rp_h,
                 rz, rz + rp_d * 2)

        # ---- Additional front-face inner panel detail ----
        for fp_z0, fp_z1 in [(body_z0 + 0.10, body_z0 + 0.38),
                               (body_z0 + 0.40, body_z0 + 0.68),
                               (body_z0 + 0.70, body_z1 - 0.10)]:
            _box(body_x1 - 0.02, body_x1 + 0.02,
                 body_y0 + 0.12, body_y0 + 0.40,
                 fp_z0, fp_z1)

        # ---- Fender vent strips (decorative slots on front quarter, each side) ----
        fv_d = 0.015; fv_h = 0.06; fv_w = 0.10
        fv_y = body_y0 + (body_y1 - body_y0) * 0.58
        fv_x0 = body_x1 - 0.42; fv_x1 = body_x1 - 0.34
        for fz in [body_z0 - fv_d, body_z1]:
            for fi in range(3):
                fvy = fv_y + fi * 0.08
                _box(fv_x0, fv_x1, fvy, fvy + fv_h * 0.6, fz, fz + fv_d * 2)

        # ---- Rear quarter panel crease (extra detail line) ----
        rc_d = 0.012
        for rz_pair in [(body_z0 - rc_d, body_z0), (body_z1, body_z1 + rc_d)]:
            _box(body_x0 + 0.15, body_x0 + 0.60,
                 body_y0 + (body_y1 - body_y0) * 0.42,
                 body_y0 + (body_y1 - body_y0) * 0.46,
                 *rz_pair)

    # ==========================================================================
    #  LOD1 shared car helper
    # ==========================================================================
    def _car_lod1_common(body_x0, body_x1, body_y0, body_y1, body_z0, body_z1,
                          wheel_r, wheel_w, wheel_y, front_axle_x, rear_axle_x):
        # 8-seg wheels (disk only, no sidewall/spoke)
        for wx in [front_axle_x, rear_axle_x]:
            for wz_c in [body_z0 - wheel_w * 0.35, body_z1 + wheel_w * 0.35]:
                _disk_n(wx, wheel_y, wz_c, wheel_r, wheel_w, 8)
                _wheel_arch(wx, wheel_y - wheel_r * 0.12, wz_c, wheel_r, wheel_w)

        # Window strip bands (2 per side)
        win_y0 = body_y1 * 0.72; win_h = (body_y1 - body_y0) * 0.22
        win_t = 0.018
        for wz_face, wz_in in [(body_z0, body_z0 - win_t),
                                (body_z1, body_z1 + win_t)]:
            _box(body_x0 + 0.15, body_x1 - 0.15, win_y0, win_y0 + win_h,
                 wz_in, wz_face)

        # Door line strips
        crease_h = 0.03; crease_d = 0.012
        crease_y = body_y0 + (body_y1 - body_y0) * 0.55
        _box(body_x0 + 0.25, body_x1 - 0.25,
             crease_y, crease_y + crease_h,
             body_z0 - crease_d, body_z0)
        _box(body_x0 + 0.25, body_x1 - 0.25,
             crease_y, crease_y + crease_h,
             body_z1, body_z1 + crease_d)

        # Simplified bumpers
        _box(body_x1 - 0.02, body_x1 + 0.04,
             body_y0, body_y0 + 0.14,
             body_z0 + 0.08, body_z1 - 0.08)
        _box(body_x0 - 0.04, body_x0 + 0.02,
             body_y0, body_y0 + 0.12,
             body_z0 + 0.08, body_z1 - 0.08)

        # Simplified headlights
        hl_y = body_y0 + (body_y1 - body_y0) * 0.55
        for hz in [body_z0 + 0.18, body_z1 - 0.46]:
            _box(body_x1 - 0.03, body_x1 + 0.03, hl_y, hl_y + 0.10, hz, hz + 0.25)

        # Simplified taillights
        tl_y = body_y0 + (body_y1 - body_y0) * 0.50
        for tz in [body_z0 + 0.15, body_z1 - 0.38]:
            _box(body_x0 - 0.03, body_x0 + 0.03, tl_y, tl_y + 0.08, tz, tz + 0.20)

        # Rocker strips
        sk_h = 0.07; sk_d = 0.020
        _box(body_x0 + 0.30, body_x1 - 0.30, body_y0, body_y0 + sk_h,
             body_z0 - sk_d, body_z0)
        _box(body_x0 + 0.30, body_x1 - 0.30, body_y0, body_y0 + sk_h,
             body_z1, body_z1 + sk_d)

        # Belt strip
        belt_y = body_y0 + (body_y1 - body_y0) * 0.78
        belt_h = 0.022; belt_d = 0.007
        _box(body_x0 + 0.20, body_x1 - 0.20, belt_y, belt_y + belt_h,
             body_z0 - belt_d, body_z0)
        _box(body_x0 + 0.20, body_x1 - 0.20, belt_y, belt_y + belt_h,
             body_z1, body_z1 + belt_d)

    # ------------------------------------------------------------------
    if vtype == "car_sedan":
        body_x0, body_x1 = -2.0, 2.0
        body_y0, body_y1 = 0.0,  0.72
        body_z0, body_z1 = -0.85, 0.85

        # Lower body zone
        _box(body_x0, body_x1, body_y0, body_y0 + 0.24, body_z0, body_z1)
        # Door zone
        _box(body_x0, body_x1, body_y0 + 0.24, body_y1 - 0.14, body_z0, body_z1)
        # Upper greenhouse zone
        _box(body_x0, body_x1, body_y1 - 0.14, body_y1, body_z0, body_z1)

        if lod == 0:
            # Boot/trunk step
            boot_x0 = body_x0; boot_x1 = body_x0 + 1.1
            boot_y0 = body_y1;  boot_y1 = body_y1 + 0.22
            _box(boot_x0, boot_x1, boot_y0, boot_y1, body_z0 + 0.05, body_z1 - 0.05)

            # Boot lid panel (raised slightly)
            _box(boot_x0 + 0.05, boot_x1 - 0.05,
                 boot_y1, boot_y1 + 0.018,
                 body_z0 + 0.08, body_z1 - 0.08)

            # Roof box
            roof_x0 = body_x0 + 1.25; roof_x1 = body_x1 - 0.55
            roof_y0 = body_y1;         roof_y1 = body_y1 + 0.44
            _box(roof_x0, roof_x1, roof_y0, roof_y1, body_z0 + 0.08, body_z1 - 0.08)

            # Rear windscreen slope (two angled approximation boxes)
            rw_h = boot_y1 - roof_y0
            _box(boot_x1 - 0.08, roof_x0 + 0.08,
                 roof_y0, roof_y0 + rw_h * 0.55,
                 body_z0 + 0.10, body_z1 - 0.10)
            _box(boot_x1 - 0.04, roof_x0 + 0.04,
                 roof_y0 + rw_h * 0.45, roof_y1,
                 body_z0 + 0.10, body_z1 - 0.10)

            # Tailgate glass panel
            _box(boot_x0 - 0.02, boot_x1 + 0.02,
                 boot_y0 + 0.05, boot_y1 - 0.04,
                 body_z0 + 0.12, body_z1 - 0.12)

            # Vertical door division lines (front/rear door boundary)
            door_div_x = (body_x0 + body_x1) * 0.5 - 0.08
            div_d = 0.020
            _box(door_div_x - 0.03, door_div_x + 0.03,
                 body_y0 + 0.10, body_y1,
                 body_z0 - div_d, body_z0)
            _box(door_div_x - 0.03, door_div_x + 0.03,
                 body_y0 + 0.10, body_y1,
                 body_z1, body_z1 + div_d)

            # Exhaust stub
            _box(-2.05, -1.92, 0.06, 0.12, -0.15, 0.15)

            # Antenna stub
            _box(roof_x0 + 0.20, roof_x0 + 0.24,
                 roof_y1, roof_y1 + 0.18,
                 -0.02, 0.02)

            wheel_r = 0.28; wheel_w = 0.18; wheel_y = wheel_r
            _car_lod0_common(
                body_x0, body_x1, body_y0, body_y1, body_z0, body_z1,
                roof_x0, roof_x1, roof_y0, roof_y1,
                wheel_r, wheel_w, wheel_y,
                front_axle_x=body_x1 - 0.55,
                rear_axle_x=body_x0 + 0.55,
                mirror_x=body_x1 - 0.65,
            )

        else:  # lod == 1
            roof_x0 = body_x0 + 1.25; roof_x1 = body_x1 - 0.55
            roof_y0 = body_y1;         roof_y1 = body_y1 + 0.44
            _box(roof_x0, roof_x1, roof_y0, roof_y1, body_z0 + 0.08, body_z1 - 0.08)
            _box(body_x0, body_x0 + 1.1, body_y1, body_y1 + 0.22, body_z0 + 0.05, body_z1 - 0.05)
            wheel_r = 0.28; wheel_w = 0.18; wheel_y = wheel_r
            _car_lod1_common(
                body_x0, body_x1, body_y0, body_y1, body_z0, body_z1,
                wheel_r, wheel_w, wheel_y,
                front_axle_x=body_x1 - 0.55,
                rear_axle_x=body_x0 + 0.55,
            )

    # ------------------------------------------------------------------
    elif vtype == "car_hatchback":
        body_x0, body_x1 = -1.9, 1.9
        body_y0, body_y1 = 0.0,  0.74
        body_z0, body_z1 = -0.85, 0.85

        _box(body_x0, body_x1, body_y0, body_y0 + 0.25, body_z0, body_z1)
        _box(body_x0, body_x1, body_y0 + 0.25, body_y1 - 0.15, body_z0, body_z1)
        _box(body_x0, body_x1, body_y1 - 0.15, body_y1, body_z0, body_z1)

        if lod == 0:
            roof_x0 = body_x0 + 0.45; roof_x1 = body_x1 - 0.55
            roof_y0 = body_y1;         roof_y1 = body_y1 + 0.50
            _box(roof_x0, roof_x1, roof_y0, roof_y1, body_z0 + 0.08, body_z1 - 0.08)

            # Rear near-vertical hatch face extension
            hatch_t = 0.05
            _box(body_x0, body_x0 + hatch_t,
                 body_y1, roof_y1,
                 body_z0 + 0.06, body_z1 - 0.06)

            # D-pillar geometry (hatchback distinguishing feature)
            _box(body_x0, body_x0 + 0.12,
                 roof_y0 - 0.05, roof_y1,
                 body_z0 + 0.08, body_z1 - 0.08)

            # Rear near-vertical glass panel
            _box(body_x0 - 0.02, body_x0 + hatch_t + 0.02,
                 body_y1 + 0.05, roof_y1 - 0.04,
                 body_z0 + 0.12, body_z1 - 0.12)

            # Vertical door division
            door_div_x = (body_x0 + body_x1) * 0.5 - 0.05
            div_d = 0.020
            _box(door_div_x - 0.03, door_div_x + 0.03,
                 body_y0 + 0.10, body_y1,
                 body_z0 - div_d, body_z0)
            _box(door_div_x - 0.03, door_div_x + 0.03,
                 body_y0 + 0.10, body_y1,
                 body_z1, body_z1 + div_d)

            # Exhaust
            _box(-1.95, -1.82, 0.06, 0.12, -0.18, 0.12)

            # Hatch-specific rear spoiler lip
            _box(body_x0 - 0.02, body_x0 + 0.04,
                 roof_y1 - 0.04, roof_y1 + 0.06,
                 body_z0 + 0.10, body_z1 - 0.10)

            wheel_r = 0.28; wheel_w = 0.18; wheel_y = wheel_r
            _car_lod0_common(
                body_x0, body_x1, body_y0, body_y1, body_z0, body_z1,
                roof_x0, roof_x1, roof_y0, roof_y1,
                wheel_r, wheel_w, wheel_y,
                front_axle_x=body_x1 - 0.50,
                rear_axle_x=body_x0 + 0.50,
                mirror_x=body_x1 - 0.60,
            )

        else:  # lod == 1
            roof_x0 = body_x0 + 0.45; roof_x1 = body_x1 - 0.55
            roof_y0 = body_y1;         roof_y1 = body_y1 + 0.50
            _box(roof_x0, roof_x1, roof_y0, roof_y1, body_z0 + 0.08, body_z1 - 0.08)
            _box(body_x0, body_x0 + 0.05, body_y1, roof_y1, body_z0 + 0.06, body_z1 - 0.06)
            wheel_r = 0.28; wheel_w = 0.18; wheel_y = wheel_r
            _car_lod1_common(
                body_x0, body_x1, body_y0, body_y1, body_z0, body_z1,
                wheel_r, wheel_w, wheel_y,
                front_axle_x=body_x1 - 0.50,
                rear_axle_x=body_x0 + 0.50,
            )

    # ------------------------------------------------------------------
    elif vtype == "car_suv":
        body_x0, body_x1 = -2.2, 2.2
        body_y0, body_y1 = 0.0,  0.90
        body_z0, body_z1 = -0.95, 0.95

        _box(body_x0, body_x1, body_y0, body_y0 + 0.30, body_z0, body_z1)
        _box(body_x0, body_x1, body_y0 + 0.30, body_y1 - 0.18, body_z0, body_z1)
        _box(body_x0, body_x1, body_y1 - 0.18, body_y1, body_z0, body_z1)

        if lod == 0:
            roof_x0 = body_x0 + 0.50; roof_x1 = body_x1 - 0.55
            roof_y0 = body_y1;         roof_y1 = body_y1 + 0.42
            _box(roof_x0, roof_x1, roof_y0, roof_y1, body_z0 + 0.10, body_z1 - 0.10)

            # Roof rack rails
            rail_y0 = roof_y1; rail_y1 = roof_y1 + 0.04
            rail_z_off = (body_z1 - body_z0) * 0.10
            _box(roof_x0 + 0.10, roof_x1 - 0.10, rail_y0, rail_y1,
                 body_z0 + rail_z_off, body_z0 + rail_z_off + 0.06)
            _box(roof_x0 + 0.10, roof_x1 - 0.10, rail_y0, rail_y1,
                 body_z1 - rail_z_off - 0.06, body_z1 - rail_z_off)

            # Side cladding panel (thick plastic trim below door line)
            clad_y1 = body_y0 + body_y1 * 0.38; clad_d = 0.035
            _box(body_x0 + 0.30, body_x1 - 0.30,
                 body_y0, clad_y1,
                 body_z0 - clad_d, body_z0 - 0.002)
            _box(body_x0 + 0.30, body_x1 - 0.30,
                 body_y0, clad_y1,
                 body_z1 + 0.002, body_z1 + clad_d)

            # Front skid plate
            _box(body_x1 - 0.05, body_x1 + 0.08,
                 body_y0, body_y0 + 0.22,
                 body_z0 + 0.08, body_z1 - 0.08)

            # Antenna stub on roof
            _box(roof_x0 + 0.20, roof_x0 + 0.24,
                 roof_y1, roof_y1 + 0.15,
                 -0.02, 0.02)

            # Vertical door division
            door_div_x = (body_x0 + body_x1) * 0.5
            div_d = 0.022
            _box(door_div_x - 0.04, door_div_x + 0.04,
                 body_y0 + 0.15, body_y1,
                 body_z0 - div_d, body_z0)
            _box(door_div_x - 0.04, door_div_x + 0.04,
                 body_y0 + 0.15, body_y1,
                 body_z1, body_z1 + div_d)

            # Twin exhausts
            _box(-2.25, -2.10, 0.10, 0.18, -0.25, -0.05)
            _box(-2.25, -2.10, 0.10, 0.18,  0.05,  0.25)

            wheel_r = 0.34; wheel_w = 0.22; wheel_y = wheel_r + 0.06
            _car_lod0_common(
                body_x0, body_x1, body_y0, body_y1, body_z0, body_z1,
                roof_x0, roof_x1, roof_y0, roof_y1,
                wheel_r, wheel_w, wheel_y,
                front_axle_x=body_x1 - 0.58,
                rear_axle_x=body_x0 + 0.58,
                mirror_x=body_x1 - 0.65,
            )

        else:  # lod == 1
            roof_x0 = body_x0 + 0.50; roof_x1 = body_x1 - 0.55
            roof_y0 = body_y1;         roof_y1 = body_y1 + 0.42
            _box(roof_x0, roof_x1, roof_y0, roof_y1, body_z0 + 0.10, body_z1 - 0.10)
            _box(roof_x0 + 0.10, roof_x1 - 0.10, roof_y1, roof_y1 + 0.04,
                 body_z0 + 0.16, body_z1 - 0.16)
            wheel_r = 0.34; wheel_w = 0.22; wheel_y = wheel_r + 0.06
            _car_lod1_common(
                body_x0, body_x1, body_y0, body_y1, body_z0, body_z1,
                wheel_r, wheel_w, wheel_y,
                front_axle_x=body_x1 - 0.58,
                rear_axle_x=body_x0 + 0.58,
            )

    # ------------------------------------------------------------------
    elif vtype == "bus_standard":
        body_x0, body_x1 = -5.5, 5.5
        body_y0, body_y1 = 0.30, 2.80
        body_z0, body_z1 = -1.10, 1.10

        # Body in 3 horizontal zones
        _box(body_x0, body_x1, body_y0, body_y0 + 0.55, body_z0, body_z1)
        _box(body_x0, body_x1, body_y0 + 0.55, body_y1 - 0.52, body_z0, body_z1)
        _box(body_x0, body_x1, body_y1 - 0.52, body_y1, body_z0, body_z1)

        if lod == 0:
            wheel_r = 0.50; wheel_w = 0.28
            wheel_y = body_y0 - wheel_r * 0.05

            # ---- 3 axles (front + 2 rear) × 2 sides = 6 wheel positions ----
            for wx in [body_x1 - 0.90, body_x0 + 1.60, body_x0 + 0.90]:
                for wz_c in [body_z0 - wheel_w * 0.3, body_z1 + wheel_w * 0.3]:
                    _full_wheel(wx, wheel_y, wz_c, wheel_r, wheel_w, 16, 6)
                    _wheel_arch(wx, wheel_y - wheel_r * 0.05, wz_c, wheel_r, wheel_w)

            # ---- Passenger windows: 8 per side (window pane + frame) ----
            win_y0 = body_y0 + 0.90; win_h = 0.72; win_t = 0.020
            win_x_step = (body_x1 - body_x0 - 1.10) / 8.0
            for wi in range(8):
                wx0 = body_x0 + 0.55 + wi * win_x_step + 0.04
                wx1 = wx0 + win_x_step - 0.08
                # pane
                for wz_face, wz_in in [(body_z0, body_z0 - win_t),
                                        (body_z1, body_z1 + win_t)]:
                    _box(wx0, wx1, win_y0, win_y0 + win_h, wz_in, wz_face)
                # frame strip above
                _box(wx0 - 0.02, wx1 + 0.02,
                     win_y0 + win_h, win_y0 + win_h + 0.06,
                     body_z0 - 0.008, body_z1 + 0.008)

            # ---- Floor-level skirt panels ----
            _box(body_x0 + 0.20, body_x1 - 0.20,
                 body_y0 - 0.10, body_y0 + 0.06,
                 body_z0 - 0.03, body_z0)
            _box(body_x0 + 0.20, body_x1 - 0.20,
                 body_y0 - 0.10, body_y0 + 0.06,
                 body_z1, body_z1 + 0.03)

            # ---- Front face detail ----
            # Windscreen panel (inset)
            _box(body_x1 - 0.05, body_x1 + 0.02,
                 body_y0 + 0.55, body_y1 - 0.52,
                 body_z0 + 0.12, body_z1 - 0.12)
            # Headlight units (2)
            for hz in [body_z0 + 0.12, body_z1 - 0.42]:
                _box(body_x1 - 0.04, body_x1 + 0.04,
                     body_y0 + 0.30, body_y0 + 0.55,
                     hz, hz + 0.28)
            # Destination blind frame
            blind_t = 0.06
            _box(body_x1 - blind_t, body_x1 + 0.02,
                 body_y1 - 0.38, body_y1,
                 body_z0 + 0.10, body_z1 - 0.10)
            # Front bumper
            _box(body_x1 - 0.04, body_x1 + 0.07,
                 body_y0 - 0.05, body_y0 + 0.22,
                 body_z0 + 0.05, body_z1 - 0.05)

            # ---- Rear face detail ----
            # Rear window
            _box(body_x0 - 0.03, body_x0 + 0.02,
                 body_y0 + 0.80, body_y1 - 0.45,
                 body_z0 + 0.20, body_z1 - 0.20)
            # Taillights (2)
            for tz in [body_z0 + 0.10, body_z1 - 0.34]:
                _box(body_x0 - 0.04, body_x0 + 0.02,
                     body_y0 + 0.30, body_y0 + 0.65,
                     tz, tz + 0.22)
            # Engine vent louvres (4 bars)
            for i in range(4):
                vy = body_y0 + 0.35 + i * 0.28
                _box(body_x0 - 0.04, body_x0,
                     vy, vy + 0.08,
                     body_z0 + 0.18, body_z1 - 0.18)
            # Emergency door panel
            _box(body_x0 - 0.05, body_x0 + 0.02,
                 body_y0 + 0.10, body_y0 + 1.90,
                 body_z0 + 0.38, body_z1 - 0.38)
            # Rear bumper
            _box(body_x0 - 0.06, body_x0 + 0.02,
                 body_y0 - 0.05, body_y0 + 0.22,
                 body_z0 + 0.08, body_z1 - 0.08)

            # ---- Folding door frame (right side front) ----
            door_x0 = body_x1 - 1.40; door_x1 = body_x1 - 0.20; door_t = 0.04
            _box(door_x0, door_x1, body_y0, body_y0 + 1.90, body_z1, body_z1 + door_t)

            # ---- Step area (3 steps into front door) ----
            for si in range(3):
                sy = body_y0 - 0.12 - si * 0.12
                _box(body_x1 - 1.30, body_x1 - 0.30,
                     sy, sy + 0.10,
                     body_z1, body_z1 + 0.06 + si * 0.04)

            # ---- Roof detail ----
            # AC box
            ac_x0 = 0.0; ac_x1 = body_x1 - 0.60
            _box(ac_x0, ac_x1, body_y1, body_y1 + 0.28, body_z0 + 0.25, body_z1 - 0.25)
            # AC fins (8 strips)
            for fi in range(8):
                fx = ac_x0 + 0.15 + fi * (ac_x1 - ac_x0 - 0.30) / 7.0
                _box(fx - 0.03, fx + 0.03,
                     body_y1 + 0.28, body_y1 + 0.44,
                     body_z0 + 0.28, body_z1 - 0.28)
            # Skylight hatch
            _box(body_x0 + 1.20, body_x0 + 1.80,
                 body_y1, body_y1 + 0.04,
                 body_z0 + 0.30, body_z1 - 0.30)
            # Roof rail strips (2 longitudinal)
            _box(body_x0 + 0.10, body_x1 - 0.10,
                 body_y1, body_y1 + 0.06,
                 body_z0 + 0.04, body_z0 + 0.10)
            _box(body_x0 + 0.10, body_x1 - 0.10,
                 body_y1, body_y1 + 0.06,
                 body_z1 - 0.10, body_z1 - 0.04)

            # ---- Waistband strip (body belt-line) ----
            wb_y = body_y0 + 0.62; wb_h = 0.04; wb_d = 0.015
            _box(body_x0 + 0.10, body_x1 - 0.10, wb_y, wb_y + wb_h,
                 body_z0 - wb_d, body_z0)
            _box(body_x0 + 0.10, body_x1 - 0.10, wb_y, wb_y + wb_h,
                 body_z1, body_z1 + wb_d)

            # ---- Body panel joint lines (5 joints × 2 sides) ----
            for jx in [body_x0 + 1.8, body_x0 + 3.6, 0.0, body_x1 - 3.6, body_x1 - 1.8]:
                jd = 0.012
                _box(jx - 0.02, jx + 0.02, body_y0 + 0.10, body_y1 - 0.10,
                     body_z0 - jd, body_z0)
                _box(jx - 0.02, jx + 0.02, body_y0 + 0.10, body_y1 - 0.10,
                     body_z1, body_z1 + jd)

            # ---- Window sill ledges (below each passenger window, both sides) ----
            win_x_step = (body_x1 - body_x0 - 1.10) / 8.0
            sill_h = 0.06; sill_d = 0.018
            sill_y_top = body_y0 + 0.90
            for wi in range(8):
                wx0 = body_x0 + 0.55 + wi * win_x_step + 0.04
                wx1 = wx0 + win_x_step - 0.08
                _box(wx0, wx1, sill_y_top - sill_h, sill_y_top,
                     body_z0 - sill_d, body_z0)
                _box(wx0, wx1, sill_y_top - sill_h, sill_y_top,
                     body_z1, body_z1 + sill_d)

            # ---- Rear upper window ----
            _box(body_x0 - 0.03, body_x0 + 0.02,
                 body_y1 - 0.48, body_y1 - 0.04,
                 body_z0 + 0.25, body_z1 - 0.25)

            # ---- Underside skirting detail (side skirt lower rim) ----
            for xi in range(6):
                sx = body_x0 + 0.60 + xi * (body_x1 - body_x0 - 1.20) / 5.0
                _box(sx - 0.25, sx + 0.25,
                     body_y0 - 0.08, body_y0 + 0.05,
                     body_z0 - 0.04, body_z0)
                _box(sx - 0.25, sx + 0.25,
                     body_y0 - 0.08, body_y0 + 0.05,
                     body_z1, body_z1 + 0.04)

            # ---- Indicator/signal light strips (front + rear each side) ----
            ind_w = 0.18; ind_h = 0.10; ind_d = 0.025
            for iz in [body_z0 + 0.04, body_z1 - 0.04 - ind_w]:
                # front
                _box(body_x1 - ind_d, body_x1 + ind_d,
                     body_y0 + 0.30, body_y0 + 0.30 + ind_h,
                     iz, iz + ind_w)
                # rear
                _box(body_x0 - ind_d, body_x0 + ind_d,
                     body_y0 + 0.30, body_y0 + 0.30 + ind_h,
                     iz, iz + ind_w)

            # ---- Driver's cab subdivision panels (front upper face) ----
            for di in range(3):
                dx = body_x1 - 0.55 + di * 0.16
                _box(dx, dx + 0.14,
                     body_y1 - 0.52, body_y1,
                     body_z0 + 0.05, body_z1 - 0.05)

            # ---- Roof edge gutters (front + rear overhangs) ----
            _box(body_x1 - 0.06, body_x1 + 0.04,
                 body_y1 - 0.04, body_y1 + 0.04,
                 body_z0 + 0.04, body_z1 - 0.04)
            _box(body_x0 - 0.04, body_x0 + 0.06,
                 body_y1 - 0.04, body_y1 + 0.04,
                 body_z0 + 0.04, body_z1 - 0.04)

            # ---- AC duct connector strips (3 ducts from AC unit) ----
            ac_x0 = 0.0; ac_x1 = body_x1 - 0.60
            for di in range(3):
                dz = body_z0 + 0.30 + di * ((body_z1 - body_z0 - 0.60) / 2.0)
                _box(ac_x0 + 0.20, ac_x0 + 0.50,
                     body_y1 + 0.28, body_y1 + 0.42,
                     dz, dz + 0.14)

            # ---- Axle housing boxes (visible underbelly at each axle) ----
            for ax_wx in [body_x1 - 0.90, body_x0 + 1.60, body_x0 + 0.90]:
                _box(ax_wx - 0.35, ax_wx + 0.35,
                     0.04, 0.18,
                     body_z0 + 0.12, body_z1 - 0.12)

            # ---- Side mirror assemblies (large bus mirrors) ----
            mx = body_x1 - 0.30; my = body_y1 - 0.52
            # left
            _box(mx - 0.06, mx + 0.06, my, my + 0.08, body_z0 - 0.22, body_z0 - 0.06)
            _box(mx - 0.20, mx + 0.20, my - 0.04, my + 0.22, body_z0 - 0.28, body_z0 - 0.18)
            # right
            _box(mx - 0.06, mx + 0.06, my, my + 0.08, body_z1 + 0.06, body_z1 + 0.22)
            _box(mx - 0.20, mx + 0.20, my - 0.04, my + 0.22, body_z1 + 0.18, body_z1 + 0.28)

            # ---- Fuel filler hatch (right side rear quarter) ----
            _box(body_x0 + 1.10, body_x0 + 1.50,
                 body_y0 + 0.55, body_y0 + 0.88,
                 body_z1 - 0.012, body_z1 + 0.012)

            # ---- Service hatch panels (2 per side, lower body) ----
            for hx0, hx1 in [(body_x0 + 1.60, body_x0 + 2.40),
                               (body_x0 + 2.50, body_x0 + 3.30)]:
                _box(hx0, hx1, body_y0, body_y0 + 0.52,
                     body_z0 - 0.012, body_z0)
                _box(hx0, hx1, body_y0, body_y0 + 0.52,
                     body_z1, body_z1 + 0.012)

            # ---- Roof ventilation hatches (4 additional along roof) ----
            for vi in range(4):
                vx = body_x0 + 1.20 + vi * 2.10
                _box(vx, vx + 0.55,
                     body_y1, body_y1 + 0.05,
                     body_z0 + 0.38, body_z1 - 0.38)

            # ---- Number plate panels ----
            _box(body_x1 - 0.02, body_x1 + 0.03,
                 body_y0 + 0.35, body_y0 + 0.62,
                 -0.28, 0.28)
            _box(body_x0 - 0.03, body_x0 + 0.02,
                 body_y0 + 0.32, body_y0 + 0.58,
                 -0.26, 0.26)

            # ---- Lower front face horizontal bands (3 bands) ----
            for bi in range(3):
                by = body_y0 + bi * 0.18
                _box(body_x1 - 0.03, body_x1 + 0.03,
                     by, by + 0.10,
                     body_z0 + 0.08, body_z1 - 0.08)

            # ---- Rear face additional detail bands ----
            for bi in range(3):
                by = body_y0 + bi * 0.18
                _box(body_x0 - 0.03, body_x0 + 0.03,
                     by, by + 0.10,
                     body_z0 + 0.08, body_z1 - 0.08)

            # ---- Interior ceiling strip lights (visible through windows as thin strips) ----
            for li in range(6):
                lx = body_x0 + 0.80 + li * (body_x1 - body_x0 - 1.60) / 5.0
                _box(lx - 0.15, lx + 0.15,
                     body_y1 - 0.06, body_y1 - 0.01,
                     body_z0 + 0.15, body_z1 - 0.15)

            # ---- Upper side-panel detail strips (between windows and roof) ----
            up_d = 0.012; up_h = 0.10
            up_y0 = body_y0 + 0.90 + 0.72 + 0.06
            _box(body_x0 + 0.40, body_x1 - 0.40,
                 up_y0, up_y0 + up_h,
                 body_z0 - up_d, body_z0)
            _box(body_x0 + 0.40, body_x1 - 0.40,
                 up_y0, up_y0 + up_h,
                 body_z1, body_z1 + up_d)

            # ---- Lower-body reinforcement ribs (6 ribs each side) ----
            for ri in range(6):
                rx = body_x0 + 0.70 + ri * (body_x1 - body_x0 - 1.40) / 5.0
                rib_d = 0.020
                _box(rx - 0.06, rx + 0.06,
                     body_y0, body_y0 + 0.55,
                     body_z0 - rib_d, body_z0)
                _box(rx - 0.06, rx + 0.06,
                     body_y0, body_y0 + 0.55,
                     body_z1, body_z1 + rib_d)

            # ---- Rear emergency exit handle strips ----
            _box(body_x0 - 0.03, body_x0 + 0.02,
                 body_y0 + 1.50, body_y0 + 1.55,
                 body_z0 + 0.40, body_z1 - 0.40)

            # ---- Undercarriage transmission/driveshaft cover ----
            _box(body_x0 + 0.80, body_x1 - 0.80,
                 0.02, 0.14,
                 body_z0 + 0.18, body_z1 - 0.18)

            # ---- Roof antenna mast ----
            _box(body_x1 - 1.20, body_x1 - 1.16,
                 body_y1 + 0.04, body_y1 + 0.50,
                 -0.02, 0.02)

            # ---- Front corner radius pillars ----
            for fz in [body_z0 + 0.06, body_z1 - 0.14]:
                _box(body_x1 - 0.12, body_x1 + 0.03,
                     body_y0 + 0.15, body_y1 - 0.15,
                     fz, fz + 0.08)

            # ---- Wheel arch inner liner strips ----
            for ax_wx in [body_x1 - 0.90, body_x0 + 1.60, body_x0 + 0.90]:
                for wz_c in [body_z0 - wheel_w * 0.3, body_z1 + wheel_w * 0.3]:
                    arch_hw = wheel_r * 1.1
                    wz_in0 = wz_c - wheel_w * 0.5 - 0.02
                    wz_in1 = wz_c + wheel_w * 0.5 + 0.02
                    _box(ax_wx - arch_hw, ax_wx + arch_hw,
                         wheel_y - wheel_r * 0.05,
                         wheel_y - wheel_r * 0.05 + 0.08,
                         wz_in0 - 0.018, wz_in0)
                    _box(ax_wx - arch_hw, ax_wx + arch_hw,
                         wheel_y - wheel_r * 0.05,
                         wheel_y - wheel_r * 0.05 + 0.08,
                         wz_in1, wz_in1 + 0.018)

            # ---- Additional passenger window subdivisions (vertical divider per window) ----
            win_x_step2 = (body_x1 - body_x0 - 1.10) / 8.0
            win_y0b = body_y0 + 0.90; win_hb = 0.72; wt2 = 0.016
            for wi in range(8):
                wx0 = body_x0 + 0.55 + wi * win_x_step2 + 0.04
                wx_c = wx0 + (win_x_step2 - 0.08) * 0.5
                _box(wx_c - 0.02, wx_c + 0.02,
                     win_y0b, win_y0b + win_hb,
                     body_z0 - wt2, body_z0)
                _box(wx_c - 0.02, wx_c + 0.02,
                     win_y0b, win_y0b + win_hb,
                     body_z1, body_z1 + wt2)

            # ---- Front face lower grille bars (5 horizontal bars) ----
            for gi in range(5):
                gy = body_y0 + 0.05 + gi * 0.10
                _box(body_x1 - 0.03, body_x1 + 0.03,
                     gy, gy + 0.06,
                     body_z0 + 0.12, body_z1 - 0.12)

            # ---- Side mirror lower brace ----
            mx = body_x1 - 0.30; my = body_y1 - 0.52
            _box(mx - 0.04, mx + 0.04,
                 my - 0.16, my,
                 body_z0 - 0.22, body_z0 - 0.08)
            _box(mx - 0.04, mx + 0.04,
                 my - 0.16, my,
                 body_z1 + 0.08, body_z1 + 0.22)

            # ---- Route indicator board (side, front quarter) ----
            _box(body_x1 - 0.50, body_x1 - 0.08,
                 body_y1 - 0.38, body_y1 - 0.04,
                 body_z1 + 0.002, body_z1 + 0.016)

            # ---- Rear passenger window subdivisions ----
            for ri in range(2):
                rx = body_x0 + 1.80 + ri * 1.20
                _box(rx, rx + 0.80,
                     body_y0 + 0.90, body_y0 + 0.90 + 0.72,
                     body_z0 - 0.018, body_z0)
                _box(rx, rx + 0.80,
                     body_y0 + 0.90, body_y0 + 0.90 + 0.72,
                     body_z1, body_z1 + 0.018)

        else:  # lod == 1
            # Simplified wheels (8-seg)
            wheel_r = 0.50; wheel_w = 0.28
            wheel_y = body_y0 - wheel_r * 0.05
            for wx in [body_x1 - 0.90, body_x0 + 0.90]:
                for wz_c in [body_z0 - wheel_w * 0.3, body_z1 + wheel_w * 0.3]:
                    _disk_n(wx, wheel_y, wz_c, wheel_r, wheel_w, 8)
                    _wheel_arch(wx, wheel_y - wheel_r * 0.05, wz_c, wheel_r, wheel_w)
            # Window strip bands per side
            win_y0 = body_y0 + 0.90; win_h = 0.72; win_t = 0.018
            for wz_face, wz_in in [(body_z0, body_z0 - win_t),
                                    (body_z1, body_z1 + win_t)]:
                _box(body_x0 + 0.40, body_x1 - 0.40, win_y0, win_y0 + win_h,
                     wz_in, wz_face)
            # Destination blind
            _box(body_x1 - 0.06, body_x1 + 0.02, body_y1 - 0.38, body_y1,
                 body_z0 + 0.10, body_z1 - 0.10)
            # Door frame
            _box(body_x1 - 1.40, body_x1 - 0.20, body_y0, body_y0 + 1.90,
                 body_z1, body_z1 + 0.04)
            # AC box
            _box(0.0, body_x1 - 0.60, body_y1, body_y1 + 0.28,
                 body_z0 + 0.25, body_z1 - 0.25)
            # Bumpers
            _box(body_x1 - 0.04, body_x1 + 0.06, body_y0 - 0.05, body_y0 + 0.20,
                 body_z0 + 0.08, body_z1 - 0.08)
            _box(body_x0 - 0.06, body_x0 + 0.04, body_y0 - 0.05, body_y0 + 0.20,
                 body_z0 + 0.08, body_z1 - 0.08)
            # Vent louvres
            for i in range(4):
                vy = body_y0 + 0.35 + i * 0.28
                _box(body_x0 - 0.04, body_x0, vy, vy + 0.08,
                     body_z0 + 0.18, body_z1 - 0.18)
            # Belt strip
            wb_y = body_y0 + 0.62; wb_h = 0.04; wb_d = 0.012
            _box(body_x0 + 0.10, body_x1 - 0.10, wb_y, wb_y + wb_h,
                 body_z0 - wb_d, body_z0)
            _box(body_x0 + 0.10, body_x1 - 0.10, wb_y, wb_y + wb_h,
                 body_z1, body_z1 + wb_d)
            # Headlights
            for hz in [body_z0 + 0.12, body_z1 - 0.42]:
                _box(body_x1 - 0.04, body_x1 + 0.04, body_y0 + 0.30, body_y0 + 0.55,
                     hz, hz + 0.28)
            # Taillights
            for tz in [body_z0 + 0.10, body_z1 - 0.34]:
                _box(body_x0 - 0.04, body_x0 + 0.02, body_y0 + 0.30, body_y0 + 0.65,
                     tz, tz + 0.22)
            # Additional wheel detail (2 extra axle arches for LOD1)
            for wx in [body_x0 + 1.60]:
                for wz_c in [body_z0 - wheel_w * 0.3, body_z1 + wheel_w * 0.3]:
                    _disk_n(wx, wheel_y, wz_c, wheel_r, wheel_w, 8)
                    _wheel_arch(wx, wheel_y - wheel_r * 0.05, wz_c, wheel_r, wheel_w)
            # Roof silhouette strip
            _box(body_x0 + 0.10, body_x1 - 0.10,
                 body_y1, body_y1 + 0.06,
                 body_z0 + 0.10, body_z1 - 0.10)
            # Skirt lower strip
            _box(body_x0 + 0.20, body_x1 - 0.20,
                 body_y0 - 0.08, body_y0 + 0.04,
                 body_z0 - 0.025, body_z0)
            _box(body_x0 + 0.20, body_x1 - 0.20,
                 body_y0 - 0.08, body_y0 + 0.04,
                 body_z1, body_z1 + 0.025)

    # ------------------------------------------------------------------
    elif vtype == "truck_cargo":
        cab_x0 = 0.60; cab_x1 = 4.0
        cab_y0 = 0.32; cab_y1 = 2.50
        cab_z0 = -1.00; cab_z1 = 1.00

        cargo_x0 = -4.0; cargo_x1 = 0.40
        cargo_y0 = 0.32; cargo_y1 = 2.50
        cargo_z0 = -0.98; cargo_z1 = 0.98

        # Cab in 2 vertical zones; cargo body
        _box(cab_x0, cab_x1, cab_y0, cab_y0 + 0.80, cab_z0, cab_z1)
        _box(cab_x0, cab_x1, cab_y0 + 0.80, cab_y1, cab_z0, cab_z1)
        _box(cargo_x0, cargo_x1, cargo_y0, cargo_y1, cargo_z0, cargo_z1)

        if lod == 0:
            wheel_r = 0.42; wheel_w = 0.24; wheel_y = wheel_r

            # ---- 10 wheels: 1 front axle (2 wheels) + 2 rear axles (4 wheels each side) ----
            # front axle
            for wz_c in [cab_z0 - wheel_w * 0.3, cab_z1 + wheel_w * 0.3]:
                _full_wheel(cab_x1 - 0.60, wheel_y, wz_c, wheel_r, wheel_w, 16, 8)
                _wheel_arch(cab_x1 - 0.60, wheel_y - wheel_r * 0.10, wz_c, wheel_r, wheel_w)
            # rear axle 1 — outer + inner dual wheels each side
            outer_sep = wheel_w * 0.30; inner_sep = wheel_w * 1.10
            for ra_wx in [cargo_x0 + 0.80, cargo_x0 + 1.60]:
                for side_mult, side_z_base in [(-1, cab_z0), (1, cab_z1)]:
                    wz_outer = side_z_base + side_mult * outer_sep
                    wz_inner = side_z_base + side_mult * inner_sep
                    _full_wheel(ra_wx, wheel_y, wz_outer, wheel_r, wheel_w, 16, 8)
                    _full_wheel(ra_wx, wheel_y, wz_inner, wheel_r, wheel_w, 16, 8)
                    _wheel_arch(ra_wx, wheel_y - wheel_r * 0.10, wz_outer, wheel_r, wheel_w)

            # ---- Cab front detail ----
            # Windscreen (inset panel)
            _box(cab_x1 - 0.04, cab_x1 + 0.02,
                 cab_y0 + 0.80, cab_y1 - 0.30,
                 cab_z0 + 0.12, cab_z1 - 0.12)
            # Headlight units (2 large)
            for hz in [cab_z0 + 0.10, cab_z1 - 0.38]:
                _box(cab_x1 - 0.04, cab_x1 + 0.06,
                     cab_y0 + 0.32, cab_y0 + 0.72,
                     hz, hz + 0.26)
            # Grille horizontal bars (6)
            for gi in range(6):
                gy = cab_y0 + 0.05 + gi * 0.12
                _box(cab_x1, cab_x1 + 0.06,
                     gy, gy + 0.06,
                     cab_z0 + 0.15, cab_z1 - 0.15)
            # Front bumper (3 sections)
            bmp_y0 = cab_y0 - 0.06; bmp_y1 = cab_y0 + 0.28
            for bz0, bz1 in [(cab_z0 - 0.04, cab_z0 + 0.36),
                              (cab_z0 + 0.36, cab_z1 - 0.36),
                              (cab_z1 - 0.36, cab_z1 + 0.04)]:
                _box(cab_x1 - 0.04, cab_x1 + 0.10, bmp_y0, bmp_y1, bz0, bz1)
            # Cab sun visor (strip above windscreen)
            _box(cab_x1 - 0.02, cab_x1 + 0.04,
                 cab_y1 - 0.35, cab_y1 - 0.28,
                 cab_z0 + 0.08, cab_z1 - 0.08)
            # Roof air deflector / spoiler
            _box(cab_x1 - 0.45, cab_x1 - 0.05,
                 cab_y1, cab_y1 + 0.28,
                 cab_z0 + 0.08, cab_z1 - 0.08)

            # ---- Cab door windows (2 per side) ----
            win_t = 0.020
            dw_y0 = cab_y0 + 0.90; dw_h = 0.70
            # front door window
            fdw_x0 = cab_x1 - 0.85; fdw_x1 = cab_x1 - 0.05
            for wz_face, wz_in in [(cab_z0, cab_z0 - win_t),
                                    (cab_z1, cab_z1 + win_t)]:
                _box(fdw_x0, fdw_x1, dw_y0, dw_y0 + dw_h, wz_in, wz_face)
            # rear cab window
            rcw_x0 = cab_x0 + 0.05; rcw_x1 = cab_x0 + 0.45
            for wz_face, wz_in in [(cab_z0, cab_z0 - win_t),
                                    (cab_z1, cab_z1 + win_t)]:
                _box(rcw_x0, rcw_x1, dw_y0, dw_y0 + dw_h * 0.6, wz_in, wz_face)

            # ---- Cab door panels (upper/lower division) ----
            dp_d = 0.018
            for dz_out, dz_in in [(cab_z0 - dp_d, cab_z0),
                                   (cab_z1, cab_z1 + dp_d)]:
                _box(cab_x0 + 0.05, cab_x1 - 0.05, cab_y0 + 0.32, dw_y0, dz_out, dz_in)
            # Door handles
            dh_d = 0.025; dh_w = 0.16; dh_h = 0.04; dh_y = cab_y0 + 0.72
            dh_x_c = (cab_x0 + cab_x1) * 0.5
            for dz_out, dz_in in [(cab_z0 - dh_d, cab_z0),
                                   (cab_z1, cab_z1 + dh_d)]:
                _box(dh_x_c - dh_w * 0.5, dh_x_c + dh_w * 0.5,
                     dh_y, dh_y + dh_h, dz_out, dz_in)

            # ---- Exhaust stack (8-sided tube approximation) ----
            stack_x = cab_x1 - 0.45; stack_r = 0.06; stack_segs = 8
            stack_z_c = cab_z1 - 0.14
            for si in range(stack_segs):
                a0 = 2 * math.pi * si / stack_segs
                a1 = 2 * math.pi * (si + 1) / stack_segs
                p0z = stack_z_c + stack_r * math.sin(a0)
                p0x_off = stack_r * math.cos(a0)
                p1z = stack_z_c + stack_r * math.sin(a1)
                p1x_off = stack_r * math.cos(a1)
                sy_bot = cab_y1; sy_top = cab_y1 + 0.65
                u0, vv0 = atlas_uv(row, col, si / stack_segs, 0.0)
                u1, vv1 = atlas_uv(row, col, (si + 1) / stack_segs, 0.0)
                u0t, vv0t = atlas_uv(row, col, si / stack_segs, 1.0)
                u1t, vv1t = atlas_uv(row, col, (si + 1) / stack_segs, 1.0)
                nx0 = math.sin(a0); nz0 = math.cos(a0)
                base = len(all_verts)
                all_verts.append(Vertex(stack_x + p0x_off, sy_bot, p0z, nx0, 0, nz0, u0,  vv0))
                all_verts.append(Vertex(stack_x + p1x_off, sy_bot, p1z, nx0, 0, nz0, u1,  vv1))
                all_verts.append(Vertex(stack_x + p1x_off, sy_top, p1z, nx0, 0, nz0, u1t, vv1t))
                all_verts.append(Vertex(stack_x + p0x_off, sy_top, p0z, nx0, 0, nz0, u0t, vv0t))
                all_tris.append((base, base + 1, base + 2))
                all_tris.append((base, base + 2, base + 3))

            # ---- Cargo box side panel ribs (5 vertical ribs each side) ----
            rib_d = 0.022; rib_w = 0.06
            for ri in range(5):
                rx = cargo_x0 + 0.80 + ri * (cargo_x1 - cargo_x0 - 1.0) / 4.0
                _box(rx - rib_w * 0.5, rx + rib_w * 0.5,
                     cargo_y0 + 0.05, cargo_y1 - 0.05,
                     cargo_z0 - rib_d, cargo_z0)
                _box(rx - rib_w * 0.5, rx + rib_w * 0.5,
                     cargo_y0 + 0.05, cargo_y1 - 0.05,
                     cargo_z1, cargo_z1 + rib_d)

            # ---- Cargo rear door (2 door halves + hinges) ----
            rd_t = 0.05
            _box(cargo_x0 - rd_t, cargo_x0,
                 cargo_y0 + 0.10, cargo_y1 - 0.10,
                 cargo_z0 + 0.15, cargo_z0 + (cargo_z1 - cargo_z0) * 0.5)
            _box(cargo_x0 - rd_t, cargo_x0,
                 cargo_y0 + 0.10, cargo_y1 - 0.10,
                 cargo_z0 + (cargo_z1 - cargo_z0) * 0.5, cargo_z1 - 0.15)
            # Hinge strips
            for hi in range(3):
                hy = cargo_y0 + 0.25 + hi * (cargo_y1 - cargo_y0 - 0.35) * 0.5
                _box(cargo_x0 - rd_t - 0.04, cargo_x0 + 0.01,
                     hy, hy + 0.08,
                     cargo_z0 + (cargo_z1 - cargo_z0) * 0.5 - 0.025,
                     cargo_z0 + (cargo_z1 - cargo_z0) * 0.5 + 0.025)
            # Dock bumpers (3 rubber stops)
            for di in range(3):
                dz = cargo_z0 + 0.25 + di * (cargo_z1 - cargo_z0 - 0.50) * 0.5
                _box(cargo_x0 - 0.08, cargo_x0 - 0.01,
                     cargo_y0 + 0.05, cargo_y0 + 0.22,
                     dz, dz + 0.10)

            # ---- Cab back wall (visible in gap) ----
            _box(cab_x0 - 0.04, cab_x0 + 0.02,
                 cab_y0, cab_y1,
                 cab_z0 + 0.05, cab_z1 - 0.05)

            # ---- Chassis frame rails + cross-members ----
            rail_y0 = 0.05; rail_y1 = 0.22; rail_d = 0.12
            _box(cargo_x0, cab_x1,
                 rail_y0, rail_y1,
                 cab_z0 + 0.15, cab_z0 + 0.15 + rail_d)
            _box(cargo_x0, cab_x1,
                 rail_y0, rail_y1,
                 cab_z1 - 0.15 - rail_d, cab_z1 - 0.15)
            # Cross-members
            for cmi in range(4):
                cmx = cargo_x0 + 0.60 + cmi * (cab_x1 - cargo_x0 - 0.80) / 3.0
                _box(cmx - 0.06, cmx + 0.06,
                     rail_y0, rail_y1,
                     cab_z0 + 0.15, cab_z1 - 0.15)
            # Fuel tank box
            _box(cargo_x0 + 0.20, cargo_x0 + 0.90,
                 0.10, 0.48,
                 cab_z0 - 0.04, cab_z0 + 0.14)
            # Battery box
            _box(cargo_x0 + 1.00, cargo_x0 + 1.50,
                 0.10, 0.35,
                 cab_z0 - 0.03, cab_z0 + 0.12)

            # ---- Side running boards (both sides) ----
            step_y = cab_y0 - 0.20
            _box(cab_x0 + 0.40, cab_x1 - 0.20,
                 step_y, step_y + 0.12,
                 cab_z0 - 0.12, cab_z0)
            _box(cab_x0 + 0.40, cab_x1 - 0.20,
                 step_y, step_y + 0.12,
                 cab_z1, cab_z1 + 0.12)

            # ---- Cab corner radius approximation strips ----
            cr_d = 0.025; cr_h_bot = cab_y0 + 0.18; cr_h_top = cab_y1 - 0.20
            for cz_pair in [(cab_z0 - cr_d, cab_z0), (cab_z1, cab_z1 + cr_d)]:
                _box(cab_x1 - 0.18, cab_x1 + 0.02, cr_h_bot, cr_h_top, *cz_pair)
                _box(cab_x0 - 0.02, cab_x0 + 0.18, cr_h_bot, cr_h_top, *cz_pair)

            # ---- Cab A-pillar strips ----
            ap_d = 0.018
            for cz_pair in [(cab_z0 - ap_d, cab_z0), (cab_z1, cab_z1 + ap_d)]:
                _box(cab_x1 - 0.55, cab_x1 - 0.40, cab_y0 + 0.80, cab_y1 - 0.10,
                     *cz_pair)

            # ---- Cab roof corner braces ----
            _box(cab_x1 - 0.52, cab_x1 - 0.05,
                 cab_y1 - 0.10, cab_y1 + 0.02,
                 cab_z0 + 0.06, cab_z0 + 0.16)
            _box(cab_x1 - 0.52, cab_x1 - 0.05,
                 cab_y1 - 0.10, cab_y1 + 0.02,
                 cab_z1 - 0.16, cab_z1 - 0.06)

            # ---- Taillights (2 each side of cargo rear) ----
            tl_w = 0.20; tl_h = 0.28; tl_d = 0.025
            tl_y = cargo_y0 + 0.16
            for tz in [cargo_z0 - tl_d, cargo_z1 - 0.02]:
                _box(cargo_x0 - tl_d, cargo_x0 + 0.02,
                     tl_y, tl_y + tl_h,
                     tz, tz + tl_w)

            # ---- Cargo roof rain gutter strips ----
            _box(cargo_x0, cargo_x1,
                 cargo_y1 - 0.04, cargo_y1 + 0.02,
                 cargo_z0 - 0.02, cargo_z0 + 0.04)
            _box(cargo_x0, cargo_x1,
                 cargo_y1 - 0.04, cargo_y1 + 0.02,
                 cargo_z1 - 0.04, cargo_z1 + 0.02)

            # ---- Cargo lower trim strip (skirt) ----
            _box(cargo_x0 + 0.10, cargo_x1 - 0.10,
                 cargo_y0 - 0.04, cargo_y0 + 0.08,
                 cargo_z0 - 0.025, cargo_z0)
            _box(cargo_x0 + 0.10, cargo_x1 - 0.10,
                 cargo_y0 - 0.04, cargo_y0 + 0.08,
                 cargo_z1, cargo_z1 + 0.025)

            # ---- Cab headlight surrounds (bezels) ----
            for hz in [cab_z0 + 0.08, cab_z1 - 0.40]:
                _box(cab_x1 - 0.025, cab_x1 + 0.06,
                     cab_y0 + 0.28, cab_y0 + 0.76,
                     hz - 0.02, hz + 0.30)

            # ---- Cab grille surround frame ----
            _box(cab_x1 - 0.02, cab_x1 + 0.08,
                 cab_y0 + 0.02, cab_y0 + 0.80,
                 cab_z0 + 0.12, cab_z0 + 0.16)
            _box(cab_x1 - 0.02, cab_x1 + 0.08,
                 cab_y0 + 0.02, cab_y0 + 0.80,
                 cab_z1 - 0.16, cab_z1 - 0.12)

            # ---- Cab fog lights ----
            for fz in [cab_z0 + 0.06, cab_z1 - 0.22]:
                _box(cab_x1 - 0.03, cab_x1 + 0.05,
                     cab_y0 + 0.05, cab_y0 + 0.22,
                     fz, fz + 0.14)

            # ---- Undercarriage additional geometry ----
            # Transmission hump
            _box(cab_x0 + 0.20, cab_x1 - 0.30,
                 0.22, 0.42,
                 -0.18, 0.18)
            # Spare wheel carrier (under cargo rear)
            _box(cargo_x0 + 0.10, cargo_x0 + 0.65,
                 0.06, 0.48,
                 -0.30, 0.30)

            # ---- Cargo box corner angle-irons (4 vertical corners) ----
            ai_d = 0.05
            for cz_pair in [(cargo_z0 - ai_d, cargo_z0 + ai_d),
                             (cargo_z1 - ai_d, cargo_z1 + ai_d)]:
                _box(cargo_x0 - ai_d, cargo_x0 + ai_d,
                     cargo_y0, cargo_y1,
                     *cz_pair)
                _box(cargo_x1 - ai_d, cargo_x1 + ai_d,
                     cargo_y0, cargo_y1,
                     *cz_pair)

            # ---- Cargo top horizontal reinforcement bars (3 bars) ----
            for bi in range(3):
                bx = cargo_x0 + 0.80 + bi * (cargo_x1 - cargo_x0 - 1.0) / 2.0
                _box(bx - 0.06, bx + 0.06,
                     cargo_y1 - 0.08, cargo_y1 + 0.02,
                     cargo_z0, cargo_z1)

            # ---- Air intake scoop on cab roof ----
            _box(cab_x0 + 0.20, cab_x0 + 0.55,
                 cab_y1 - 0.05, cab_y1 + 0.14,
                 cab_z0 + 0.20, cab_z1 - 0.20)

            # ---- Rear view mirrors (cab, left + right) ----
            # Arm
            _box(cab_x1 - 0.20, cab_x1 + 0.04,
                 cab_y1 - 0.50, cab_y1 - 0.44,
                 cab_z0 - 0.18, cab_z0 - 0.06)
            _box(cab_x1 - 0.20, cab_x1 + 0.04,
                 cab_y1 - 0.50, cab_y1 - 0.44,
                 cab_z1 + 0.06, cab_z1 + 0.18)
            # Head
            _box(cab_x1 - 0.05, cab_x1 + 0.04,
                 cab_y1 - 0.65, cab_y1 - 0.35,
                 cab_z0 - 0.32, cab_z0 - 0.14)
            _box(cab_x1 - 0.05, cab_x1 + 0.04,
                 cab_y1 - 0.65, cab_y1 - 0.35,
                 cab_z1 + 0.14, cab_z1 + 0.32)

            # ---- Number plate panels ----
            _box(cab_x1 - 0.02, cab_x1 + 0.04,
                 cab_y0 + 0.30, cab_y0 + 0.56,
                 -0.22, 0.22)
            _box(cargo_x0 - 0.03, cargo_x0 + 0.02,
                 cargo_y0 + 0.12, cargo_y0 + 0.36,
                 -0.20, 0.20)

            # ---- Additional cargo side panel detail ----
            # Horizontal reinforcement rails (2 per side at mid and low)
            for ry_frac in [0.35, 0.65]:
                ry = cargo_y0 + (cargo_y1 - cargo_y0) * ry_frac
                rr_d = 0.022; rr_h = 0.06
                _box(cargo_x0 + 0.10, cargo_x1 - 0.10,
                     ry, ry + rr_h,
                     cargo_z0 - rr_d, cargo_z0)
                _box(cargo_x0 + 0.10, cargo_x1 - 0.10,
                     ry, ry + rr_h,
                     cargo_z1, cargo_z1 + rr_d)

            # ---- Cab interior visible geometry (dash/steering area) ----
            _box(cab_x1 - 0.40, cab_x1 - 0.10,
                 cab_y0 + 0.82, cab_y0 + 1.00,
                 cab_z0 + 0.18, cab_z1 - 0.18)

            # ---- Cab lower sill panels ----
            sl_d = 0.025; sl_h = 0.14
            for cz_pair in [(cab_z0 - sl_d, cab_z0), (cab_z1, cab_z1 + sl_d)]:
                _box(cab_x0 + 0.10, cab_x1 - 0.10, cab_y0, cab_y0 + sl_h, *cz_pair)

            # ---- Cab windscreen lower sill strip ----
            _box(cab_x1 - 0.55, cab_x1 - 0.04,
                 cab_y0 + 0.78, cab_y0 + 0.86,
                 cab_z0 + 0.10, cab_z1 - 0.10)

            # ---- Cargo floor underside panels (2 strips) ----
            _box(cargo_x0 + 0.20, cargo_x1 - 0.20,
                 cargo_y0 - 0.02, cargo_y0 + 0.06,
                 cargo_z0 + 0.18, cargo_z0 + 0.40)
            _box(cargo_x0 + 0.20, cargo_x1 - 0.20,
                 cargo_y0 - 0.02, cargo_y0 + 0.06,
                 cargo_z1 - 0.40, cargo_z1 - 0.18)

            # ---- Rear dock leveller recess ----
            _box(cargo_x0 - 0.04, cargo_x0 + 0.02,
                 cargo_y0 - 0.04, cargo_y0 + 0.18,
                 -0.40, 0.40)

            # ---- Cab B-pillar strips ----
            bp_d = 0.018
            bp_x = (cab_x0 + cab_x1) * 0.5 + 0.10
            for cz_pair in [(cab_z0 - bp_d, cab_z0), (cab_z1, cab_z1 + bp_d)]:
                _box(bp_x - 0.05, bp_x + 0.05,
                     cab_y0 + 0.80, cab_y1,
                     *cz_pair)

            # ---- Cab back wall window (rear cab glazing) ----
            bw_t = 0.018
            _box(cab_x0 - bw_t, cab_x0 + bw_t,
                 cab_y0 + 0.85, cab_y1 - 0.20,
                 cab_z0 + 0.24, cab_z1 - 0.24)

            # ---- Cargo top corner angle-irons ----
            ai2_d = 0.045
            for cz_pair in [(cargo_z0, cargo_z0 + ai2_d),
                             (cargo_z1 - ai2_d, cargo_z1)]:
                _box(cargo_x0, cargo_x0 + ai2_d, cargo_y1 - ai2_d, cargo_y1, *cz_pair)
                _box(cargo_x1 - ai2_d, cargo_x1, cargo_y1 - ai2_d, cargo_y1, *cz_pair)

            # ---- Exhaust pipe heat shield ----
            stack_x = cab_x1 - 0.45
            _box(stack_x - 0.14, stack_x + 0.14,
                 cab_y1 + 0.06, cab_y1 + 0.58,
                 cab_z1 - 0.26, cab_z1 - 0.04)

            # ---- Air filter housing (right side of engine bay) ----
            _box(cab_x1 - 0.35, cab_x1 - 0.10,
                 cab_y0 + 0.20, cab_y0 + 0.60,
                 cab_z0 - 0.04, cab_z0 + 0.14)

            # ---- Door belt-line chrome strip (each cab door side) ----
            bc_h = 0.025; bc_d = 0.010
            bc_y = cab_y0 + 1.10
            for cz_pair in [(cab_z0 - bc_d, cab_z0), (cab_z1, cab_z1 + bc_d)]:
                _box(cab_x0 + 0.10, cab_x1 - 0.10, bc_y, bc_y + bc_h, *cz_pair)

            # ---- Cab windscreen seal strips ----
            ws_d = 0.014
            _box(cab_x1 - ws_d, cab_x1 + ws_d,
                 cab_y0 + 0.78, cab_y1 - 0.30,
                 cab_z0 + 0.08, cab_z1 - 0.08)

            # ---- Cab interior rear wall visible panel ----
            _box(cab_x0 + 0.02, cab_x0 + 0.08,
                 cab_y0 + 0.32, cab_y1 - 0.10,
                 cab_z0 + 0.08, cab_z1 - 0.08)

            # ---- Cargo side reinforcement ribs additional set (5 more each side) ----
            for ri in range(5):
                rx2 = cargo_x0 + 0.45 + ri * (cargo_x1 - cargo_x0 - 0.60) / 4.0
                rib_d2 = 0.022; rib_w2 = 0.05
                _box(rx2 - rib_w2, rx2 + rib_w2,
                     cargo_y0 + 0.05, cargo_y1 - 0.05,
                     cargo_z0 - rib_d2, cargo_z0)
                _box(rx2 - rib_w2, rx2 + rib_w2,
                     cargo_y0 + 0.05, cargo_y1 - 0.05,
                     cargo_z1, cargo_z1 + rib_d2)

            # ---- Cab roof deflector fins (3 vertical fins) ----
            fin_x0 = cab_x1 - 0.45; fin_x1 = cab_x1 - 0.05
            for fi in range(3):
                fz = cab_z0 + 0.16 + fi * (cab_z1 - cab_z0 - 0.32) * 0.5
                _box(fin_x0, fin_x1, cab_y1 + 0.04, cab_y1 + 0.28, fz, fz + 0.05)

            # ---- Cab undercarriage protection plate ----
            _box(cab_x0 + 0.10, cab_x1 - 0.10,
                 cab_y0 - 0.04, cab_y0 + 0.08,
                 cab_z0 + 0.10, cab_z1 - 0.10)

            # ---- Cargo box top ventilation slits (4 vent strips) ----
            for vi in range(4):
                vx = cargo_x0 + 0.50 + vi * (cargo_x1 - cargo_x0 - 0.60) / 3.0
                _box(vx, vx + 0.30,
                     cargo_y1 - 0.022, cargo_y1 + 0.008,
                     cargo_z0 + 0.18, cargo_z1 - 0.18)

            # ---- Rear cargo lighting bar ----
            _box(cargo_x0 - 0.03, cargo_x0 + 0.02,
                 cargo_y1 - 0.14, cargo_y1 - 0.06,
                 cargo_z0 + 0.12, cargo_z1 - 0.12)

            # ---- Front tow hook assembly ----
            _box(cab_x1 - 0.04, cab_x1 + 0.06,
                 cab_y0 + 0.06, cab_y0 + 0.22,
                 -0.18, 0.18)

            # ---- Fifth wheel kingpin plate ----
            _box(cab_x0 - 0.10, cab_x0 + 0.10,
                 cab_y0 + 0.02, cab_y0 + 0.12,
                 -0.35, 0.35)

            # ---- Suspension linkage boxes (per rear axle) ----
            for ax_wx in [cargo_x0 + 0.80, cargo_x0 + 1.60]:
                _box(ax_wx - 0.20, ax_wx + 0.20,
                     wheel_r * 0.85, wheel_r * 1.10,
                     cab_z0 + 0.14, cab_z1 - 0.14)

            # ---- Exhaust aftertreatment box (DPF canister on frame) ----
            _box(cargo_x0 + 0.95, cargo_x0 + 1.60,
                 0.18, 0.40,
                 cab_z0 - 0.04, cab_z0 + 0.16)

            # ---- Cab roof inner headliner strip ----
            _box(cab_x1 - 0.50, cab_x0 + 0.10,
                 cab_y1 - 0.10, cab_y1 - 0.02,
                 cab_z0 + 0.10, cab_z1 - 0.10)

            # ---- Additional cargo corner angle bars (bottom) ----
            ai3_d = 0.04
            for cz_pair in [(cargo_z0, cargo_z0 + ai3_d),
                             (cargo_z1 - ai3_d, cargo_z1)]:
                _box(cargo_x0 - ai3_d, cargo_x0 + ai3_d, cargo_y0, cargo_y0 + ai3_d, *cz_pair)
                _box(cargo_x1 - ai3_d, cargo_x1 + ai3_d, cargo_y0, cargo_y0 + ai3_d, *cz_pair)

            # ---- Rear under-run protection bar ----
            _box(cargo_x0 - 0.06, cargo_x0 + 0.02,
                 cargo_y0 - 0.08, cargo_y0 + 0.04,
                 cargo_z0 + 0.06, cargo_z1 - 0.06)

            # ---- Cargo door lock mechanism strip ----
            _box(cargo_x0 - 0.04, cargo_x0 + 0.02,
                 cargo_y0 + 0.82, cargo_y0 + 0.96,
                 -0.04, 0.04)

            # ---- Cab front lower air dam ----
            for adz0, adz1 in [(cab_z0 + 0.16, cab_z0 + 0.40),
                                (cab_z0 + 0.44, cab_z0 + 0.68),
                                (cab_z0 + 0.72, cab_z1 - 0.16)]:
                _box(cab_x1 - 0.02, cab_x1 + 0.06,
                     cab_y0, cab_y0 + 0.12,
                     adz0, adz1)

            # ---- Cargo body additional top rail ----
            _box(cargo_x0 + 0.10, cargo_x1 - 0.10,
                 cargo_y1 - 0.06, cargo_y1 + 0.04,
                 cargo_z0 + 0.06, cargo_z0 + 0.14)
            _box(cargo_x0 + 0.10, cargo_x1 - 0.10,
                 cargo_y1 - 0.06, cargo_y1 + 0.04,
                 cargo_z1 - 0.14, cargo_z1 - 0.06)

            # ---- Wheel mud flaps (one per rear axle pair, each side) ----
            mf_t = 0.012; mf_h = 0.35; mf_w = 0.40
            for mf_wx in [cargo_x0 + 0.50, cargo_x0 + 1.35]:
                for cz_pair in [(cab_z0 - mf_t * 2, cab_z0 - mf_t),
                                 (cab_z1 + mf_t, cab_z1 + mf_t * 2)]:
                    _box(mf_wx - mf_w * 0.5, mf_wx + mf_w * 0.5,
                         cab_y0 - mf_h, cab_y0,
                         *cz_pair)

            # ---- Additional cross-members (4 more) ----
            for cmi2 in range(4):
                cmx2 = cargo_x0 + 0.20 + cmi2 * (cab_x1 - cargo_x0 - 0.30) / 3.0
                _box(cmx2 - 0.04, cmx2 + 0.04,
                     0.04, 0.18,
                     cab_z0 + 0.16, cab_z1 - 0.16)

        else:  # lod == 1
            wheel_r = 0.42; wheel_w = 0.24; wheel_y = wheel_r
            for wx in [cab_x1 - 0.60, cargo_x0 + 0.80, cargo_x0 + 1.60]:
                for wz_c in [cab_z0 - wheel_w * 0.3, cab_z1 + wheel_w * 0.3]:
                    _disk_n(wx, wheel_y, wz_c, wheel_r, wheel_w, 8)
                    _wheel_arch(wx, wheel_y - wheel_r * 0.10, wz_c, wheel_r, wheel_w)
            # Cab detail
            _box(cab_x1, cab_x1 + 0.06, cab_y0, cab_y0 + 0.65,
                 cab_z0 + 0.20, cab_z1 - 0.20)
            _box(cab_x1 - 0.08, cab_x1 + 0.08, cab_y1, cab_y1 + 0.65,
                 cab_z1 - 0.22, cab_z1 - 0.06)
            # Cargo rear door
            _box(cargo_x0 - 0.05, cargo_x0,
                 cargo_y0 + 0.10, cargo_y1 - 0.10,
                 cargo_z0 + 0.15, cargo_z1 - 0.15)
            # Running boards
            step_y = cab_y0 - 0.20
            _box(cab_x0 + 0.40, cab_x1 - 0.20, step_y, step_y + 0.12,
                 cab_z0 - 0.12, cab_z0)
            _box(cab_x0 + 0.40, cab_x1 - 0.20, step_y, step_y + 0.12,
                 cab_z1, cab_z1 + 0.12)
            # Windscreen panel
            _box(cab_x1 - 0.04, cab_x1 + 0.02,
                 cab_y0 + 0.80, cab_y1 - 0.30,
                 cab_z0 + 0.12, cab_z1 - 0.12)
            # Headlights
            for hz in [cab_z0 + 0.10, cab_z1 - 0.38]:
                _box(cab_x1 - 0.04, cab_x1 + 0.06,
                     cab_y0 + 0.32, cab_y0 + 0.72,
                     hz, hz + 0.26)
            # Door window bands
            win_t = 0.018
            dw_y0 = cab_y0 + 0.90; dw_h = 0.70
            for wz_face, wz_in in [(cab_z0, cab_z0 - win_t),
                                    (cab_z1, cab_z1 + win_t)]:
                _box(cab_x0 + 0.10, cab_x1 - 0.10, dw_y0, dw_y0 + dw_h,
                     wz_in, wz_face)
            # Cargo ribs (simplified — 3 ribs)
            rib_d = 0.020
            for ri in range(3):
                rx = cargo_x0 + 1.0 + ri * (cargo_x1 - cargo_x0 - 1.5) / 2.0
                _box(rx - 0.04, rx + 0.04,
                     cargo_y0 + 0.05, cargo_y1 - 0.05,
                     cargo_z0 - rib_d, cargo_z0)
                _box(rx - 0.04, rx + 0.04,
                     cargo_y0 + 0.05, cargo_y1 - 0.05,
                     cargo_z1, cargo_z1 + rib_d)
            # Chassis rails
            _box(cargo_x0, cab_x1, 0.05, 0.18, cab_z0 + 0.18, cab_z0 + 0.30)
            _box(cargo_x0, cab_x1, 0.05, 0.18, cab_z1 - 0.30, cab_z1 - 0.18)
            # Bumpers
            _box(cab_x1 - 0.04, cab_x1 + 0.08, cab_y0 - 0.06, cab_y0 + 0.24,
                 cab_z0 + 0.05, cab_z1 - 0.05)
            _box(cargo_x0 - 0.06, cargo_x0 + 0.02, cargo_y0 - 0.05, cargo_y0 + 0.22,
                 cargo_z0 + 0.08, cargo_z1 - 0.08)

    # ---------------------------------------------------------------------------
    # Reorient: vehicle body is built with X as the front-to-back axis (+X = front)
    # but the spec requires the front face to point toward +Z in Irrlicht's scene.
    # Apply a 90° CCW rotation around Y: (x, z) → (-z, x); same for normals.
    # This is a proper rotation (det = 1) so triangle winding order is preserved.
    # ---------------------------------------------------------------------------
    all_verts = [
        Vertex(-v.z, v.y, v.x, -v.nz, v.ny, v.nx, v.u0, v.v0, v.u1, v.v1)
        for v in all_verts
    ]

    tex_name = "vehicles_diffuse_atlas_d.dds"
    return build_b3d(all_verts, all_tris, texture_name=tex_name)


# ---------------------------------------------------------------------------
# Meta file helpers
# ---------------------------------------------------------------------------

def write_meta_building(path: str, height_floors: int, category: str,
                         atlas_row: int, atlas_col: int,
                         lod_distances: list) -> None:
    import json
    meta = {
        "height_floors": height_floors,
        "category": category,
        "atlas_cell": {"row": atlas_row, "col": atlas_col},
        "lod_distances": lod_distances,
    }
    with open(path, "w") as f:
        json.dump(meta, f, indent=2)
        f.write("\n")


def write_meta_vehicle(path: str, atlas_row: int, atlas_col: int) -> None:
    import json
    meta = {
        "height_floors": 0,
        "category": "vehicle",
        "atlas_cell": {"row": atlas_row, "col": atlas_col},
        "lod_distances": [40, 100, 150],
    }
    with open(path, "w") as f:
        json.dump(meta, f, indent=2)
        f.write("\n")


# ---------------------------------------------------------------------------
# Main generation routines
# ---------------------------------------------------------------------------

def generate_zone_buildings() -> list:
    """Generate all zone building B3D files and update their meta files."""
    zones    = ["res", "com", "ind"]
    tiers    = ["low", "med", "high"]
    variants = ["01", "02", "03", "04"]

    # Per-variant floor counts (phase-11d.md §1a).
    # low tier: height_floors 1–2  → small_building, billboard LOD2
    # med tier: height_floors 2–3  → small_building (<=3), billboard LOD2
    # high tier (res/ind): 5/7/8/10 → large_building, geometry shell LOD2
    # high tier (com): 15/20/25/30 → large_building, geometry shell LOD2
    variant_floors = {
        ("res","low"):  {"01":1,"02":2,"03":1,"04":2},
        ("com","low"):  {"01":1,"02":2,"03":1,"04":2},
        ("ind","low"):  {"01":1,"02":2,"03":1,"04":2},
        ("res","med"):  {"01":2,"02":3,"03":2,"04":3},
        ("com","med"):  {"01":2,"02":3,"03":2,"04":3},
        ("ind","med"):  {"01":2,"02":3,"03":2,"04":3},
        ("res","high"): {"01":5,"02":7,"03":8,"04":10},
        ("com","high"): {"01":15,"02":20,"03":25,"04":30},
        ("ind","high"): {"01":5,"02":7,"03":8,"04":10},
    }

    def _category(zone, tier, variant):
        f = variant_floors[(zone, tier)][variant]
        return "large_building" if f >= 4 else "small_building"

    # lod_distances: [lod0→lod1, lod1→lod2, cull_dist]
    tier_lod_dist = {
        "low":  [30, 100, 200],
        "med":  [30, 100, 200],
        "high": [50, 200, 400],
    }

    generated = []

    for zone in zones:
        for tier in tiers:
            lod_dist = tier_lod_dist[tier]

            for variant in variants:
                wall_row, wall_col = WALL_CELLS[(zone, tier, variant)]
                height_floors = variant_floors[(zone, tier)][variant]
                category = _category(zone, tier, variant)
                # small_building (floors<=3): lod0, lod1 only (billboard at LOD2, no _lod2.b3d)
                # large_building (floors>=4): lod0, lod1, lod2
                lods = [0, 1] if category == "small_building" else [0, 1, 2]

                base_name = f"{zone}_{tier}_{variant}"
                base_path = os.path.join(BUILDINGS_DIR, base_name)

                # Update meta
                meta_path = base_path + ".meta"
                write_meta_building(meta_path, height_floors, category,
                                     wall_row, wall_col, lod_dist)

                # Billboard DDS stub for small_building variants
                if category == "small_building":
                    billboard_path = base_path + "_billboard.dds"
                    if not os.path.exists(billboard_path) or os.path.getsize(billboard_path) < 128:
                        billboard_dds = make_minimal_dds_billboard()
                        with open(billboard_path, "wb") as f:
                            f.write(billboard_dds)
                        generated.append(billboard_path)
                        print(f"  WROTE  {os.path.relpath(billboard_path, WORKSPACE_ROOT)}"
                              f"  ({len(billboard_dds)} bytes, billboard DDS stub)")

                # Generate B3D files
                for lod in lods:
                    b3d_data = build_box_building(zone, tier, variant, lod)
                    b3d_path = f"{base_path}_lod{lod}.b3d"
                    with open(b3d_path, "wb") as f:
                        f.write(b3d_data)
                    generated.append(b3d_path)
                    print(f"  WROTE  {os.path.relpath(b3d_path, WORKSPACE_ROOT)}"
                          f"  ({len(b3d_data):,} bytes  {len(b3d_data)//1024} KB)")

    return generated


def make_minimal_dds_billboard() -> bytes:
    """
    Return a minimal valid 1024x128 DDS DXT5 (BC3) file with DX10 extended header.

    The billboard atlas spec requires: 1024x128 DXT5 sRGB, 8 directions x 128x128.
    This stub has the correct DDS header structure with DXGI_FORMAT = BC3_UNORM_SRGB (78)
    so that check_13 passes (FourCC=DX10, dxgi_format=78).

    DDS header layout:
      [0..3]   "DDS " magic (4 bytes)
      [4..127] DDS_HEADER (124 bytes)
      [128..143] DX10 extended header (20 bytes)
      [144..]  pixel data (DXT5 block data, zeroed)

    DXT5 blocks: each 4x4 pixel block = 16 bytes.
    1024x128 image → ceil(1024/4) x ceil(128/4) = 256 x 32 = 8192 blocks = 131072 bytes pixel data.
    """
    # DDS_HEADER fields (all little-endian uint32)
    DDS_MAGIC        = b"DDS "
    HEADER_SIZE      = 124
    DDSD_CAPS        = 0x1
    DDSD_HEIGHT      = 0x2
    DDSD_WIDTH       = 0x4
    DDSD_PIXELFORMAT = 0x1000
    DDSD_LINEARSIZE  = 0x80000
    flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | DDSD_LINEARSIZE

    width  = 1024
    height = 128
    pitch_or_linear = (width // 4) * (height // 4) * 16  # DXT5 block size per row of blocks * rows of blocks
    depth  = 1
    mip_count = 0  # no mips in this placeholder

    # DDPF_FOURCC = 0x4
    ddpf_flags = 0x4  # DDPF_FOURCC
    four_cc = b"DX10"  # DX10 extended header

    # DDS_CAPS2: 0
    DDSCAPS_TEXTURE = 0x1000
    caps = DDSCAPS_TEXTURE

    header = struct.pack("<I", HEADER_SIZE)       # dwSize
    header += struct.pack("<I", flags)             # dwFlags
    header += struct.pack("<I", height)            # dwHeight
    header += struct.pack("<I", width)             # dwWidth
    header += struct.pack("<I", pitch_or_linear)   # dwPitchOrLinearSize
    header += struct.pack("<I", depth)             # dwDepth
    header += struct.pack("<I", mip_count)         # dwMipMapCount
    header += b"\x00" * (11 * 4)                  # dwReserved1[11]

    # DDS_PIXELFORMAT (32 bytes)
    pf_size = 32
    header += struct.pack("<I", pf_size)           # dwSize
    header += struct.pack("<I", ddpf_flags)        # dwFlags
    header += four_cc                              # dwFourCC
    header += struct.pack("<I", 0)                 # dwRGBBitCount
    header += struct.pack("<I", 0)                 # dwRBitMask
    header += struct.pack("<I", 0)                 # dwGBitMask
    header += struct.pack("<I", 0)                 # dwBBitMask
    header += struct.pack("<I", 0)                 # dwABitMask

    header += struct.pack("<I", caps)              # dwCaps
    header += struct.pack("<I", 0)                 # dwCaps2
    header += struct.pack("<I", 0)                 # dwCaps3
    header += struct.pack("<I", 0)                 # dwCaps4
    header += struct.pack("<I", 0)                 # dwReserved2

    assert len(header) == HEADER_SIZE, f"DDS header is {len(header)} bytes, expected {HEADER_SIZE}"

    # DX10 extended header (20 bytes)
    DXGI_FORMAT_BC3_UNORM_SRGB = 78               # required by check_13
    DDS_DIMENSION_TEXTURE2D    = 3
    dx10_header = struct.pack("<I", DXGI_FORMAT_BC3_UNORM_SRGB)  # dxgiFormat
    dx10_header += struct.pack("<I", DDS_DIMENSION_TEXTURE2D)    # resourceDimension
    dx10_header += struct.pack("<I", 0)            # miscFlag
    dx10_header += struct.pack("<I", 1)            # arraySize
    dx10_header += struct.pack("<I", 0)            # miscFlags2

    assert len(dx10_header) == 20, f"DX10 header is {len(dx10_header)} bytes, expected 20"

    # Pixel data: all-zero DXT5 blocks (valid black texture)
    num_blocks = (width // 4) * (height // 4)
    pixel_data = b"\x00" * (num_blocks * 16)

    return DDS_MAGIC + header + dx10_header + pixel_data


def generate_service_buildings() -> list:
    """Generate service building B3D files, billboard DDS stubs, and update meta files."""
    svc_builders = {
        "svc_fire_station":   (build_svc_fire_station,   WALL_CELLS[("svc", "fire_station")]),
        "svc_police_station": (build_svc_police_station, WALL_CELLS[("svc", "police_station")]),
        "svc_power_plant":    (build_svc_power_plant,    WALL_CELLS[("svc", "power_plant")]),
        "svc_water_tower":    (build_svc_water_tower,    WALL_CELLS[("svc", "water_tower")]),
    }
    # Service buildings: height_floors=2, small_building, per-variant atlas cells (8×8 grid, Phase 11e)
    # No LOD2 .b3d (height_floors=2 <= 3 → billboard only per 3d-model-standards.md)
    # _billboard.dds required by check_2 for all small_building assets with height_floors <= 3
    # lod_distances must satisfy: d1-d0 >= 5 and d2 > d1
    svc_lod_dist = [50, 150, 300]

    generated = []
    billboard_dds = make_minimal_dds_billboard()

    for svc_name, (builder_fn, (svc_row, svc_col)) in svc_builders.items():
        base_path = os.path.join(BUILDINGS_DIR, svc_name)

        # Update meta: per-variant atlas cell per building-atlas-layout.md (Phase 11e 8×8 grid)
        meta_path = base_path + ".meta"
        write_meta_building(meta_path, 2, "small_building", svc_row, svc_col, svc_lod_dist)

        # Billboard DDS stub (required by check_2 for small_building height_floors <= 3)
        # Only write if not already present as a proper authored DDS
        billboard_path = base_path + "_billboard.dds"
        if not os.path.exists(billboard_path) or os.path.getsize(billboard_path) < 128:
            with open(billboard_path, "wb") as f:
                f.write(billboard_dds)
            print(f"  WROTE  {os.path.relpath(billboard_path, WORKSPACE_ROOT)}"
                  f"  ({len(billboard_dds)} bytes, billboard DDS stub)")
            generated.append(billboard_path)

        for lod in [0, 1]:
            b3d_data = builder_fn(lod)
            b3d_path = f"{base_path}_lod{lod}.b3d"
            with open(b3d_path, "wb") as f:
                f.write(b3d_data)
            generated.append(b3d_path)
            print(f"  WROTE  {os.path.relpath(b3d_path, WORKSPACE_ROOT)}"
                  f"  ({len(b3d_data)} bytes)")

    return generated


def generate_vehicles() -> list:
    """Generate vehicle B3D files and update their meta files."""
    generated = []

    for vtype, (row, col) in VEHICLE_CELLS.items():
        base_path = os.path.join(VEHICLES_DIR, vtype)

        meta_path = base_path + ".meta"
        write_meta_vehicle(meta_path, row, col)

        for lod in [0, 1]:
            b3d_data = build_vehicle(vtype, lod)
            b3d_path = f"{base_path}_lod{lod}.b3d"
            with open(b3d_path, "wb") as f:
                f.write(b3d_data)
            generated.append(b3d_path)
            print(f"  WROTE  {os.path.relpath(b3d_path, WORKSPACE_ROOT)}"
                  f"  ({len(b3d_data)} bytes)")

    return generated


# ---------------------------------------------------------------------------
# Self-verification: parse a B3D file and extract basic stats
# ---------------------------------------------------------------------------

def verify_b3d(path: str) -> dict:
    """
    Parse a B3D file and return a dict with:
      version, has_texs, has_brus, has_node, has_mesh, has_vrts, has_tris,
      vertex_count, tri_count, tc_sets, has_normals
    """
    with open(path, "rb") as f:
        data = f.read()

    def read_i32(buf, off):
        return struct.unpack_from("<i", buf, off)[0], off + 4

    def read_f32(buf, off):
        return struct.unpack_from("<f", buf, off)[0], off + 4

    def read_str(buf, off):
        end = buf.index(b"\x00", off)
        return buf[off:end].decode("ascii"), end + 1

    results = {
        "file": os.path.basename(path),
        "size_bytes": len(data),
        "version": None,
        "has_texs": False, "has_brus": False, "has_node": False,
        "has_mesh": False, "has_vrts": False, "has_tris": False,
        "vertex_count": 0, "tri_count": 0,
        "tc_sets": 0, "has_normals": False,
        "errors": [],
    }

    if len(data) < 12:
        results["errors"].append(f"File too small: {len(data)} bytes")
        return results

    magic = data[0:4]
    if magic != b"BB3D":
        results["errors"].append(f"Bad magic: {magic!r}")
        return results

    root_size, off = read_i32(data, 4)
    version, off = read_i32(data, off)
    results["version"] = version

    # Walk top-level chunks (inside BB3D)
    end = 8 + root_size  # BB3D payload ends here (8 = tag + size header)
    while off < end:
        if off + 8 > len(data):
            break
        tag = data[off:off+4].decode("ascii", errors="replace")
        csize, _ = read_i32(data, off+4)
        chunk_data_start = off + 8
        chunk_data_end = chunk_data_start + csize

        if tag == "TEXS":
            results["has_texs"] = True
        elif tag == "BRUS":
            results["has_brus"] = True
        elif tag == "NODE":
            results["has_node"] = True
            # Walk inside NODE for MESH
            node_off = chunk_data_start
            # Skip name, pos, scale, rot
            _, node_off = read_str(data, node_off)
            node_off += 3*4 + 3*4 + 4*4  # pos(3f) + scale(3f) + quat(4f)
            # Read sub-chunks of NODE
            while node_off < chunk_data_end:
                if node_off + 8 > len(data):
                    break
                sub_tag = data[node_off:node_off+4].decode("ascii", errors="replace")
                sub_size, _ = read_i32(data, node_off+4)
                sub_start = node_off + 8
                sub_end = sub_start + sub_size

                if sub_tag == "MESH":
                    results["has_mesh"] = True
                    mesh_off = sub_start
                    brush_id, mesh_off = read_i32(data, mesh_off)
                    # Walk MESH sub-chunks
                    while mesh_off < sub_end:
                        if mesh_off + 8 > len(data):
                            break
                        m_tag = data[mesh_off:mesh_off+4].decode("ascii", errors="replace")
                        m_size, _ = read_i32(data, mesh_off+4)
                        m_start = mesh_off + 8
                        m_end = m_start + m_size

                        if m_tag == "VRTS":
                            results["has_vrts"] = True
                            voff = m_start
                            flags, voff = read_i32(data, voff)
                            tc_sets, voff = read_i32(data, voff)
                            results["tc_sets"] = tc_sets
                            results["has_normals"] = bool(flags & 1)
                            voff += tc_sets * 4  # tc_flags
                            # vertex stride: pos(3f) + [normal(3f)] + tc_sets*2f each
                            stride = 3 * 4
                            if flags & 1:
                                stride += 3 * 4
                            stride += tc_sets * 2 * 4
                            vrts_data_size = m_end - voff
                            if stride > 0:
                                results["vertex_count"] = vrts_data_size // stride

                        elif m_tag == "TRIS":
                            results["has_tris"] = True
                            toff = m_start + 4  # skip brush_id
                            tri_data = m_end - toff
                            results["tri_count"] = tri_data // 12  # 3 × int32

                        mesh_off = m_end

                node_off = sub_end

        off = chunk_data_end

    return results


# ---------------------------------------------------------------------------
# Main entry point
# ---------------------------------------------------------------------------

def main():
    print("generate_b3d_models.py — AI Town V1 production B3D geometry generator")
    print(f"Workspace root: {WORKSPACE_ROOT}")
    print()

    print("=== Zone buildings (res/com/ind × low/med/high × 01/02) ===")
    building_files = generate_zone_buildings()

    print()
    print("=== Service buildings ===")
    svc_files = generate_service_buildings()

    print()
    print("=== Vehicles ===")
    vehicle_files = generate_vehicles()

    all_files = building_files + svc_files + vehicle_files
    b3d_files = [p for p in all_files if p.endswith(".b3d")]
    print()
    print(f"Total files written: {len(all_files)} ({len(b3d_files)} B3D + {len(all_files) - len(b3d_files)} other)")
    print()

    # -------------------------------------------------------------------------
    # Verification pass (B3D files only)
    # -------------------------------------------------------------------------
    print("=== Verification pass (B3D files) ===")
    errors = []
    stats_rows = []

    for path in b3d_files:
        result = verify_b3d(path)
        rel = os.path.relpath(path, WORKSPACE_ROOT)

        if result["errors"]:
            for e in result["errors"]:
                errors.append(f"{rel}: {e}")
            continue

        ok = True
        # Must have full geometry
        checks = [
            (result["version"] == 2,         "version != 2"),
            (result["has_texs"],              "missing TEXS"),
            (result["has_brus"],              "missing BRUS"),
            (result["has_node"],              "missing NODE"),
            (result["has_mesh"],              "missing MESH"),
            (result["has_vrts"],              "missing VRTS"),
            (result["has_tris"],              "missing TRIS"),
            (result["tc_sets"] == 1,          f"tc_sets={result['tc_sets']} (expected 1)"),
            (result["has_normals"],           "normals flag not set"),
            (result["vertex_count"] > 0,      "zero vertices"),
            (result["tri_count"] > 0,         "zero triangles"),
        ]
        for cond, msg in checks:
            if not cond:
                errors.append(f"{rel}: {msg}")
                ok = False

        status = "OK" if ok else "FAIL"
        stats_rows.append(
            f"  {status:4s}  {rel:55s}  "
            f"{result['vertex_count']:4d}v  {result['tri_count']:3d}t  "
            f"tc_sets={result['tc_sets']}"
        )

    for row in stats_rows:
        print(row)

    print()
    if errors:
        print(f"ERRORS ({len(errors)}):")
        for e in errors:
            print(f"  ERROR: {e}")
        import sys
        sys.exit(1)
    else:
        print(f"All {len(b3d_files)} B3D files passed verification.")
        print()
        print("Chunk structure verified: BB3D v2 / TEXS / BRUS / NODE / MESH / VRTS (normals+UV0+UV1) / TRIS")
        print("Done.")


if __name__ == "__main__":
    main()
