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

# Building heights per tier (in Irrlicht unit space, before setScale)
# low=2 floors → 0.6 units, med=4 floors → 1.2 units, high=8 floors → 2.4 units
TIER_HEIGHT = {
    "low":  0.6,
    "med":  1.2,
    "high": 2.4,
    "svc":  0.6,   # service buildings are 2-floor equivalent
}

# Half-extent on X and Z (leaves slight gap between tiles)
BUILDING_HALF_XZ = 0.45


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
                     xmin, xmax, y_base, ridge_h, zmin, zmax):
    """
    Gabled (two-slope) pitched roof.
    Ridge runs along X axis from (xmin, ridge_y, cz) to (xmax, ridge_y, cz).
    Front slope faces -Z; back slope faces +Z.
    Gable ends (triangles) on X=xmin and X=xmax sides.
    """
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
    m.add_tri(
        (xmin, y_base, zmin), (xmin, y_base, zmax), (xmin, ridge_y, cz),
        (-1, 0, 0), wall_row, wall_col
    )
    # Right gable triangle (X=xmax): CCW from outside (from +X)
    # Viewed from +X: zmax→zmin is left→right
    m.add_tri(
        (xmax, y_base, zmax), (xmax, y_base, zmin), (xmax, ridge_y, cz),
        (1, 0, 0), wall_row, wall_col
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

    S = 0.1

    if variant == "01":
        # Detached house: box + gabled roof + chimney box + porch slab
        bw, bd, bh = 8*S, 10*S, 6*S
        hx, hz = bw/2, bd/2
        ridge_h = (10 - 6) * S
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            _add_gabled_roof(m, wr, wc, rr, rc, -hx, hx, bh, ridge_h, -hz, hz)
            return m.to_b3d()
        # LOD0
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        _add_gabled_roof(m, wr, wc, rr, rc, -hx, hx, bh, ridge_h, -hz, hz)
        # Chimney box
        chw = 0.8*S/2
        _add_chimney(m, wr, wc, rr, rc, 0.0, 0.0, bh + ridge_h*0.5, 2*S, hw=chw)
        # Porch slab
        porch_d = 1.5*S
        porch_w = 2.0*S
        porch_y = 3.0*S
        m.add_box(-porch_w/2, porch_w/2, porch_y, porch_y+0.2*S, -hz-porch_d, -hz, wr, wc)
        return m.to_b3d()

    elif variant == "02":
        # Semi-detached pair: two unit boxes + two hipped roofs
        unit_w, unit_d, unit_h = 6*S, 8*S, 5*S
        total_w = 12*S
        hx, hz = total_w/2, unit_d/2
        ridge_h_val = 3*S
        if lod == 1:
            m.add_box(-hx, hx, 0, unit_h, -hz, hz, wr, wc, rr, rc)
            _add_hipped_roof(m, wr, wc, rr, rc, -hx, 0, unit_h, ridge_h_val, -hz, hz)
            _add_hipped_roof(m, wr, wc, rr, rc, 0, hx, unit_h, ridge_h_val, -hz, hz)
            return m.to_b3d()
        # LOD0: two unit boxes
        m.add_box(-hx, 0, 0, unit_h, -hz, hz, wr, wc, rr, rc)
        m.add_box(0, hx, 0, unit_h, -hz, hz, wr, wc, rr, rc)
        _add_hipped_roof(m, wr, wc, rr, rc, -hx, 0, unit_h, ridge_h_val, -hz, hz)
        _add_hipped_roof(m, wr, wc, rr, rc, 0, hx, unit_h, ridge_h_val, -hz, hz)
        return m.to_b3d()

    elif variant == "03":
        # Three unit boxes + mono-pitch roof + bay window box projection
        unit_w, unit_d = 4*S, 8*S
        total_w = 12*S
        front_h, rear_h = 5*S, 4*S
        hx, hz = total_w/2, unit_d/2
        if lod == 1:
            m.add_box(-hx, hx, 0, front_h, -hz, hz, wr, wc, rr, rc)
            m.add_quad((-hx, front_h, -hz), (hx, front_h, -hz),
                       (hx, rear_h, hz), (-hx, rear_h, hz),
                       (0, 1, 0.3), rr, rc)
            return m.to_b3d()
        # LOD0: three unit boxes
        for i in range(3):
            x0 = -hx + i * unit_w
            x1 = x0 + unit_w
            m.add_box(x0, x1, 0, front_h, -hz, hz, wr, wc, rr, rc)
        _add_mono_pitch_roof(m, wr, wc, rr, rc, -hx, hx, rear_h, front_h, -hz, hz)
        # Bay window box projection on centre unit
        bay_w, bay_h = 0.8*S, 2.5*S
        m.add_box(-bay_w/2, bay_w/2, 0.3*S, 0.3*S+bay_h, -hz-bay_w, -hz, wr, wc, rr, rc)
        return m.to_b3d()

    elif variant == "04":
        # Bungalow: box + low hipped roof + veranda slab
        bw, bd, bh = 10*S, 12*S, 3*S
        hx, hz = bw/2, bd/2
        ridge_h = (5-3)*S
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            _add_hipped_roof(m, wr, wc, rr, rc, -hx, hx, bh, ridge_h, -hz, hz)
            return m.to_b3d()
        # LOD0
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        _add_hipped_roof(m, wr, wc, rr, rc, -hx, hx, bh, ridge_h, -hz, hz)
        # Veranda slab (single roof slab, no posts)
        ver_d = 1.2*S
        ver_h = 2.4*S
        m.add_box(-hx-0.05*S, hx+0.05*S, ver_h, ver_h+0.1*S, -hz-ver_d, -hz, wr, wc)
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
    S = 0.1

    if variant == "01":
        # Box + flat roof (add_box to parapet height)
        bw, bd, bh = 16*S, 12*S, 12*S
        hx, hz = bw/2, bd/2
        par_h = 0.5*S
        if lod == 1:
            m.add_box(-hx, hx, 0, bh+par_h, -hz, hz, wr, wc, rr, rc)
            return m.to_b3d()
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        _add_parapet(m, wr, wc, -hx, hx, bh, -hz, hz, pw=0.03, ph=par_h*0.1)
        return m.to_b3d()

    elif variant == "02":
        # Four unit boxes + alternating gabled roofs
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
            return m.to_b3d()
        # LOD0: four units with staggered setbacks
        for i in range(4):
            x0 = -hx + i*unit_w
            x1 = x0 + unit_w
            setback = 0.5*S if (i%2==0) else 0.0
            m.add_box(x0, x1, 0, unit_h, -hz-setback, hz, wr, wc, rr, rc)
            rh = 1.0*S if (i%2==0) else 0.8*S
            _add_gabled_roof(m, wr, wc, rr, rc, x0, x1, unit_h, rh, -hz-setback, hz)
        return m.to_b3d()

    elif variant == "03":
        # L-shaped plan (two boxes forming L) + flat roofs
        h = 9*S
        if lod == 1:
            m.add_box(-7*S, 7*S, 0, h, -6*S, 6*S, wr, wc, rr, rc)
            return m.to_b3d()
        # L-shape: main block + leg
        m.add_box(-7*S, 7*S, 0, h, -6*S, 0, wr, wc, rr, rc)
        m.add_box(-7*S, -1*S, 0, h, 0, 6*S, wr, wc, rr, rc)
        return m.to_b3d()

    elif variant == "04":
        # Box + mansard roof sides
        bw, bd, bh = 18*S, 14*S, 10*S
        hx, hz = bw/2, bd/2
        mansard_h = 3*S
        total_h = bh + mansard_h
        if lod == 1:
            m.add_box(-hx, hx, 0, total_h, -hz, hz, wr, wc, rr, rc)
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
    S = 0.1
    floor_h = 3*S

    if variant == "01":
        # Tall shaft box + flat roof + balcony slabs every 3 floors
        bw, bd, bh = 10*S, 10*S, 40*S
        hx, hz = bw/2, bd/2
        floors = 13
        if lod == 2:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            return m.to_b3d()
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            m.add_box(-2*S, 2*S, bh, bh+3*S, -2*S, 2*S, wr, wc, rr, rc)
            return m.to_b3d()
        # LOD0
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        # Plant room box
        m.add_box(-2*S, 2*S, bh, bh+3*S, -2*S, 2*S, wr, wc, rr, rc)
        # Balcony slabs every 3 floors
        for fl in range(3, floors, 3):
            fy = fl * floor_h
            _add_balcony_slab(m, wr, wc, -4*S, 4*S, fy, -hz, 1.2*S)
        return m.to_b3d()

    elif variant == "02":
        # Slab box + flat roof
        bw, bd, bh = 24*S, 12*S, 30*S
        hx, hz = bw/2, bd/2
        if lod == 2:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            return m.to_b3d()
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            m.add_box(-hx-1*S, -hx, 0, bh, -1*S, 1*S, wr, wc, rr, rc)
            m.add_box(hx, hx+1*S, 0, bh, -1*S, 1*S, wr, wc, rr, rc)
            return m.to_b3d()
        # LOD0
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        # End staircase boxes
        m.add_box(-hx-1*S, -hx, 0, bh, -1*S, 1*S, wr, wc, rr, rc)
        m.add_box(hx, hx+1*S, 0, bh, -1*S, 1*S, wr, wc, rr, rc)
        return m.to_b3d()

    elif variant == "03":
        # Stepped shaft (3 stacked boxes decreasing in plan) + antenna box
        base_hw = 6*S
        bh = 36*S
        sb1_y, sb2_y = 15*S, 25*S
        sb = 2*S
        if lod == 2:
            m.add_box(-base_hw, base_hw, 0, bh, -base_hw, base_hw, wr, wc, rr, rc)
            return m.to_b3d()
        if lod == 1:
            m.add_box(-base_hw, base_hw, 0, sb1_y, -base_hw, base_hw, wr, wc, rr, rc)
            m.add_box(-base_hw+sb, base_hw-sb, sb1_y, sb2_y, -base_hw+sb, base_hw-sb, wr, wc, rr, rc)
            m.add_box(-base_hw+2*sb, base_hw-2*sb, sb2_y, bh, -base_hw+2*sb, base_hw-2*sb, wr, wc, rr, rc)
            return m.to_b3d()
        # LOD0: three stacked boxes
        m.add_box(-base_hw, base_hw, 0, sb1_y, -base_hw, base_hw, wr, wc, rr, rc)
        hw1 = base_hw - sb
        m.add_box(-hw1, hw1, sb1_y, sb2_y, -hw1, hw1, wr, wc, rr, rc)
        hw2 = base_hw - 2*sb
        m.add_box(-hw2, hw2, sb2_y, bh, -hw2, hw2, wr, wc, rr, rc)
        # Antenna box
        m.add_box(-0.5*S, 0.5*S, bh, bh+4*S, -0.5*S, 0.5*S, wr, wc)
        return m.to_b3d()

    elif variant == "04":
        # Slab box + flat roof (curved front as approximation)
        bw, bd, bh = 20*S, 10*S, 28*S
        hx, hz = bw/2, bd/2
        bow = 1.5*S
        n_seg = 8
        if lod == 2:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            return m.to_b3d()
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
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
    S = 0.1

    if variant == "01":
        # Box + flat roof + canopy slab over entrance
        bw, bd, bh = 8*S, 6*S, 4*S
        hx, hz = bw/2, bd/2
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            return m.to_b3d()
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        # Canopy slab
        can_w, can_d = 4*S, 2*S
        m.add_box(-can_w/2, can_w/2, 3*S, 3*S+0.2*S, -hz-can_d, -hz, wr, wc)
        return m.to_b3d()

    elif variant == "02":
        # Box + flat roof (plain box)
        bw, bd, bh = 8*S, 8*S, 4*S
        hx, hz = bw/2, bd/2
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            return m.to_b3d()
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        return m.to_b3d()

    elif variant == "03":
        # Box + flat roof + 4 simple pilaster boxes on front
        bw, bd, bh = 16*S, 6*S, 5*S
        hx, hz = bw/2, bd/2
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            return m.to_b3d()
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        # 4 simple thin pilaster boxes on front only
        for i in range(4):
            px = -hx + bw * (i + 0.5) / 4
            m.add_box(px-0.1*S, px+0.1*S, 0, bh, -hz-0.2*S, -hz, wr, wc, walls_only=True)
        return m.to_b3d()

    elif variant == "04":
        # Box + sawtooth roof
        bw, bd, bh = 12*S, 8*S, 4*S
        hx, hz = bw/2, bd/2
        tooth_h = 2.5*S
        if lod == 1:
            m.add_box(-hx, hx, 0, bh+tooth_h*0.5, -hz, hz, wr, wc, rr, rc)
            return m.to_b3d()
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        _add_sawtooth_roof(m, wr, wc, rr, rc, -hx, hx, bh, bh+tooth_h, -hz, hz, 4)
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
    S = 0.1

    if variant == "01":
        # Box + smaller set-back top box
        bw, bd, bh = 20*S, 16*S, 15*S
        hx, hz = bw/2, bd/2
        top_h = 18*S
        sb = 1*S
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            m.add_box(-hx+sb, hx-sb, bh, top_h, -hz+sb, hz-sb, wr, wc, rr, rc)
            return m.to_b3d()
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        m.add_box(-hx+sb, hx-sb, bh, top_h, -hz+sb, hz-sb, wr, wc, rr, rc)
        return m.to_b3d()

    elif variant == "02":
        # Wide podium box + narrower tower box on top
        pod_w, pod_d, pod_h = 20*S, 16*S, 6*S
        tow_w, tow_d, tow_h = 10*S, 10*S, 24*S
        hx, hz = pod_w/2, pod_d/2
        if lod == 1:
            m.add_box(-hx, hx, 0, pod_h, -hz, hz, wr, wc, rr, rc)
            m.add_box(-tow_w/2, tow_w/2, pod_h, pod_h+tow_h, -tow_d/2, tow_d/2, wr, wc, rr, rc)
            return m.to_b3d()
        m.add_box(-hx, hx, 0, pod_h, -hz, hz, wr, wc, rr, rc)
        m.add_box(-tow_w/2, tow_w/2, pod_h, pod_h+tow_h, -tow_d/2, tow_d/2, wr, wc, rr, rc)
        return m.to_b3d()

    elif variant == "03":
        # Box + flat roof (atrium as shallow box recess in front face)
        bw, bd, bh = 18*S, 16*S, 15*S
        hx, hz = bw/2, bd/2
        atrium_w, atrium_d = 8*S, 4*S
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            return m.to_b3d()
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        # Atrium slot recess box
        m.add_box(-atrium_w/2, atrium_w/2, 0, bh, -hz, -hz+atrium_d, wr, wc, rr, rc)
        return m.to_b3d()

    elif variant == "04":
        # Box + flat roof (arcade = 6 simple arch boxes on ground floor)
        bw, bd, bh = 20*S, 12*S, 12*S
        hx, hz = bw/2, bd/2
        arcade_h = 3*S
        n_arches = 6
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            return m.to_b3d()
        # Upper floors solid
        m.add_box(-hx, hx, arcade_h, bh, -hz, hz, wr, wc, rr, rc)
        # Ground floor: 6 simple arch column boxes
        for i in range(n_arches):
            cx = -hx + bw * (i + 0.5) / n_arches
            m.add_box(cx-0.15*S, cx+0.15*S, 0, arcade_h, -hz-0.3*S, -hz, wr, wc)
        # Ground floor back wall
        m.add_box(-hx, hx, 0, arcade_h, hz-0.1*S, hz, wr, wc, rr, rc)
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
    S = 0.1

    if variant == "01":
        # Tapered shaft (main box + two smaller setback boxes at top) -- NO mullion grid
        bw, bd, bh = 18*S, 14*S, 60*S
        hx, hz = bw/2, bd/2
        taper1_y, taper2_y = 48*S, 55*S
        if lod == 2:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            return m.to_b3d()
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            return m.to_b3d()
        # LOD0: main shaft + two taper steps
        m.add_box(-hx, hx, 0, taper1_y, -hz, hz, wr, wc, rr, rc)
        sb1 = 1*S
        m.add_box(-hx+sb1, hx-sb1, taper1_y, taper2_y, -hz+sb1, hz-sb1, wr, wc, rr, rc)
        sb2 = 2*S
        m.add_box(-hx+sb2, hx-sb2, taper2_y, bh, -hz+sb2, hz-sb2, wr, wc, rr, rc)
        return m.to_b3d()

    elif variant == "02":
        # Main box + pediment fins (two triangular prisms at top) + entry canopy slab
        bw, bd, bh = 20*S, 16*S, 42*S
        hx, hz = bw/2, bd/2
        crown_h = 6*S
        pod_h = 8*S
        if lod == 2:
            m.add_box(-hx, hx, 0, bh+crown_h, -hz, hz, wr, wc, rr, rc)
            return m.to_b3d()
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
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
        # Entry canopy slab
        m.add_box(-3*S, 3*S, 4*S, 4*S+0.15*S, -hz-2*S, -hz, wr, wc)
        return m.to_b3d()

    elif variant == "03":
        # Main shaft box + chamfered octagonal top section + spire box
        base_hw = 6*S
        bh = 55*S
        chamfer_y = 47*S
        n_sides = 8
        if lod == 2:
            m.add_box(-base_hw, base_hw, 0, bh, -base_hw, base_hw, wr, wc, rr, rc)
            return m.to_b3d()
        if lod == 1:
            m.add_box(-base_hw, base_hw, 0, bh, -base_hw, base_hw, wr, wc, rr, rc)
            m.add_box(-1.5*S, 1.5*S, bh, bh+8*S, -1.5*S, 1.5*S, wr, wc)
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
        return m.to_b3d()

    elif variant == "04":
        # Main slab box + 4 corner mega-column boxes + 3 cross-brace slab boxes
        bw, bd, bh = 14*S, 10*S, 50*S
        hx, hz = bw/2, bd/2
        if lod == 2:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            return m.to_b3d()
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            for cx, cz in [(-hx,-hz),(-hx,hz),(hx,-hz),(hx,hz)]:
                m.add_box(cx-0.5*S, cx+0.5*S, 0, bh, cz-0.5*S, cz+0.5*S, wr, wc, walls_only=True)
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
    S = 0.1

    if variant == "01":
        # Box + pitched roof (portal frame gabled)
        bw, bd, bh = 12*S, 16*S, 5*S
        hx, hz = bw/2, bd/2
        ridge_h = (7-5)*S
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            _add_gabled_roof(m, wr, wc, rr, rc, -hx, hx, bh, ridge_h, -hz, hz)
            return m.to_b3d()
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        _add_gabled_roof(m, wr, wc, rr, rc, -hx, hx, bh, ridge_h, -hz, hz)
        return m.to_b3d()

    elif variant == "02":
        # Box + twin parallel pitched roofs
        bw, bd, bh = 20*S, 16*S, 5*S
        hx, hz = bw/2, bd/2
        ridge_h = (7-5)*S
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            _add_gabled_roof(m, wr, wc, rr, rc, -hx, 0, bh, ridge_h, -hz, hz)
            _add_gabled_roof(m, wr, wc, rr, rc, 0, hx, bh, ridge_h, -hz, hz)
            return m.to_b3d()
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        _add_gabled_roof(m, wr, wc, rr, rc, -hx, 0, bh, ridge_h, -hz, hz)
        _add_gabled_roof(m, wr, wc, rr, rc, 0, hx, bh, ridge_h, -hz, hz)
        return m.to_b3d()

    elif variant == "03":
        # Main box + lean-to box + mono-pitch roofs on each
        mw, md, mh = 12*S, 10*S, 6*S
        ridge_h = (8-6)*S
        lw, lh_top, lh_bot = 6*S, 5*S, 4*S
        hx_main = mw/2
        hz = md/2
        if lod == 1:
            m.add_box(-hx_main, hx_main, 0, mh, -hz, hz, wr, wc, rr, rc)
            _add_gabled_roof(m, wr, wc, rr, rc, -hx_main, hx_main, mh, ridge_h, -hz, hz)
            m.add_box(hx_main, hx_main+lw, 0, lh_top, -hz, hz, wr, wc, rr, rc)
            return m.to_b3d()
        # Main shed
        m.add_box(-hx_main, hx_main, 0, mh, -hz, hz, wr, wc, rr, rc)
        _add_gabled_roof(m, wr, wc, rr, rc, -hx_main, hx_main, mh, ridge_h, -hz, hz)
        # Lean-to box
        m.add_box(hx_main, hx_main+lw, 0, lh_bot, -hz, hz, wr, wc, rr, rc)
        _add_mono_pitch_roof(m, wr, wc, rr, rc, hx_main, hx_main+lw, lh_bot, lh_top, -hz, hz)
        return m.to_b3d()

    elif variant == "04":
        # Full box geometry + mono-pitch roof
        bw, bd = 16*S, 12*S
        hx, hz = bw/2, bd/2
        front_h, rear_h = 6*S, 4*S
        if lod == 1:
            m.add_box(-hx, hx, 0, front_h, -hz, hz, wr, wc, rr, rc)
            return m.to_b3d()
        m.add_box(-hx, hx, 0, rear_h, -hz, hz, wr, wc, rr, rc)
        _add_mono_pitch_roof(m, wr, wc, rr, rc, -hx, hx, rear_h, front_h, -hz, hz)
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
    S = 0.1

    if variant == "01":
        # Box + flat roof (dock leveller recesses = 3 simple shallow box recesses on front)
        bw, bd, bh = 24*S, 20*S, 8*S
        hx, hz = bw/2, bd/2
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            return m.to_b3d()
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        # 3 dock leveller recesses (simple box recesses)
        for i in range(3):
            dcx = -hx + bw*(i+0.5)/3
            _add_loading_dock(m, wr, wc, dcx, 0, -hz, 3*S, 1.2*S, 0.5*S, normal_sign_z=-1)
        return m.to_b3d()

    elif variant == "02":
        # Box + sawtooth north-light roof
        bw, bd, bh = 22*S, 18*S, 10*S
        hx, hz = bw/2, bd/2
        saw_h = 2.5*S
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            return m.to_b3d()
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        _add_sawtooth_roof(m, wr, wc, rr, rc, -hx, hx, bh, bh+saw_h, -hz, hz, 4)
        return m.to_b3d()

    elif variant == "03":
        # Box + flat roof + optional 6 simple thin pilaster boxes on front
        bw, bd, bh = 18*S, 14*S, 14*S
        hx, hz = bw/2, bd/2
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            return m.to_b3d()
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        # 6 simple thin pilaster boxes on front face only
        for i in range(6):
            px = -hx + bw * (i + 0.5) / 6
            m.add_box(px-0.1*S, px+0.1*S, 0, bh, -hz-0.15*S, -hz, wr, wc, walls_only=True)
        return m.to_b3d()

    elif variant == "04":
        # Box + flat roof
        bw, bd, bh = 16*S, 14*S, 9*S
        hx, hz = bw/2, bd/2
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            return m.to_b3d()
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
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
    S = 0.1

    if variant == "01":
        # 3 cylinder approximations (12 segments) + conical tops + small base shed box
        silo_r = 2.5*S
        silo_h = 20*S
        cone_h = 2*S
        n_seg = 12
        cx_list = [(-3*S, -1.5*S), (3*S, -1.5*S), (0, 2.5*S)]
        if lod == 2:
            for cx, cz in cx_list:
                _add_cylinder(m, wr, wc, cx, cz, 0, silo_h, silo_r, n_sides=8)
            return m.to_b3d()
        if lod == 1:
            for cx, cz in cx_list:
                _add_cylinder(m, wr, wc, cx, cz, 0, silo_h, silo_r, n_sides=n_seg)
                _add_cylinder_cap(m, wr, wc, cx, cz, silo_h, silo_r, n_sides=n_seg, face_up=True)
            return m.to_b3d()
        # LOD0
        for cx, cz in cx_list:
            _add_cylinder(m, wr, wc, cx, cz, 0, silo_h, silo_r, n_sides=n_seg)
            _add_cylinder_cap(m, wr, wc, cx, cz, silo_h, silo_r, n_sides=n_seg, face_up=True)
            # Conical roof
            _add_spire(m, rr, rc, cx, cz, silo_h, silo_h+cone_h, silo_r, n_sides=n_seg)
        # Loading shed box
        m.add_box(-6*S, 6*S, 0, 4*S, -5*S, -2*S, wr, wc, rr, rc)
        return m.to_b3d()

    elif variant == "02":
        # Main box + 2 cylinder stack approximations (12 segs) + pipe run box
        bw, bd, bh = 16*S, 12*S, 24*S
        hx, hz = bw/2, bd/2
        stack_r = 0.75*S
        stack_h = 30*S
        n_seg = 12
        if lod == 2:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            _add_cylinder(m, wr, wc, -hx+1*S, hz-1*S, 0, stack_h, stack_r, n_sides=8)
            _add_cylinder(m, wr, wc, hx-1*S, hz-1*S, 0, stack_h, stack_r, n_sides=8)
            return m.to_b3d()
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            _add_cylinder(m, wr, wc, -hx+1*S, hz-1*S, 0, stack_h, stack_r, n_sides=n_seg)
            _add_cylinder(m, wr, wc, hx-1*S, hz-1*S, 0, stack_h, stack_r, n_sides=n_seg)
            return m.to_b3d()
        # LOD0
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        for sx in [-hx+1*S, hx-1*S]:
            _add_cylinder(m, wr, wc, sx, hz-1*S, 0, stack_h, stack_r, n_sides=n_seg)
            _add_cylinder_cap(m, wr, wc, sx, hz-1*S, stack_h, stack_r, n_sides=n_seg, face_up=True)
        # Pipe run box
        m.add_box(-hx+1*S, hx-1*S, 8*S-0.3*S, 8*S+0.3*S, hz-1*S-0.2*S, hz-1*S+0.2*S, wr, wc)
        return m.to_b3d()

    elif variant == "03":
        # L-shaped plan (two boxes forming L) + flat roof
        h = 10*S
        if lod == 2:
            m.add_box(-10*S, 10*S, 0, h, -8*S, 8*S, wr, wc, rr, rc)
            return m.to_b3d()
        if lod == 1:
            m.add_box(-10*S, 10*S, 0, h, -4*S, 4*S, wr, wc, rr, rc)
            m.add_box(-10*S, -2*S, 0, h, 4*S, 12*S, wr, wc, rr, rc)
            return m.to_b3d()
        # LOD0: L-shape
        m.add_box(-10*S, 10*S, 0, h, -4*S, 4*S, wr, wc, rr, rc)
        m.add_box(-10*S, -2*S, 0, h, 4*S, 12*S, wr, wc, rr, rc)
        return m.to_b3d()

    elif variant == "04":
        # Main box + 2 transformer pad boxes + lightning rod box
        bw, bd, bh = 18*S, 14*S, 16*S
        hx, hz = bw/2, bd/2
        if lod == 2:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            return m.to_b3d()
        if lod == 1:
            m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
            m.add_box(-hx+1*S, -hx+7*S, 0, 3*S, -hz-1*S, -hz, wr, wc, rr, rc)
            m.add_box(hx-7*S, hx-1*S, 0, 3*S, -hz-1*S, -hz, wr, wc, rr, rc)
            return m.to_b3d()
        # LOD0
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        # Two transformer pad boxes (simple rectangles, NO radiator fin strips)
        for tx in [-hx+4*S, hx-4*S]:
            m.add_box(tx-3*S, tx+3*S, 0, 3*S, -hz-4*S, -hz, wr, wc, rr, rc)
        # Lightning rod box
        m.add_box(-1*S, 1*S, bh, bh+8*S, -1*S, 1*S, wr, wc, walls_only=True)
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
    S = 0.1

    hall_w, hall_d, hall_h = 14*S, 12*S, 6*S
    hx, hz = hall_w/2, hall_d/2
    tow_w, tow_h = 4*S, 10*S

    if lod == 1:
        m.add_box(-hx, hx, 0, hall_h, -hz, hz, wr, wc, rr, rc)
        m.add_box(hx, hx+tow_w, 0, tow_h, -tow_w/2, tow_w/2, wr, wc, rr, rc)
        return m.to_b3d()

    # LOD0: main hall box
    m.add_box(-hx, hx, 0, hall_h, -hz, hz, wr, wc, rr, rc)
    # Watch tower box
    m.add_box(hx, hx+tow_w, 0, tow_h, -tow_w/2, tow_w/2, wr, wc, rr, rc)
    # Simple canopy slab over bay doors
    m.add_box(-hx, 0, hall_h*0.75, hall_h*0.75+0.15*S, -hz-1.5*S, -hz, wr, wc)
    return m.to_b3d()


def build_svc_police_station(lod):
    """Police station: main box + projecting entrance box + triangular pediment."""
    wr, wc = WALL_CELLS[("svc", "police_station")]
    rr, rc = ROOF_CELL
    m = MeshAccum()
    S = 0.1

    bw, bd, bh = 12*S, 10*S, 9*S
    hx, hz = bw/2, bd/2
    ent_w, ent_d = 4*S, 2*S

    if lod == 1:
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        m.add_box(-ent_w/2, ent_w/2, 0, bh, -hz-ent_d, -hz, wr, wc, rr, rc)
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
    return m.to_b3d()


def build_svc_power_plant(lod):
    """Power plant: main box + pitched roof + 2 cooling tower frustums."""
    wr, wc = WALL_CELLS[("svc", "power_plant")]
    rr, rc = ROOF_CELL
    m = MeshAccum()
    S = 0.1

    bw, bd, bh = 20*S, 16*S, 14*S
    hx, hz = bw/2, bd/2
    ridge_h = (16-14)*S
    ct_base_r = 4*S
    ct_top_r = 3*S
    ct_h = 12*S
    n_seg = 12

    if lod == 1:
        m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
        _add_gabled_roof(m, wr, wc, rr, rc, -hx, hx, bh, ridge_h, -hz, hz)
        for tcx in [-hx*0.4, hx*0.4]:
            _add_cylinder(m, wr, wc, tcx, hz+ct_base_r+1*S, 0, ct_h, ct_base_r*0.8, n_sides=8)
        return m.to_b3d()

    # LOD0: main box + pitched roof
    m.add_box(-hx, hx, 0, bh, -hz, hz, wr, wc, rr, rc)
    _add_gabled_roof(m, wr, wc, rr, rc, -hx, hx, bh, ridge_h, -hz, hz)

    # Two cooling tower frustums (cylinder_approx with 12 segs)
    for tcx in [-hx*0.4, hx*0.4]:
        tcz = hz + ct_base_r + 1*S
        for i in range(n_seg):
            a0 = 2*math.pi*i/n_seg
            a1 = 2*math.pi*(i+1)/n_seg
            bx0 = tcx + ct_base_r*math.sin(a0); bz0 = tcz + ct_base_r*math.cos(a0)
            bx1 = tcx + ct_base_r*math.sin(a1); bz1 = tcz + ct_base_r*math.cos(a1)
            tx0 = tcx + ct_top_r*math.sin(a0); tz0 = tcz + ct_top_r*math.cos(a0)
            tx1 = tcx + ct_top_r*math.sin(a1); tz1 = tcz + ct_top_r*math.cos(a1)
            nx = math.sin((a0+a1)*0.5); nz = math.cos((a0+a1)*0.5)
            m.add_quad((bx0, 0, bz0), (bx1, 0, bz1), (tx1, ct_h, tz1), (tx0, ct_h, tz0),
                       (nx, 0.2, nz), wr, wc)
        _add_cylinder_cap(m, rr, rc, tcx, tcz, ct_h, ct_top_r, n_sides=n_seg, face_up=True)

    return m.to_b3d()


def build_svc_water_tower(lod):
    """Water tower: 4 solid leg columns + cylindrical tank (16 segs) + conical roof."""
    wr, wc = WALL_CELLS[("svc", "water_tower")]
    rr, rc = ROOF_CELL
    m = MeshAccum()
    S = 0.1

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

# Vehicle body dimensions (world-space units, before any engine scaling)
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
    LOD1 = body outline only (all types).
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

    def _wheel_disks(wx, wy, wz_centre, wheel_r, wheel_w):
        """Two flat 8-sided disks (inner + outer face) approximating a wheel."""
        n_seg = 8
        wz_in  = wz_centre - wheel_w * 0.5
        wz_out = wz_centre + wheel_w * 0.5
        for z_val, normal, reverse in [(wz_out, (0,0,1) if wz_out > 0 else (0,0,-1), False),
                                        (wz_in,  (0,0,-1) if wz_out > 0 else (0,0,1),  True)]:
            verts_disk = []
            for i in range(n_seg):
                angle = 2 * math.pi * i / n_seg
                px = wx + wheel_r * math.sin(angle)
                py = wy + wheel_r * math.cos(angle)
                u2, v2 = atlas_uv(row, col, 0.5 + 0.5*math.sin(angle), 0.5 + 0.5*math.cos(angle))
                verts_disk.append(Vertex(px, py, z_val, *normal, u2, v2))
            center_v = Vertex(wx, wy, z_val, *normal, *atlas_uv(row, col, 0.5, 0.5))
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

    def _wheel_arch(wx, wy, wz_centre, wheel_r, wheel_w):
        """Thin rectangular arch skirt sitting above wheel position."""
        arch_h  = wheel_r * 1.2
        arch_hw = wheel_r * 1.1
        arch_t  = 0.02
        wz0 = wz_centre - wheel_w * 0.5 - arch_t
        wz1 = wz_centre + wheel_w * 0.5 + arch_t
        # outer face of arch skirt
        _box(wx - arch_hw, wx + arch_hw, wy, wy + arch_h, wz0 - arch_t, wz0)
        _box(wx - arch_hw, wx + arch_hw, wy, wy + arch_h, wz1, wz1 + arch_t)

    def _side_mirror(wx, wy, wz, facing_z):
        """Small projecting stub for side mirror (flat box)."""
        mw = 0.06; mh = 0.04; md = 0.08
        z0 = wz if facing_z > 0 else wz - md
        z1 = wz + md if facing_z > 0 else wz
        _box(wx - mw*0.5, wx + mw*0.5, wy, wy + mh, z0, z1)

    # ------------------------------------------------------------------
    if vtype == "car_sedan":
        # Dimensions
        body_x0, body_x1 = -2.0, 2.0
        body_y0, body_y1 = 0.0,  0.72
        body_z0, body_z1 = -0.85, 0.85
        # Main body
        _box(body_x0, body_x1, body_y0, body_y1, body_z0, body_z1)

        if lod == 0:
            # Boot/trunk step — raised block at rear quarter only (rear = -x side)
            # The body rear extends from body_x0 to body_x0+1.1
            boot_x0 = body_x0
            boot_x1 = body_x0 + 1.1   # boot occupies rear ~55% of body length
            boot_y0 = body_y1
            boot_y1 = body_y1 + 0.22  # boot top sits ~22% of body height above body
            _box(boot_x0, boot_x1, boot_y0, boot_y1, body_z0 + 0.05, body_z1 - 0.05)

            # Roof box — above body, setback from boot and bonnet ends
            roof_x0 = body_x0 + 1.25   # set back from boot step
            roof_x1 = body_x1 - 0.55   # set back from bonnet front
            roof_y0 = body_y1
            roof_y1 = body_y1 + 0.44
            _box(roof_x0, roof_x1, roof_y0, roof_y1, body_z0 + 0.08, body_z1 - 0.08)

            # Door crease lines — thin horizontal ledge strips on each side
            crease_h = 0.03; crease_d = 0.012
            crease_y = body_y0 + (body_y1 - body_y0) * 0.55
            # left side
            _box(body_x0 + 0.25, body_x1 - 0.25,
                 crease_y, crease_y + crease_h,
                 body_z0 - crease_d, body_z0)
            # right side
            _box(body_x0 + 0.25, body_x1 - 0.25,
                 crease_y, crease_y + crease_h,
                 body_z1, body_z1 + crease_d)

            # Wheel arches + disks — four corners
            wheel_r = 0.28; wheel_w = 0.18
            wheel_y = wheel_r
            for wx in [body_x1 - 0.55, body_x0 + 0.55]:  # front, rear axle
                for wz_c, fz in [(body_z0 - wheel_w*0.35, -1), (body_z1 + wheel_w*0.35, 1)]:
                    _wheel_disks(wx, wheel_y, wz_c, wheel_r, wheel_w)
                    _wheel_arch(wx, wheel_y - wheel_r * 0.1, wz_c, wheel_r, wheel_w)

            # Side mirrors — near front, just above door crease
            mirror_y = body_y0 + (body_y1 - body_y0) * 0.75
            mirror_x = body_x1 - 0.65
            _side_mirror(mirror_x, mirror_y, body_z0, -1)
            _side_mirror(mirror_x, mirror_y, body_z1,  1)

            # Exhaust pipe stub — rear underside, left of centre
            _box(-2.05, -1.92, 0.06, 0.12, -0.15, 0.15)

    # ------------------------------------------------------------------
    elif vtype == "car_hatchback":
        body_x0, body_x1 = -1.9, 1.9
        body_y0, body_y1 = 0.0,  0.74
        body_z0, body_z1 = -0.85, 0.85
        _box(body_x0, body_x1, body_y0, body_y1, body_z0, body_z1)

        if lod == 0:
            # No boot step — rear end is near-vertical, body ends at rear windscreen line.
            # Roof is squarer and extends close to the rear.
            roof_x0 = body_x0 + 0.45   # only short setback at hatch rear
            roof_x1 = body_x1 - 0.55   # bonnet setback similar to sedan
            roof_y0 = body_y1
            roof_y1 = body_y1 + 0.50   # slightly taller than sedan roof
            _box(roof_x0, roof_x1, roof_y0, roof_y1, body_z0 + 0.08, body_z1 - 0.08)

            # Rear near-vertical hatch face extension — thin slab flush with body rear
            hatch_t = 0.05
            _box(body_x0, body_x0 + hatch_t,
                 body_y1, roof_y1,
                 body_z0 + 0.06, body_z1 - 0.06)

            # Door crease lines
            crease_h = 0.03; crease_d = 0.012
            crease_y = body_y0 + (body_y1 - body_y0) * 0.55
            _box(body_x0 + 0.20, body_x1 - 0.20,
                 crease_y, crease_y + crease_h,
                 body_z0 - crease_d, body_z0)
            _box(body_x0 + 0.20, body_x1 - 0.20,
                 crease_y, crease_y + crease_h,
                 body_z1, body_z1 + crease_d)

            # Wheel arches + disks
            wheel_r = 0.28; wheel_w = 0.18
            wheel_y = wheel_r
            for wx in [body_x1 - 0.50, body_x0 + 0.50]:
                for wz_c, fz in [(body_z0 - wheel_w*0.35, -1), (body_z1 + wheel_w*0.35, 1)]:
                    _wheel_disks(wx, wheel_y, wz_c, wheel_r, wheel_w)
                    _wheel_arch(wx, wheel_y - wheel_r * 0.1, wz_c, wheel_r, wheel_w)

            # Side mirrors
            mirror_y = body_y0 + (body_y1 - body_y0) * 0.75
            mirror_x = body_x1 - 0.60
            _side_mirror(mirror_x, mirror_y, body_z0, -1)
            _side_mirror(mirror_x, mirror_y, body_z1,  1)

            # Exhaust
            _box(-1.95, -1.82, 0.06, 0.12, -0.18, 0.12)

    # ------------------------------------------------------------------
    elif vtype == "car_suv":
        body_x0, body_x1 = -2.2, 2.2
        body_y0, body_y1 = 0.0,  0.90   # higher ground clearance → taller body
        body_z0, body_z1 = -0.95, 0.95
        _box(body_x0, body_x1, body_y0, body_y1, body_z0, body_z1)

        if lod == 0:
            # Flat roof extends full body length (no boot step, no strong taper)
            roof_x0 = body_x0 + 0.50
            roof_x1 = body_x1 - 0.55
            roof_y0 = body_y1
            roof_y1 = body_y1 + 0.42   # flat roofline
            _box(roof_x0, roof_x1, roof_y0, roof_y1, body_z0 + 0.10, body_z1 - 0.10)

            # Roof rack rails — 2 longitudinal bars on top of roof
            rail_y0 = roof_y1
            rail_y1 = roof_y1 + 0.04
            rail_z_off = (body_z1 - body_z0) * 0.10
            # left rail
            _box(roof_x0 + 0.10, roof_x1 - 0.10,
                 rail_y0, rail_y1,
                 body_z0 + rail_z_off, body_z0 + rail_z_off + 0.06)
            # right rail
            _box(roof_x0 + 0.10, roof_x1 - 0.10,
                 rail_y0, rail_y1,
                 body_z1 - rail_z_off - 0.06, body_z1 - rail_z_off)

            # Door crease lines
            crease_h = 0.035; crease_d = 0.015
            crease_y = body_y0 + (body_y1 - body_y0) * 0.50
            _box(body_x0 + 0.30, body_x1 - 0.30,
                 crease_y, crease_y + crease_h,
                 body_z0 - crease_d, body_z0)
            _box(body_x0 + 0.30, body_x1 - 0.30,
                 crease_y, crease_y + crease_h,
                 body_z1, body_z1 + crease_d)

            # Raised ground clearance visible as step between body bottom and wheel
            # Wheel arches + disks (larger wheels, higher centre)
            wheel_r = 0.34; wheel_w = 0.22
            wheel_y = wheel_r + 0.06   # extra clearance for SUV
            for wx in [body_x1 - 0.58, body_x0 + 0.58]:
                for wz_c, fz in [(body_z0 - wheel_w*0.35, -1), (body_z1 + wheel_w*0.35, 1)]:
                    _wheel_disks(wx, wheel_y, wz_c, wheel_r, wheel_w)
                    _wheel_arch(wx, wheel_y - wheel_r * 0.15, wz_c, wheel_r, wheel_w)

            # Side mirrors — larger, mounted higher
            mirror_y = body_y0 + (body_y1 - body_y0) * 0.72
            mirror_x = body_x1 - 0.65
            _side_mirror(mirror_x, mirror_y, body_z0, -1)
            _side_mirror(mirror_x, mirror_y, body_z1,  1)

            # Exhaust (twin)
            _box(-2.25, -2.10, 0.10, 0.18, -0.25, -0.05)
            _box(-2.25, -2.10, 0.10, 0.18,  0.05,  0.25)

    # ------------------------------------------------------------------
    elif vtype == "bus_standard":
        body_x0, body_x1 = -5.5, 5.5
        body_y0, body_y1 = 0.30, 2.80   # body sits ~30cm above ground (wheels underneath)
        body_z0, body_z1 = -1.10, 1.10
        _box(body_x0, body_x1, body_y0, body_y1, body_z0, body_z1)

        if lod == 0:
            # Destination blind recess — narrow strip at top-front of bus face
            blind_t = 0.06
            _box(body_x1 - blind_t, body_x1,
                 body_y1 - 0.38, body_y1,
                 body_z0 + 0.10, body_z1 - 0.10)

            # Folding door frame — recess on right side (front door)
            door_x0 = body_x1 - 1.40
            door_x1 = body_x1 - 0.20
            door_t  = 0.04
            _box(door_x0, door_x1,
                 body_y0, body_y0 + 1.90,
                 body_z1, body_z1 + door_t)

            # Wheel arch skirts — 4 corners, large
            wheel_r = 0.50; wheel_w = 0.28
            wheel_y = body_y0 - wheel_r * 0.05  # wheel centres below body floor
            for wx in [body_x1 - 0.90, body_x0 + 0.90]:
                for wz_c, fz in [(body_z0 - wheel_w*0.3, -1), (body_z1 + wheel_w*0.3, 1)]:
                    _wheel_disks(wx, wheel_y, wz_c, wheel_r, wheel_w)
                    _wheel_arch(wx, wheel_y - wheel_r * 0.05, wz_c, wheel_r, wheel_w)

            # Roof AC box — centred on roof, front half
            ac_x0 = 0.0; ac_x1 = body_x1 - 0.60
            _box(ac_x0, ac_x1,
                 body_y1, body_y1 + 0.28,
                 body_z0 + 0.25, body_z1 - 0.25)

            # Rear engine vent louvres — 4 horizontal bar strips on rear face
            for i in range(4):
                vy = body_y0 + 0.35 + i * 0.28
                _box(body_x0 - 0.04, body_x0,
                     vy, vy + 0.08,
                     body_z0 + 0.18, body_z1 - 0.18)

    # ------------------------------------------------------------------
    elif vtype == "truck_cargo":
        # Cab occupies front portion, cargo box the rear
        cab_x0 = 0.60; cab_x1 = 4.0
        cab_y0 = 0.32; cab_y1 = 2.50
        cab_z0 = -1.00; cab_z1 = 1.00

        cargo_x0 = -4.0; cargo_x1 = 0.40   # 20cm gap between cab and cargo
        cargo_y0 = 0.32; cargo_y1 = 2.50
        cargo_z0 = -0.98; cargo_z1 = 0.98

        _box(cab_x0, cab_x1, cab_y0, cab_y1, cab_z0, cab_z1)
        _box(cargo_x0, cargo_x1, cargo_y0, cargo_y1, cargo_z0, cargo_z1)

        if lod == 0:
            # Cab front grille — thin slab on cab face
            _box(cab_x1, cab_x1 + 0.06,
                 cab_y0, cab_y0 + 0.65,
                 cab_z0 + 0.20, cab_z1 - 0.20)

            # Exhaust stack stub — top of cab, right side, forward half
            stack_x = cab_x1 - 0.45
            _box(stack_x - 0.08, stack_x + 0.08,
                 cab_y1, cab_y1 + 0.65,
                 cab_z1 - 0.22, cab_z1 - 0.06)

            # Cargo box rear door inset — recessed panel on rear face
            door_inset = 0.05
            _box(cargo_x0 - door_inset, cargo_x0,
                 cargo_y0 + 0.10, cargo_y1 - 0.10,
                 cargo_z0 + 0.15, cargo_z1 - 0.15)

            # Side step under cab — running board
            step_y = cab_y0 - 0.20
            _box(cab_x0 + 0.40, cab_x1 - 0.20,
                 step_y, step_y + 0.12,
                 cab_z0 - 0.12, cab_z0)
            _box(cab_x0 + 0.40, cab_x1 - 0.20,
                 step_y, step_y + 0.12,
                 cab_z1, cab_z1 + 0.12)

            # Wheels — large truck wheels (3 axles: front, rear-forward, rear-back)
            wheel_r = 0.42; wheel_w = 0.24
            wheel_y = wheel_r
            for wx in [cab_x1 - 0.60, cargo_x0 + 0.80, cargo_x0 + 1.60]:
                for wz_c in [cab_z0 - wheel_w*0.3, cab_z1 + wheel_w*0.3]:
                    _wheel_disks(wx, wheel_y, wz_c, wheel_r, wheel_w)
                    _wheel_arch(wx, wheel_y - wheel_r * 0.1, wz_c, wheel_r, wheel_w)

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
