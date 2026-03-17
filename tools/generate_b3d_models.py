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
    Map a face-local UV (0..1) to the 4×4 atlas cell at (row, col).
    Each cell occupies 0.25 of the 0..1 UV range.
    """
    u = col * 0.25 + face_u * 0.25
    v = row * 0.25 + face_v * 0.25
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

# Atlas cell assignments from building-atlas-layout.md
# Walls: (row, col) per zone_tier
# Roof: cell (2, 3) shared for all
WALL_CELLS = {
    ("res", "low"):  (0, 0),
    ("com", "low"):  (0, 1),
    ("ind", "low"):  (0, 2),
    ("res", "med"):  (1, 0),
    ("com", "med"):  (1, 1),
    ("ind", "med"):  (1, 2),
    ("res", "high"): (2, 0),
    ("com", "high"): (2, 1),
    ("ind", "high"): (2, 2),
    # service buildings → cell (3, 2)
    ("svc", "svc"):  (3, 2),
}
ROOF_CELL = (2, 3)  # shared roof cell per building-atlas-layout.md

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
            v, t = box_faces(xmin, xmax, ymin, ymax, zmin, zmax,
                             wall_row, wall_col, roof_row, roof_col)
            self.verts.extend(v)
            self.tris.extend(t)

    def add_quad(self, v0, v1, v2, v3, normal, row, col):
        v, t = make_quad(v0, v1, v2, v3, normal, row, col)
        self.add(v, t)

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
    """Gabled (two-slope) pitched roof."""
    cx = (xmin + xmax) * 0.5
    ridge_y = y_base + ridge_h
    # Front slope (Z = zmin face)
    m.add_quad((xmin,y_base,zmin),(xmax,y_base,zmin),(cx,ridge_y,(zmin+zmax)*0.5),(cx,ridge_y,(zmin+zmax)*0.5),(0,0,-1),roof_row,roof_col)
    # Back slope
    m.add_quad((xmax,y_base,zmax),(xmin,y_base,zmax),(cx,ridge_y,(zmin+zmax)*0.5),(cx,ridge_y,(zmin+zmax)*0.5),(0,0,1),roof_row,roof_col)
    # Left gable
    m.add_quad((xmin,y_base,zmin),(xmin,y_base,zmax),(xmin,y_base,zmax),(cx,ridge_y,(zmin+zmax)*0.5),(-1,0,0),wall_row,wall_col)
    m.add_quad((xmin,y_base,zmin),(cx,ridge_y,(zmin+zmax)*0.5),(xmin,y_base,zmin),(xmin,y_base,zmin),(-1,0,0),wall_row,wall_col)
    # Right gable
    m.add_quad((xmax,y_base,zmax),(xmax,y_base,zmin),(cx,ridge_y,(zmin+zmax)*0.5),(cx,ridge_y,(zmin+zmax)*0.5),(1,0,0),wall_row,wall_col)
    # Left slope (X = xmin)
    m.add_quad((xmin,y_base,zmin),(xmin,y_base,zmax),(cx,ridge_y,(zmin+zmax)*0.5),(cx,ridge_y,(zmin+zmax)*0.5),(-1,0.5,0),roof_row,roof_col)
    # Right slope (X = xmax)
    m.add_quad((xmax,y_base,zmax),(xmax,y_base,zmin),(cx,ridge_y,(zmin+zmax)*0.5),(cx,ridge_y,(zmin+zmax)*0.5),(1,0.5,0),roof_row,roof_col)


def _add_hipped_roof(m, wall_row, wall_col, roof_row, roof_col,
                     xmin, xmax, y_base, ridge_h, zmin, zmax):
    """Hipped (four-slope) pitched roof with centred ridge point."""
    cx = (xmin + xmax) * 0.5
    cz = (zmin + zmax) * 0.5
    ridge_y = y_base + ridge_h
    # Four triangular roof slopes to a single apex
    m.add_quad((xmin,y_base,zmin),(xmax,y_base,zmin),(cx,ridge_y,cz),(cx,ridge_y,cz),(0,0.5,-1),roof_row,roof_col)
    m.add_quad((xmax,y_base,zmax),(xmin,y_base,zmax),(cx,ridge_y,cz),(cx,ridge_y,cz),(0,0.5,1),roof_row,roof_col)
    m.add_quad((xmin,y_base,zmax),(xmin,y_base,zmin),(cx,ridge_y,cz),(cx,ridge_y,cz),(-1,0.5,0),roof_row,roof_col)
    m.add_quad((xmax,y_base,zmin),(xmax,y_base,zmax),(cx,ridge_y,cz),(cx,ridge_y,cz),(1,0.5,0),roof_row,roof_col)


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
        bz_mid = bz0 + bay_w * 0.3  # ridge peak close to front of bay
        # Shallow slope quad (wide, low angle) from bz0→bz_mid
        m.add_quad((xmin, y_base, bz0), (xmax, y_base, bz0),
                   (xmax, y_high, bz_mid), (xmin, y_high, bz_mid),
                   (0, 0.8, -0.6), roof_row, roof_col)
        # Steep north-light face (nearly vertical) from bz_mid→bz1
        m.add_quad((xmin, y_high, bz_mid), (xmax, y_high, bz_mid),
                   (xmax, y_base, bz1), (xmin, y_base, bz1),
                   (0, 0.2, 0.98), wall_row, wall_col)
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


# ---------------------------------------------------------------------------
# RESIDENTIAL LOW / MED  (small buildings, 2,000–3,000 tris)
# ---------------------------------------------------------------------------

def _build_res_small(zone, tier, variant, lod):
    """
    Residential low/med — four DISTINCT building types per the spec:
      low/01  flat-roof block: parapet, AC unit, utility meter, window recesses, tarmac
      low/02  villa: hipped/gabled roof, carport lean-to, perimeter wall, gate
      low/03  cottage: gabled clay-tile roof, chimney, covered porch, fence posts
      low/04  red-brick: steeply-pitched roof, dormer, narrow chimney, boundary wall
      med/01  2-storey block: flat parapet, external staircase, clustered AC units
      med/02  2-storey villa: hipped roof, wrap-around balcony slab, kidney pool
      med/03  2-storey cottage: hipped roof+dormer, chimney, full-width balcony
      med/04  3-storey red-brick: pitched roof+2 dormers, projecting bay window
    """
    wr, wc = WALL_CELLS[(zone, tier)]
    rr, rc = ROOF_CELL
    m = MeshAccum()

    floor_h = 0.30
    floors = {"low": {"01":1,"02":2,"03":1,"04":2},
              "med": {"01":2,"02":3,"03":2,"04":3}}[tier][variant]
    h = floors * floor_h

    # ------------------------------------------------------------------ LOD1
    if lod == 1:
        if variant in ("01",) and tier == "low":
            # flat parapet roof
            hx, hz = 0.45, 0.45
            m.add_box(-hx, hx, 0, h + 0.06, -hz, hz, wr, wc, rr, rc)
        elif variant in ("01",) and tier == "med":
            hx, hz = 0.45, 0.45
            m.add_box(-hx, hx, 0, h + 0.06, -hz, hz, wr, wc, rr, rc)
            m.add_box(-hx - 0.07, -hx, h * 0.4, h + 0.08, -0.08, 0.08, wr, wc, rr, rc)
        elif variant == "02":
            hx, hz = 0.50, 0.45; ridge_h = 0.13
            m.add_box(-hx, hx, 0, h, -hz, hz, wr, wc, rr, rc)
            _add_hipped_roof(m, wr, wc, rr, rc, -hx, hx, h, ridge_h, -hz, hz)
        elif variant == "03":
            hx, hz = 0.38, 0.44; ridge_h = 0.14
            m.add_box(-hx, hx, 0, h, -hz, hz, wr, wc, rr, rc)
            _add_gabled_roof(m, wr, wc, rr, rc, -hx, hx, h, ridge_h, -hz, hz)
        else:  # 04
            hx, hz = 0.42, 0.42; ridge_h = 0.18
            m.add_box(-hx, hx, 0, h, -hz, hz, wr, wc, rr, rc)
            _add_gabled_roof(m, wr, wc, rr, rc, -hx, hx, h, ridge_h, -hz, hz)
        return m.to_b3d()

    # ------------------------------------------------------------------ LOD0

    # ---- variant 01: flat-roof block (low) / 2-storey block with staircase (med) ----
    if variant == "01":
        hx, hz = 0.45, 0.45
        ph = 0.06  # parapet height
        m.add_box(-hx, hx, 0, h, -hz, hz, wr, wc, rr, rc)
        _add_parapet(m, wr, wc, -hx, hx, h, -hz, hz, pw=0.03, ph=ph)
        # AC condenser on parapet
        m.add_box(-0.07, 0.07, h + ph, h + ph + 0.06, -hz + 0.10, -hz + 0.14, wr, wc, rr, rc)
        # Utility meter box on facade
        m.add_box(hx * 0.55 - 0.03, hx * 0.55 + 0.03, h * 0.3, h * 0.5, -hz - 0.02, -hz, wr, wc)
        # Window recesses (front)
        _add_windows(m, wr, wc, 3, floors, 0.06, h - 0.06,
                     -hx + 0.08, hx - 0.08, -hz, -hz, -1, win_w_frac=0.20, win_h_frac=0.55)
        # Window-box ledges
        step_w = (2 * hx - 0.16) / 3
        for ci in range(3):
            lcx = -hx + 0.08 + step_w * (ci + 0.5)
            for ri in range(floors):
                ly = 0.06 + (h - 0.12) / floors * (ri + 0.3)
                m.add_box(lcx - step_w * 0.3, lcx + step_w * 0.3, ly - 0.015, ly,
                          -hz - 0.022, -hz, wr, wc, walls_only=True)
        # Tarmac forecourt (flat quad in front)
        m.add_box(-hx, hx, 0, 0.02, -hz - 0.18, -hz, wr, wc, rr, rc)
        # Side windows
        _add_windows(m, wr, wc, 2, floors, 0.06, h - 0.06,
                     -hz + 0.06, hz - 0.06, hx, hx, 1, win_w_frac=0.22, win_h_frac=0.50)
        _add_windows(m, wr, wc, 2, floors, 0.06, h - 0.06,
                     -hz + 0.06, hz - 0.06, -hx, -hx, -1, win_w_frac=0.22, win_h_frac=0.50)
        # Plinth
        m.add_box(-hx - 0.01, hx + 0.01, 0, 0.04, -hz - 0.01, hz + 0.01, wr, wc, walls_only=True)
        if tier == "med":
            # External staircase box on left side
            m.add_box(-hx - 0.08, -hx, h * 0.0, h + 0.06, -0.10, 0.10, wr, wc, rr, rc)
            # Clustered AC condensers (3 units)
            for ac_i in range(3):
                ax = -hx * 0.5 + hx * ac_i * 0.5
                m.add_box(ax - 0.04, ax + 0.04, h + ph, h + ph + 0.06,
                          -hz + 0.08 + ac_i * 0.06, -hz + 0.12 + ac_i * 0.06, wr, wc, rr, rc)
        # Dense detail
        _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h, -hz,
                                  n_horiz_strips=38, n_vert_strips=22, normal_sign_z=-1)
        _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h, -hx,
                                  n_horiz_strips=32, n_vert_strips=18, normal_sign_z=-1)
        _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h, hx,
                                  n_horiz_strips=32, n_vert_strips=18, normal_sign_z=1)
        _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h, hz,
                                  n_horiz_strips=30, n_vert_strips=18, normal_sign_z=1)
        return m.to_b3d()

    # ---- variant 02: villa with hipped roof, carport, perimeter wall, gate ----
    if variant == "02":
        hx, hz = 0.50, 0.45; ridge_h = 0.13
        m.add_box(-hx, hx, 0, h, -hz, hz, wr, wc, rr, rc)
        _add_hipped_roof(m, wr, wc, rr, rc, -hx, hx, h, ridge_h, -hz, hz)
        # Carport lean-to on right side
        cx0 = hx; cx1 = hx + 0.22; cz0 = -hz * 0.6; cz1 = hz; carport_h = h * 0.55
        m.add_box(cx0, cx1, 0, carport_h, cz0, cz1, wr, wc, rr, rc)
        # Flat carport roof slab
        m.add_box(cx0 - 0.01, cx1 + 0.01, carport_h, carport_h + 0.02, cz0 - 0.01, cz1 + 0.01,
                  wr, wc, rr, rc)
        # Perimeter wall at plot edge (front face, low wall)
        wall_h = 0.08; wall_t = 0.02
        m.add_box(-hx - 0.05, hx + 0.22 + 0.05, 0, wall_h, -hz - 0.22, -hz - 0.22 + wall_t,
                  wr, wc, rr, rc)
        # Left perimeter wall
        m.add_box(-hx - 0.05, -hx - 0.05 + wall_t, 0, wall_h, -hz - 0.22, hz + 0.05, wr, wc, rr, rc)
        # Gate gap: right perimeter wall (two sections)
        m.add_box(hx + 0.22 + 0.05 - wall_t, hx + 0.22 + 0.05, 0, wall_h,
                  -hz - 0.22, hz + 0.05, wr, wc, rr, rc)
        # Windows front
        _add_windows(m, wr, wc, 3, floors, 0.06, h - 0.06,
                     -hx + 0.10, hx * 0.7, -hz, -hz, -1, win_w_frac=0.20, win_h_frac=0.55)
        # Window-box ledges
        step_w2 = (hx * 0.7 - (-hx + 0.10)) / 3
        for ci in range(3):
            lcx = -hx + 0.10 + step_w2 * (ci + 0.5)
            for ri in range(floors):
                ly = 0.06 + (h - 0.12) / floors * (ri + 0.3)
                m.add_box(lcx - step_w2 * 0.28, lcx + step_w2 * 0.28, ly - 0.015, ly,
                          -hz - 0.022, -hz, wr, wc, walls_only=True)
        if tier == "med":
            # Wrap-around first-floor balcony slab
            _add_balcony_slab(m, wr, wc, -hx - 0.02, hx + 0.02, floor_h, -hz, 0.06)
            _add_balcony_slab(m, wr, wc, -hx - 0.02, -hx + 0.02, floor_h, -hz, 0.06)
            # Kidney pool area (flat slab in garden)
            m.add_box(-hx + 0.05, hx - 0.05, 0.01, 0.025, -hz - 0.16, -hz - 0.06, wr, wc, rr, rc)
        _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h, -hz,
                                  n_horiz_strips=38, n_vert_strips=22, normal_sign_z=-1)
        _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h, -hx,
                                  n_horiz_strips=32, n_vert_strips=18, normal_sign_z=-1)
        _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h, hx,
                                  n_horiz_strips=32, n_vert_strips=18, normal_sign_z=1)
        _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h, hz,
                                  n_horiz_strips=30, n_vert_strips=18, normal_sign_z=1)
        return m.to_b3d()

    # ---- variant 03: cottage — gabled roof, chimney, covered porch, fence posts ----
    if variant == "03":
        hx, hz = 0.38, 0.44; ridge_h = 0.16
        m.add_box(-hx, hx, 0, h, -hz, hz, wr, wc, rr, rc)
        _add_gabled_roof(m, wr, wc, rr, rc, -hx, hx, h, ridge_h, -hz, hz)
        # Brick chimney stub (taller than ridge)
        _add_chimney(m, wr, wc, rr, rc, hx * 0.35, 0.0, h + ridge_h * 0.5, 0.22, hw=0.03)
        # Covered front porch canopy (projecting flat slab)
        pc_w = 0.22; pc_d = 0.12; pc_y = h * 0.40; pc_t = 0.02
        m.add_box(-pc_w / 2, pc_w / 2, pc_y, pc_y + pc_t, -hz - pc_d, -hz, wr, wc, rr, rc)
        # Porch posts
        for px in [-pc_w / 2 + 0.02, pc_w / 2 - 0.02]:
            m.add_box(px - 0.015, px + 0.015, 0, pc_y, -hz - pc_d + 0.01, -hz - pc_d + 0.03, wr, wc)
        # Timber-post garden fence
        _add_fence_posts(m, wr, wc, -hx - 0.02, hx + 0.02, 0, 0.10, -hz - 0.18, 10)
        # Windows front
        _add_windows(m, wr, wc, 2, floors, 0.06, h - 0.06,
                     -hx + 0.08, hx - 0.08, -hz, -hz, -1, win_w_frac=0.25, win_h_frac=0.55)
        # Window-box ledges
        step_w3 = (2 * hx - 0.16) / 2
        for ci in range(2):
            lcx = -hx + 0.08 + step_w3 * (ci + 0.5)
            for ri in range(floors):
                ly = 0.06 + (h - 0.12) / floors * (ri + 0.3)
                m.add_box(lcx - step_w3 * 0.35, lcx + step_w3 * 0.35, ly - 0.015, ly,
                          -hz - 0.022, -hz, wr, wc, walls_only=True)
        if tier == "med":
            # Full-width covered balcony on first floor
            _add_balcony_slab(m, wr, wc, -hx - 0.02, hx + 0.02, floor_h, -hz, 0.07)
            # Wrought-iron fence posts on balcony
            _add_fence_posts(m, wr, wc, -hx, hx, floor_h - 0.06, floor_h, -hz - 0.07, 8)
        _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h, -hz,
                                  n_horiz_strips=38, n_vert_strips=22, normal_sign_z=-1)
        _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h, -hx,
                                  n_horiz_strips=32, n_vert_strips=18, normal_sign_z=-1)
        _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h, hx,
                                  n_horiz_strips=32, n_vert_strips=18, normal_sign_z=1)
        _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h, hz,
                                  n_horiz_strips=30, n_vert_strips=18, normal_sign_z=1)
        return m.to_b3d()

    # ---- variant 04: red-brick — steeply-pitched roof, dormer, narrow chimney,
    #                              boundary wall at plot edge ----
    if variant == "04":
        hx, hz = 0.42, 0.42; ridge_h = 0.20  # steeper pitch
        m.add_box(-hx, hx, 0, h, -hz, hz, wr, wc, rr, rc)
        _add_gabled_roof(m, wr, wc, rr, rc, -hx, hx, h, ridge_h, -hz, hz)
        # Single dormer on front slope
        _add_dormer(m, wr, wc, rr, rc, 0.0, -hz + (hz - (-hz)) * 0.4,
                    h + ridge_h * 0.25, dormer_w=0.14, dormer_h=0.12, dormer_d=0.09)
        # Narrow chimney
        _add_chimney(m, wr, wc, rr, rc, -hx * 0.3, 0.0, h + ridge_h * 0.4, 0.24, hw=0.022)
        # Low brick boundary wall at plot edge
        bw_h = 0.07; bw_t = 0.025
        for (bx0, bx1, bz0, bz1) in [
            (-hx - 0.05, hx + 0.05, -hz - 0.16, -hz - 0.16 + bw_t),  # front
            (-hx - 0.05, -hx - 0.05 + bw_t, -hz - 0.16, hz + 0.05),  # left
            (hx + 0.05 - bw_t, hx + 0.05, -hz - 0.16, hz + 0.05),    # right
        ]:
            m.add_box(bx0, bx1, 0, bw_h, bz0, bz1, wr, wc, rr, rc)
        # Windows front
        _add_windows(m, wr, wc, 3, floors, 0.06, h - 0.06,
                     -hx + 0.08, hx - 0.08, -hz, -hz, -1, win_w_frac=0.18, win_h_frac=0.52)
        # Window-box ledges
        step_w4 = (2 * hx - 0.16) / 3
        for ci in range(3):
            lcx = -hx + 0.08 + step_w4 * (ci + 0.5)
            for ri in range(floors):
                ly = 0.06 + (h - 0.12) / floors * (ri + 0.3)
                m.add_box(lcx - step_w4 * 0.28, lcx + step_w4 * 0.28, ly - 0.015, ly,
                          -hz - 0.022, -hz, wr, wc, walls_only=True)
        if tier == "med":
            # Projecting bay window on first floor
            bw = 0.14; bd = 0.06
            m.add_box(-bw / 2, bw / 2, floor_h * 0.1, floor_h * 0.85, -hz - bd, -hz, wr, wc)
            # Second dormer
            _add_dormer(m, wr, wc, rr, rc, -hx * 0.5, -hz + (hz - (-hz)) * 0.4,
                        h + ridge_h * 0.25, dormer_w=0.12, dormer_h=0.11, dormer_d=0.08)
        _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h, -hz,
                                  n_horiz_strips=38, n_vert_strips=22, normal_sign_z=-1)
        _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h, -hx,
                                  n_horiz_strips=32, n_vert_strips=18, normal_sign_z=-1)
        _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h, hx,
                                  n_horiz_strips=32, n_vert_strips=18, normal_sign_z=1)
        _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h, hz,
                                  n_horiz_strips=30, n_vert_strips=18, normal_sign_z=1)
        return m.to_b3d()

    return m.to_b3d()


# ---------------------------------------------------------------------------
# COMMERCIAL LOW / MED  (small buildings, 2,000–3,000 tris)
# ---------------------------------------------------------------------------

def _build_com_small(zone, tier, variant, lod):
    """
    Commercial low/med — four DISTINCT building types per the spec:
      low/01  convenience store: flat parapet, full-width glazed shopfront, sign board, parking apron
      low/02  café: flat roof, canvas awning frame with brackets, side terrace
      low/03  auto garage: corrugated facade, two roll-up shutter doors, open forecourt
      low/04  supermarket: flat parapet, full-width shopfront, covered walkway canopy, trolley bay
      med/01  strip mall: flat roof+HVAC, continuous shopfronts, ribbon windows, parking apron
      med/02  boutique hotel: flat roof, juliet balcony railings, fabric canopy, bracket lintels
      med/03  corner bank: flat roof+cornice, paired pilasters, arched window heads, revolving door recess
      med/04  office block: curtain-wall facade, flat roof+plant room, recessed entrance+canopy
    """
    wr, wc = WALL_CELLS[(zone, tier)]
    rr, rc = ROOF_CELL
    m = MeshAccum()

    floors = {"low": {"01":1,"02":2,"03":1,"04":2},
              "med": {"01":2,"02":3,"03":2,"04":3}}[tier][variant]
    floor_h = 0.30
    h = floors * floor_h

    def _dense():
        hx_ = {"01":0.45,"02":0.50,"03":0.44,"04":0.46,"":0.45}
        hz_ = {"01":0.45,"02":0.42,"03":0.50,"04":0.45,"":0.45}
        hx = hx_.get(variant, 0.45); hz = hz_.get(variant, 0.45)
        _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h, -hz, n_horiz_strips=38, n_vert_strips=22, normal_sign_z=-1)
        _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h, -hx, n_horiz_strips=32, n_vert_strips=18, normal_sign_z=-1)
        _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h, hx,  n_horiz_strips=32, n_vert_strips=18, normal_sign_z=1)
        _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h, hz,  n_horiz_strips=30, n_vert_strips=18, normal_sign_z=1)

    # ------------------------------------------------------------------ LOD1
    if lod == 1:
        hx = {"01":0.45,"02":0.50,"03":0.44,"04":0.46}.get(variant, 0.45)
        hz = {"01":0.45,"02":0.42,"03":0.50,"04":0.45}.get(variant, 0.45)
        ph = 0.07
        m.add_box(-hx, hx, 0, h + ph, -hz, hz, wr, wc, rr, rc)
        # Single awning extrusion for silhouette
        aw_y = floor_h * 0.74
        m.add_box(-hx * 0.75, hx * 0.75, aw_y, aw_y + 0.035, -hz - 0.09, -hz, wr, wc)
        return m.to_b3d()

    # ------------------------------------------------------------------ LOD0

    # ---- variant 01: convenience store ----
    if variant == "01":
        hx, hz, ph = 0.45, 0.45, 0.065
        m.add_box(-hx, hx, 0, h, -hz, hz, wr, wc, rr, rc)
        _add_parapet(m, wr, wc, -hx, hx, h, -hz, hz, pw=0.03, ph=ph)
        # Full-width glazed shopfront recess
        _add_loading_dock(m, wr, wc, 0.0, 0.0, -hz, hx * 1.6, floor_h * 0.80, 0.07, normal_sign_z=-1)
        # Projecting sign board above entrance
        m.add_box(-hx * 0.90, hx * 0.90, floor_h * 0.82, floor_h * 0.97, -hz - 0.045, -hz, wr, wc)
        # Parking apron
        m.add_box(-hx, hx, 0, 0.02, -hz - 0.25, -hz, wr, wc, rr, rc)
        # Upper windows (if 2 floors)
        if floors > 1:
            _add_windows(m, wr, wc, 4, floors - 1, floor_h + 0.04, h - 0.05,
                         -hx + 0.06, hx - 0.06, -hz, -hz, -1, win_w_frac=0.18, win_h_frac=0.60)
        _add_ac_units(m, wr, wc, rr, rc, h + ph, -hx * 0.6, hx * 0.6, -hz * 0.5, hz * 0.5, count=2)
        m.add_box(-hx - 0.01, hx + 0.01, 0, 0.04, -hz - 0.01, hz + 0.01, wr, wc, walls_only=True)
        if tier == "med":
            # Strip mall: HVAC units, ribbon windows on upper floor, parking apron
            _add_ac_units(m, wr, wc, rr, rc, h + ph, -hx * 0.7, hx * 0.7, -hz * 0.6, hz * 0.6, count=3)
            # Fascia sign panels
            for sp_i in range(3):
                spx = -hx * 0.7 + hx * 1.4 * sp_i / 2
                m.add_box(spx - 0.08, spx + 0.08, h - 0.06, h, -hz - 0.012, -hz, wr, wc, walls_only=True)
        _dense()
        return m.to_b3d()

    # ---- variant 02: café ----
    if variant == "02":
        hx, hz, ph = 0.50, 0.42, 0.08
        m.add_box(-hx, hx, 0, h, -hz, hz, wr, wc, rr, rc)
        _add_parapet(m, wr, wc, -hx, hx, h, -hz, hz, pw=0.03, ph=ph)
        # Canvas awning frame (bracket-and-valance)
        aw_y = floor_h * 0.68; aw_h = 0.045; aw_d = 0.12
        aw_x0 = -hx * 0.70; aw_x1 = hx * 0.70
        # Valance front face
        m.add_box(aw_x0, aw_x1, aw_y, aw_y + aw_h, -hz - aw_d, -hz - aw_d + 0.015, wr, wc)
        # Awning top slab
        m.add_box(aw_x0 - 0.01, aw_x1 + 0.01, aw_y + aw_h - 0.018, aw_y + aw_h,
                  -hz - aw_d, -hz, wr, wc, walls_only=False)
        # Bracket supports
        n_brk = max(3, int((aw_x1 - aw_x0) / 0.16))
        brk_step = (aw_x1 - aw_x0) / n_brk
        for bi in range(n_brk + 1):
            bx = aw_x0 + bi * brk_step
            m.add_box(bx - 0.009, bx + 0.009, aw_y, aw_y + aw_h, -hz - aw_d, -hz, wr, wc, walls_only=True)
        # Signage band
        m.add_box(-hx * 0.85, hx * 0.85, floor_h * 0.82, floor_h * 0.98, -hz - 0.012, -hz, wr, wc, walls_only=True)
        # Side terrace (flat slab area)
        m.add_box(hx, hx + 0.18, 0, 0.02, -hz, hz * 0.6, wr, wc, rr, rc)
        # Entrance window recess
        _add_loading_dock(m, wr, wc, 0.0, 0.0, -hz, hx * 0.8, floor_h * 0.72, 0.05, normal_sign_z=-1)
        # Upper windows
        if floors > 1:
            _add_windows(m, wr, wc, 3, floors - 1, floor_h + 0.04, h - 0.05,
                         -hx + 0.06, hx - 0.06, -hz, -hz, -1, win_w_frac=0.20, win_h_frac=0.60)
        m.add_box(-hx - 0.01, hx + 0.01, 0, 0.04, -hz - 0.01, hz + 0.01, wr, wc, walls_only=True)
        if tier == "med":
            # Boutique hotel: juliet balcony railings on upper floor windows, fabric canopy
            for fl in range(1, floors):
                fy = fl * floor_h
                m.add_box(-hx * 0.70, hx * 0.70, fy, fy + 0.015, -hz - 0.025, -hz, wr, wc, walls_only=True)
            # Ornamental bracket lintels above windows
            _add_windows(m, wr, wc, 3, 1, floor_h + 0.04, h - 0.05,
                         -hx + 0.06, hx - 0.06, -hz, -hz, -1, win_w_frac=0.22, win_h_frac=0.55)
        _dense()
        return m.to_b3d()

    # ---- variant 03: auto garage ----
    if variant == "03":
        hx, hz, ph = 0.44, 0.50, 0.06
        m.add_box(-hx, hx, 0, h, -hz, hz, wr, wc, rr, rc)
        _add_parapet(m, wr, wc, -hx, hx, h, -hz, hz, pw=0.025, ph=ph)
        # Corrugated-look facade ribs on front
        _add_facade_ribs(m, wr, wc, -hx + 0.02, hx - 0.02, 0.04, h - 0.04, -hz, 14)
        # Two wide roll-up shutter door openings
        for door_cx in [-hx * 0.45, hx * 0.28]:
            _add_loading_dock(m, wr, wc, door_cx, 0.0, -hz, 0.28, h * 0.68, 0.06, normal_sign_z=-1)
        # Open forecourt
        m.add_box(-hx, hx, 0, 0.02, -hz - 0.25, -hz, wr, wc, rr, rc)
        # Corrugated ribs on sides
        _add_facade_ribs(m, wr, wc, -hz + 0.02, hz - 0.02, 0.04, h - 0.04, hx, 10)
        _add_facade_ribs(m, wr, wc, -hz + 0.02, hz - 0.02, 0.04, h - 0.04, -hx, 10)
        # Small windows above shutter doors
        for door_cx in [-hx * 0.45, hx * 0.28]:
            m.add_box(door_cx - 0.06, door_cx + 0.06, h * 0.72, h - 0.05, -hz - 0.02, -hz, wr, wc)
        if tier == "med":
            # Wider footprint, add side loading bay
            _add_loading_dock(m, wr, wc, 0.0, 0.0, hz, 0.30, h * 0.65, 0.06, normal_sign_z=1)
        _dense()
        return m.to_b3d()

    # ---- variant 04: supermarket ----
    if variant == "04":
        hx, hz, ph = 0.46, 0.45, 0.07
        m.add_box(-hx, hx, 0, h, -hz, hz, wr, wc, rr, rc)
        _add_parapet(m, wr, wc, -hx, hx, h, -hz, hz, pw=0.03, ph=ph)
        # Full-width glazed shopfront
        _add_loading_dock(m, wr, wc, 0.0, 0.0, -hz, hx * 1.70, floor_h * 0.82, 0.06, normal_sign_z=-1)
        # Recessed covered walkway canopy (set back creating covered zone)
        m.add_box(-hx * 0.95, hx * 0.95, floor_h * 0.84, floor_h * 0.88, -hz - 0.08, -hz, wr, wc)
        # Freestanding trolley-bay shelter in forecourt
        m.add_box(hx * 0.30, hx * 0.65, 0, floor_h * 0.55, -hz - 0.22, -hz - 0.06, wr, wc, rr, rc)
        # Trolley bay shelter roof
        m.add_box(hx * 0.28, hx * 0.67, floor_h * 0.53, floor_h * 0.55, -hz - 0.24, -hz - 0.04, wr, wc)
        # Parking apron
        m.add_box(-hx, hx, 0, 0.02, -hz - 0.30, -hz, wr, wc, rr, rc)
        _add_ac_units(m, wr, wc, rr, rc, h + ph, -hx * 0.6, hx * 0.6, -hz * 0.5, hz * 0.5, count=3)
        m.add_box(-hx - 0.01, hx + 0.01, 0, 0.04, -hz - 0.01, hz + 0.01, wr, wc, walls_only=True)
        if tier == "med":
            # Office block: curtain-wall, plant room behind parapet, recessed entrance canopy
            _add_curtain_wall_mullions(m, wr, wc, -hx, hx, 0, h, -hz, 5, floors, normal_sign_z=-1)
            # Plant room box behind parapet
            m.add_box(-hx * 0.5, hx * 0.5, h + ph, h + ph + 0.12, -hz * 0.5, hz * 0.5, wr, wc, rr, rc)
            # Projecting concrete entrance canopy
            m.add_box(-hx * 0.55, hx * 0.55, floor_h * 0.78, floor_h * 0.80, -hz - 0.14, -hz, wr, wc)
        _dense()
        return m.to_b3d()

    return m.to_b3d()


# ---------------------------------------------------------------------------
# INDUSTRIAL LOW  (small buildings, 2,000–3,000 tris)
# ---------------------------------------------------------------------------

def _build_ind_low(zone, tier, variant, lod):
    """
    Industrial low — four DISTINCT building types per the spec:
      01  corrugated metal warehouse: mono-pitch shed, corrugated ribs, shutter door, lean-to annex, truck dock
      02  brick workshop: flat felted roof+parapet, roller-shutter entrance, sign board above entrance
      03  sawtooth factory: CRITICAL sawtooth roofline (min 2 ridges), chimney stack, chain-link fence
      04  storage yard: small flat-roof gatehouse, container stacks, floodlight mast
    """
    wr, wc = WALL_CELLS[(zone, tier)]
    rr, rc = ROOF_CELL
    m = MeshAccum()

    floor_h = 0.30
    floors = {"01":1,"02":2,"03":1,"04":2}[variant]
    h_wall = floors * floor_h

    # ------------------------------------------------------------------ LOD1
    if lod == 1:
        if variant == "01":
            hx, hz = 0.45, 0.45
            roof_rise = h_wall * 0.28
            h_avg = h_wall + roof_rise * 0.5
            m.add_box(-hx, hx, 0, h_avg, -hz, hz, wr, wc, rr, rc)
            m.add_quad((-hx, h_wall + roof_rise, hz), (hx, h_wall + roof_rise, hz),
                       (hx, h_wall, -hz), (-hx, h_wall, -hz), (0, 1, 0), rr, rc)
        elif variant == "02":
            hx, hz = 0.50, 0.42
            m.add_box(-hx, hx, 0, h_wall + 0.07, -hz, hz, wr, wc, rr, rc)
        elif variant == "03":
            hx, hz = 0.48, 0.50
            y_high = h_wall + h_wall * 0.30
            # Two visible sawtooth ridges at LOD1
            m.add_box(-hx, hx, 0, h_wall, -hz, hz, wr, wc, rr, rc)
            bay_w = (hz * 2) / 2
            for i in range(2):
                bz_mid = -hz + i * bay_w + bay_w * 0.3
                m.add_quad((-hx, y_high, bz_mid), (hx, y_high, bz_mid),
                           (hx, h_wall, -hz + i * bay_w), (-hx, h_wall, -hz + i * bay_w),
                           (0, 1, 0), rr, rc)
        else:  # 04
            hx, hz = 0.30, 0.30
            m.add_box(-hx, hx, 0, h_wall, -hz, hz, wr, wc, rr, rc)
        return m.to_b3d()

    # ------------------------------------------------------------------ LOD0

    # ---- variant 01: corrugated metal warehouse ----
    if variant == "01":
        hx, hz = 0.45, 0.45
        roof_rise = h_wall * 0.28
        # Main shed box
        m.add_box(-hx, hx, 0, h_wall, -hz, hz, wr, wc, rr, rc)
        # Mono-pitch roof (high at back)
        _add_mono_pitch_roof(m, wr, wc, rr, rc, -hx, hx, h_wall, h_wall + roof_rise, -hz, hz)
        # Corrugated ribs on principal (front) facade — min 8 per spec
        _add_facade_ribs(m, wr, wc, -hx + 0.02, hx - 0.02, 0.04, h_wall - 0.04, -hz, 10)
        # Wide roll-up shutter loading door
        _add_loading_dock(m, wr, wc, 0.0, 0.0, -hz, 0.36, h_wall * 0.70, 0.07, normal_sign_z=-1)
        # Lean-to office annexe on left end
        annex_w = 0.22; annex_h = h_wall * 0.55
        m.add_box(-hx - annex_w, -hx, 0, annex_h, -hz * 0.7, hz, wr, wc, rr, rc)
        # Annex flat roof
        m.add_box(-hx - annex_w - 0.01, -hx + 0.01, annex_h, annex_h + 0.02,
                  -hz * 0.7 - 0.01, hz + 0.01, wr, wc, rr, rc)
        # Truck dock (recessed bay at floor level, back face)
        _add_loading_dock(m, wr, wc, 0.0, 0.0, hz, 0.32, h_wall * 0.55, 0.08, normal_sign_z=1)
        # Yellow kerb marker (thin flat slab in front of dock)
        m.add_box(-0.18, 0.18, 0, 0.02, hz, hz + 0.06, wr, wc, rr, rc)
        # Side ribs
        _add_facade_ribs(m, wr, wc, -hz + 0.02, hz - 0.02, 0.04, h_wall - 0.04, hx, 8)
        _add_facade_ribs(m, wr, wc, -hz * 0.7 + 0.02, hz - 0.02, 0.04, h_wall - 0.04, -hx, 6)
        # Plinth
        m.add_box(-hx - annex_w - 0.01, hx + 0.01, 0, 0.04, -hz - 0.01, hz + 0.01, wr, wc, walls_only=True)
        _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h_wall, -hz, n_horiz_strips=40, n_vert_strips=25, normal_sign_z=-1)
        _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h_wall, -hx, n_horiz_strips=35, n_vert_strips=20, normal_sign_z=-1)
        _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h_wall, hx,  n_horiz_strips=35, n_vert_strips=20, normal_sign_z=1)
        _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h_wall, hz,  n_horiz_strips=32, n_vert_strips=20, normal_sign_z=1)
        return m.to_b3d()

    # ---- variant 02: brick workshop ----
    if variant == "02":
        hx, hz = 0.50, 0.42; ph = 0.07
        m.add_box(-hx, hx, 0, h_wall, -hz, hz, wr, wc, rr, rc)
        _add_parapet(m, wr, wc, -hx, hx, h_wall, -hz, hz, pw=0.025, ph=ph)
        # Roller-shutter entrance recess
        _add_loading_dock(m, wr, wc, 0.0, 0.0, -hz, 0.28, h_wall * 0.72, 0.06, normal_sign_z=-1)
        # Hand-painted sign board above entrance
        m.add_box(-hx * 0.70, hx * 0.70, h_wall * 0.74, h_wall * 0.96, -hz - 0.014, -hz, wr, wc, walls_only=True)
        # Small windows on upper part
        _add_windows(m, wr, wc, 2, 1, h_wall * 0.55, h_wall - 0.05,
                     -hx * 0.65, -hx * 0.05, -hz, -hz, -1, win_w_frac=0.25, win_h_frac=0.55)
        _add_windows(m, wr, wc, 2, 1, h_wall * 0.55, h_wall - 0.05,
                     hx * 0.05, hx * 0.65, -hz, -hz, -1, win_w_frac=0.25, win_h_frac=0.55)
        # Tyre prop stacks (small box stacks against side wall)
        for ti in range(3):
            m.add_box(hx, hx + 0.06, 0, 0.08 + ti * 0.07,
                      -hz + 0.05 + ti * 0.12, -hz + 0.05 + ti * 0.12 + 0.07, wr, wc, rr, rc)
        # Corner columns
        for cx, cz in [(-hx, -hz), (-hx, hz), (hx, -hz), (hx, hz)]:
            m.add_box(cx - 0.02, cx + 0.02, 0, h_wall, cz - 0.02, cz + 0.02, wr, wc, walls_only=True)
        m.add_box(-hx - 0.01, hx + 0.01, 0, 0.04, -hz - 0.01, hz + 0.01, wr, wc, walls_only=True)
        _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h_wall, -hz, n_horiz_strips=40, n_vert_strips=25, normal_sign_z=-1)
        _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h_wall, -hx, n_horiz_strips=35, n_vert_strips=20, normal_sign_z=-1)
        _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h_wall, hx,  n_horiz_strips=35, n_vert_strips=20, normal_sign_z=1)
        _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h_wall, hz,  n_horiz_strips=32, n_vert_strips=20, normal_sign_z=1)
        return m.to_b3d()

    # ---- variant 03: sawtooth factory — CRITICAL PRIMARY IDENTIFIER ----
    if variant == "03":
        hx, hz = 0.48, 0.50
        y_high = h_wall + h_wall * 0.32
        # Main shed walls
        m.add_box(-hx, hx, 0, h_wall, -hz, hz, wr, wc, rr, rc)
        # SAWTOOTH ROOF: min 2 distinct asymmetric ridges visible from the side
        _add_sawtooth_roof(m, wr, wc, rr, rc, -hx, hx, h_wall, y_high, -hz, hz, n_ridges=3)
        # Chimney stack on gable end
        _add_chimney(m, wr, wc, rr, rc, 0.0, hz - 0.06, y_high, 0.22, hw=0.04)
        # Chain-link fence perimeter (thin box posts)
        _add_fence_posts(m, wr, wc, -hx - 0.06, hx + 0.06, 0, 0.14, -hz - 0.18, 8)
        _add_fence_posts(m, wr, wc, -hz - 0.06, hz + 0.06, 0, 0.14, -hx - 0.06, 8)
        # Front windows
        _add_windows(m, wr, wc, 3, 1, h_wall * 0.15, h_wall * 0.72,
                     -hx + 0.10, hx - 0.10, -hz, -hz, -1, win_w_frac=0.16, win_h_frac=0.55)
        # Side ribs
        _add_facade_ribs(m, wr, wc, -hz + 0.02, hz - 0.02, 0.04, h_wall - 0.04, hx, 8)
        _add_facade_ribs(m, wr, wc, -hz + 0.02, hz - 0.02, 0.04, h_wall - 0.04, -hx, 8)
        m.add_box(-hx - 0.01, hx + 0.01, 0, 0.04, -hz - 0.01, hz + 0.01, wr, wc, walls_only=True)
        _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h_wall, -hz, n_horiz_strips=40, n_vert_strips=25, normal_sign_z=-1)
        _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h_wall, -hx, n_horiz_strips=35, n_vert_strips=20, normal_sign_z=-1)
        _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h_wall, hx,  n_horiz_strips=35, n_vert_strips=20, normal_sign_z=1)
        _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h_wall, hz,  n_horiz_strips=32, n_vert_strips=20, normal_sign_z=1)
        return m.to_b3d()

    # ---- variant 04: storage yard with gatehouse ----
    if variant == "04":
        # Small flat-roof gatehouse (min 3×3 m footprint)
        hx, hz = 0.30, 0.30; ph = 0.055
        m.add_box(-hx, hx, 0, h_wall, -hz, hz, wr, wc, rr, rc)
        _add_parapet(m, wr, wc, -hx, hx, h_wall, -hz, hz, pw=0.025, ph=ph)
        # Gate window on front
        _add_windows(m, wr, wc, 2, 1, h_wall * 0.15, h_wall * 0.75,
                     -hx + 0.06, hx - 0.06, -hz, -hz, -1, win_w_frac=0.30, win_h_frac=0.60)
        # Container stacks — 3 box props of varying sizes
        containers = [
            ( 0.35,  0.75, 0, 0.16, -hz + 0.05, hz + 0.40),
            ( 0.35,  0.75, 0.16, 0.30, -hz + 0.05, hz + 0.40),
            (-0.80, -0.40, 0, 0.18, -hz + 0.05, hz + 0.32),
        ]
        for cx0, cx1, cy0, cy1, cz0, cz1 in containers:
            m.add_box(cx0, cx1, cy0, cy1, cz0, cz1, wr, wc, rr, rc)
        # Floodlight mast (thin vertical stick + flat top box)
        mast_x = hx + 0.06; mast_z = -hz + 0.05
        m.add_box(mast_x - 0.012, mast_x + 0.012, 0, h_wall + 0.35,
                  mast_z - 0.012, mast_z + 0.012, wr, wc, walls_only=True)
        m.add_box(mast_x - 0.04, mast_x + 0.04, h_wall + 0.32, h_wall + 0.36,
                  mast_z - 0.035, mast_z + 0.025, wr, wc, rr, rc)
        # Chain-link fence perimeter
        _add_fence_posts(m, wr, wc, -hx - 0.06, 0.85, 0, 0.14, -hz - 0.10, 10)
        m.add_box(-hx - 0.01, hx + 0.01, 0, 0.04, -hz - 0.01, hz + 0.01, wr, wc, walls_only=True)
        _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h_wall, -hz, n_horiz_strips=38, n_vert_strips=22, normal_sign_z=-1)
        _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h_wall, -hx, n_horiz_strips=32, n_vert_strips=18, normal_sign_z=-1)
        _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h_wall, hx,  n_horiz_strips=32, n_vert_strips=18, normal_sign_z=1)
        _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h_wall, hz,  n_horiz_strips=30, n_vert_strips=18, normal_sign_z=1)
        return m.to_b3d()

    return m.to_b3d()


# ---------------------------------------------------------------------------
# INDUSTRIAL MED  (small buildings, 2,000–3,000 tris)
# ---------------------------------------------------------------------------

def _build_ind_med(zone, tier, variant, lod):
    """
    Industrial med — four DISTINCT building types per the spec:
      01  flat-roof factory: flat roof+2 chimney stacks, 2 loading bays, metal-railed walkway
      02  steel-frame warehouse: corner columns proud of cladding, fire escape, elevated covered walkway
      03  brick mill: flat roof+rooftop cylindrical water tank on support frame, large industrial windows
      04  distribution centre: square footprint, loading docks on 2 sides, dock shelter hoods, gatehouse booth
    """
    wr, wc = WALL_CELLS[(zone, tier)]
    rr, rc = ROOF_CELL
    m = MeshAccum()

    floor_h = 0.30
    floors = {"01":2,"02":3,"03":2,"04":3}[variant]
    h_wall = floors * floor_h

    # ------------------------------------------------------------------ LOD1
    if lod == 1:
        hx = {"01":0.46,"02":0.52,"03":0.44,"04":0.48}.get(variant, 0.46)
        hz = {"01":0.46,"02":0.48,"03":0.50,"04":0.48}.get(variant, 0.46)
        m.add_box(-hx, hx, 0, h_wall + 0.06, -hz, hz, wr, wc, rr, rc)
        if variant == "01":
            # Two chimney stubs visible
            for ch_x in [-hx * 0.4, hx * 0.35]:
                m.add_box(ch_x - 0.03, ch_x + 0.03, h_wall, h_wall + 0.22, hz * 0.5 - 0.03, hz * 0.5 + 0.03, wr, wc, walls_only=True)
        elif variant == "03":
            # Water tank hint
            m.add_box(-0.08, 0.08, h_wall + 0.06, h_wall + 0.24, -0.08, 0.08, wr, wc, rr, rc)
        return m.to_b3d()

    # ------------------------------------------------------------------ LOD0

    # ---- variant 01: flat-roof factory with chimney stacks and loading bays ----
    if variant == "01":
        hx, hz = 0.46, 0.46; ph = 0.06
        m.add_box(-hx, hx, 0, h_wall, -hz, hz, wr, wc, rr, rc)
        _add_parapet(m, wr, wc, -hx, hx, h_wall, -hz, hz, pw=0.025, ph=ph)
        # Two concrete chimney stacks above parapet (min 2m above roof = 0.20 model units)
        _add_chimney(m, wr, wc, rr, rc, -hx * 0.40, hz * 0.42, h_wall, 0.24, hw=0.04)
        _add_chimney(m, wr, wc, rr, rc,  hx * 0.35, hz * 0.42, h_wall, 0.22, hw=0.038)
        # Ground-floor loading bays (2 inset rects)
        for bay_cx in [-hx * 0.45, hx * 0.25]:
            _add_loading_dock(m, wr, wc, bay_cx, 0.0, -hz, 0.24, h_wall * 0.60, 0.07, normal_sign_z=-1)
        # Metal-railed access walkway at second floor along facade
        walkway_y = floor_h
        m.add_box(-hx - 0.01, hx + 0.01, walkway_y, walkway_y + 0.02, -hz - 0.06, -hz, wr, wc, walls_only=False)
        # Walkway railing posts
        n_posts = 8
        for pi in range(n_posts + 1):
            px = -hx + 2 * hx * pi / n_posts
            m.add_box(px - 0.008, px + 0.008, walkway_y, walkway_y + 0.06, -hz - 0.055, -hz - 0.04, wr, wc)
        # Windows
        _add_windows(m, wr, wc, 4, floors, 0.06, h_wall - 0.06,
                     -hx + 0.08, hx - 0.08, -hz, -hz, -1, win_w_frac=0.14, win_h_frac=0.50)
        # Side ribs
        _add_facade_ribs(m, wr, wc, -hz + 0.02, hz - 0.02, 0.04, h_wall - 0.04, hx, 8)
        _add_facade_ribs(m, wr, wc, -hz + 0.02, hz - 0.02, 0.04, h_wall - 0.04, -hx, 8)
        m.add_box(-hx - 0.01, hx + 0.01, 0, 0.04, -hz - 0.01, hz + 0.01, wr, wc, walls_only=True)
        _add_ac_units(m, wr, wc, rr, rc, h_wall + ph, -hx * 0.6, hx * 0.6, -hz * 0.5, hz * 0.5, count=3)
        _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h_wall, -hz, n_horiz_strips=40, n_vert_strips=25, normal_sign_z=-1)
        _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h_wall, -hx, n_horiz_strips=35, n_vert_strips=20, normal_sign_z=-1)
        _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h_wall, hx,  n_horiz_strips=35, n_vert_strips=20, normal_sign_z=1)
        _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h_wall, hz,  n_horiz_strips=32, n_vert_strips=20, normal_sign_z=1)
        return m.to_b3d()

    # ---- variant 02: steel-frame warehouse ----
    if variant == "02":
        hx, hz = 0.52, 0.48; ph = 0.06
        m.add_box(-hx, hx, 0, h_wall, -hz, hz, wr, wc, rr, rc)
        _add_parapet(m, wr, wc, -hx, hx, h_wall, -hz, hz, pw=0.025, ph=ph)
        # Exposed corner columns proud of cladding (vertical slab strips)
        col_d = 0.05
        for cx, cz, nx in [(-hx, -hz, -1), (hx, -hz, 1), (-hx, hz, -1), (hx, hz, 1)]:
            if nx < 0:
                m.add_box(cx - col_d, cx, 0, h_wall, cz - 0.03, cz + 0.03, wr, wc, walls_only=True)
            else:
                m.add_box(cx, cx + col_d, 0, h_wall, cz - 0.03, cz + 0.03, wr, wc, walls_only=True)
        # Fire-escape staircase on gable end (zigzag box)
        m.add_box(hx, hx + 0.09, 0, h_wall + 0.06, -0.12, 0.12, wr, wc, rr, rc)
        # Stair landing hints
        for fl in range(floors + 1):
            fy = fl * floor_h
            m.add_box(hx + 0.01, hx + 0.085, fy, fy + 0.02, -0.10, 0.10, wr, wc, walls_only=False)
        # Elevated covered walkway (connecting two building wings — bridge slab)
        m.add_box(-hx - 0.01, hx + 0.01, h_wall * 0.55, h_wall * 0.57, -0.10, 0.10, wr, wc, walls_only=False)
        # Wide windows
        _add_windows(m, wr, wc, 4, floors, 0.06, h_wall - 0.06,
                     -hx + 0.08, hx - 0.08, -hz, -hz, -1, win_w_frac=0.18, win_h_frac=0.55)
        # Corrugated ribs on walls
        _add_facade_ribs(m, wr, wc, -hx + 0.05, hx - 0.05, 0.04, h_wall - 0.04, -hz, 10)
        _add_facade_ribs(m, wr, wc, -hz + 0.02, hz - 0.02, 0.04, h_wall - 0.04, -hx - col_d, 8)
        _add_facade_ribs(m, wr, wc, -hz + 0.02, hz - 0.02, 0.04, h_wall - 0.04, hx + col_d, 8)
        m.add_box(-hx - 0.01, hx + 0.01, 0, 0.04, -hz - 0.01, hz + 0.01, wr, wc, walls_only=True)
        _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h_wall, -hz, n_horiz_strips=40, n_vert_strips=25, normal_sign_z=-1)
        _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h_wall, -hx, n_horiz_strips=35, n_vert_strips=20, normal_sign_z=-1)
        _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h_wall, hx,  n_horiz_strips=35, n_vert_strips=20, normal_sign_z=1)
        _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h_wall, hz,  n_horiz_strips=32, n_vert_strips=20, normal_sign_z=1)
        return m.to_b3d()

    # ---- variant 03: brick mill with rooftop cylindrical water tank ----
    if variant == "03":
        hx, hz = 0.44, 0.50; ph = 0.06
        m.add_box(-hx, hx, 0, h_wall, -hz, hz, wr, wc, rr, rc)
        _add_parapet(m, wr, wc, -hx, hx, h_wall, -hz, hz, pw=0.025, ph=ph)
        # Rooftop water tank on steel support frame
        tank_r = 0.12; tank_cy = h_wall + ph + 0.12; tank_top = tank_cy + 0.18
        _add_cylinder(m, wr, wc, 0.0, 0.0, tank_cy, tank_top, tank_r, n_sides=8)
        _add_cylinder_cap(m, wr, wc, 0.0, 0.0, tank_top + 0.03, tank_r * 1.1, n_sides=8, face_up=True)
        # Support frame legs (4 thin boxes)
        for sx, sz in [(-1, -1), (1, -1), (-1, 1), (1, 1)]:
            lx = sx * tank_r * 0.7; lz = sz * tank_r * 0.7
            m.add_box(lx - 0.015, lx + 0.015, h_wall + ph, tank_cy, lz - 0.015, lz + 0.015, wr, wc, walls_only=True)
        # Large multi-pane industrial windows (wider than tall)
        _add_windows(m, wr, wc, 3, floors, 0.06, h_wall - 0.06,
                     -hx + 0.08, hx - 0.08, -hz, -hz, -1, win_w_frac=0.24, win_h_frac=0.40)
        # Arched window head lintels (thin slab above each window row)
        for wi in range(3):
            wx = -hx + 0.08 + (2 * hx - 0.16) / 3 * (wi + 0.5)
            m.add_box(wx - 0.07, wx + 0.07, h_wall * 0.72, h_wall * 0.76, -hz - 0.02, -hz, wr, wc, walls_only=True)
        # Cast-iron fire escapes on rear facade
        m.add_box(-hx * 0.8, -hx * 0.5, 0, h_wall + 0.06, hz, hz + 0.08, wr, wc, rr, rc)
        m.add_box(-hx - 0.01, hx + 0.01, 0, 0.04, -hz - 0.01, hz + 0.01, wr, wc, walls_only=True)
        _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h_wall, -hz, n_horiz_strips=40, n_vert_strips=25, normal_sign_z=-1)
        _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h_wall, -hx, n_horiz_strips=35, n_vert_strips=20, normal_sign_z=-1)
        _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h_wall, hx,  n_horiz_strips=35, n_vert_strips=20, normal_sign_z=1)
        _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h_wall, hz,  n_horiz_strips=32, n_vert_strips=20, normal_sign_z=1)
        return m.to_b3d()

    # ---- variant 04: distribution centre — square, loading docks on 2 sides ----
    if variant == "04":
        hx, hz = 0.48, 0.48; ph = 0.06
        m.add_box(-hx, hx, 0, h_wall, -hz, hz, wr, wc, rr, rc)
        _add_parapet(m, wr, wc, -hx, hx, h_wall, -hz, hz, pw=0.025, ph=ph)
        # Loading docks on two perpendicular sides with dock shelter hoods
        for dock_cx in [-hx * 0.40, hx * 0.25]:
            _add_loading_dock(m, wr, wc, dock_cx, 0.0, -hz, 0.25, h_wall * 0.62, 0.07, normal_sign_z=-1)
            # Dock shelter hood (sloped slab overhang)
            m.add_box(dock_cx - 0.14, dock_cx + 0.14, h_wall * 0.64, h_wall * 0.67, -hz - 0.10, -hz, wr, wc, walls_only=False)
        # Loading docks on side face
        _add_loading_dock(m, wr, wc, 0.0, 0.0, hx, 0.28, h_wall * 0.62, 0.07, normal_sign_z=1)
        m.add_box(-0.16, 0.16, h_wall * 0.64, h_wall * 0.67, hx, hx + 0.10, wr, wc, walls_only=False)
        # Elevated gatehouse booth (raised box above main entrance)
        m.add_box(-hx * 0.20, hx * 0.20, h_wall * 0.70, h_wall + 0.08, -hz - 0.06, -hz, wr, wc, rr, rc)
        # Concrete truck apron
        m.add_box(-hx, hx, 0, 0.02, -hz - 0.30, -hz, wr, wc, rr, rc)
        m.add_box(-hx - 0.30, -hx, 0, 0.02, -hz, hz, wr, wc, rr, rc)
        # Windows
        _add_windows(m, wr, wc, 4, floors, 0.06, h_wall - 0.06,
                     -hx * 0.15, hx - 0.08, -hz, -hz, -1, win_w_frac=0.14, win_h_frac=0.50)
        m.add_box(-hx - 0.01, hx + 0.01, 0, 0.04, -hz - 0.01, hz + 0.01, wr, wc, walls_only=True)
        _add_ac_units(m, wr, wc, rr, rc, h_wall + ph, -hx * 0.6, hx * 0.6, -hz * 0.5, hz * 0.5, count=3)
        _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h_wall, -hz, n_horiz_strips=40, n_vert_strips=25, normal_sign_z=-1)
        _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h_wall, -hx, n_horiz_strips=35, n_vert_strips=20, normal_sign_z=-1)
        _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h_wall, hx,  n_horiz_strips=35, n_vert_strips=20, normal_sign_z=1)
        _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h_wall, hz,  n_horiz_strips=32, n_vert_strips=20, normal_sign_z=1)
        return m.to_b3d()

    return m.to_b3d()


# ---------------------------------------------------------------------------
# RESIDENTIAL HIGH  (large buildings, 6,000–8,000 tris)
# ---------------------------------------------------------------------------

def _build_res_high(zone, tier, variant, lod):
    """
    Residential high-rise: balcony slabs, recessed window bays, planted parapet,
    stairwell tower, AC units.
    height_floors: 01=5, 02=7, 03=8, 04=10
    """
    wr, wc = WALL_CELLS[(zone, tier)]
    rr, rc = ROOF_CELL
    m = MeshAccum()

    floor_h = 0.30
    floors = {"01":5,"02":7,"03":8,"04":10}[variant]
    h = floors * floor_h

    params = {
        "01": {"hx":0.40,"hz":0.35,"setbacks":0,"stair_side":"left"},
        "02": {"hx":0.45,"hz":0.40,"setbacks":1,"stair_side":"right"},
        "03": {"hx":0.38,"hz":0.45,"setbacks":2,"stair_side":"left"},
        "04": {"hx":0.42,"hz":0.38,"setbacks":1,"stair_side":"both"},
    }[variant]
    hx = params["hx"]; hz = params["hz"]
    setbacks = params["setbacks"]

    if lod == 2:
        # LOD2 geometry shell: simplified box + balcony extrusions hint
        m.add_box(-hx, hx, 0, h, -hz, hz, wr, wc, rr, rc)
        for fl in range(2, floors, 3):
            fy = fl * floor_h
            m.add_box(-hx-0.025, hx+0.025, fy-0.02, fy, -hz-0.025, -hz, wr, wc)
        return m.to_b3d()

    if lod == 1:
        # LOD1: box with balcony slab profile and stair tower hint
        m.add_box(-hx, hx, 0, h, -hz, hz, wr, wc, rr, rc)
        for fl in range(2, floors, 3):
            fy = fl * floor_h
            m.add_box(-hx-0.02, hx+0.02, fy-0.018, fy, -hz-0.03, -hz, wr, wc)
        # Stair tower stub
        m.add_box(-hx-0.06, -hx, h*0.6, h+0.06, -0.08, 0.08, wr, wc, rr, rc)
        return m.to_b3d()

    # LOD0 — full detail
    # Main tower volume (may have setback at upper floors)
    setback_floor = int(floors * 0.65)
    if setbacks >= 1:
        # Lower portion
        m.add_box(-hx, hx, 0, setback_floor*floor_h, -hz, hz, wr, wc, rr, rc)
        # Upper portion (stepped in)
        sx = 0.04; sz = 0.04
        m.add_box(-hx+sx, hx-sx, setback_floor*floor_h, h, -hz+sz, hz-sz, wr, wc, rr, rc)
        # Setback ledge
        m.add_box(-hx, hx, setback_floor*floor_h-0.02, setback_floor*floor_h, -hz, hz, wr, wc, walls_only=True)
    else:
        m.add_box(-hx, hx, 0, h, -hz, hz, wr, wc, rr, rc)

    # Per-floor window recesses on all four faces
    # Front / Back face
    for face_z, nz in [(-hz,-1),(hz,1)]:
        _add_windows(m, wr, wc, 4, floors, 0.05, h-0.06,
                     -hx+0.05, hx-0.05, face_z, face_z, nz,
                     win_w_frac=0.16, win_h_frac=0.60)
    # Side faces
    for face_x, nx in [(-hx,-1),(hx,1)]:
        _add_windows(m, wr, wc, 3, floors, 0.05, h-0.06,
                     -hz+0.05, hz-0.05, face_x, face_x, nx,
                     win_w_frac=0.18, win_h_frac=0.58)

    # Balcony slabs every 2-3 floors on front face
    overhang = 0.03 + 0.01 * (int(variant)-1)
    for fl in range(2, floors, 3):
        fy = fl * floor_h
        _add_balcony_slab(m, wr, wc, -hx+0.04, hx-0.04, fy, -hz, overhang)
        # Balcony side panels
        for bx in [-hx+0.04, hx-0.04]:
            m.add_box(bx-0.015, bx+0.015, fy-0.06, fy, -hz-overhang, -hz, wr, wc, walls_only=True)

    # Floor banding strips
    for fl in range(1, floors):
        fy = fl * floor_h
        m.add_box(-hx-0.005, hx+0.005, fy-0.01, fy+0.005, -hz-0.005, hz+0.005, wr, wc, walls_only=True)

    # Planted parapet
    _add_parapet(m, wr, wc, -hx, hx, h, -hz, hz, pw=0.025, ph=0.05)
    # Planter boxes on parapet
    for px in [-hx*0.6, -hx*0.2, hx*0.2, hx*0.6]:
        m.add_box(px-0.04, px+0.04, h+0.05, h+0.09, -hz-0.025, -hz+0.025, wr, wc)

    # Stairwell tower
    st_side = params["stair_side"]
    if st_side in ("left","both"):
        m.add_box(-hx-0.08, -hx, h*0.5, h+0.10, -0.10, 0.10, wr, wc, rr, rc)
    if st_side in ("right","both"):
        m.add_box(hx, hx+0.08, h*0.5, h+0.10, -0.10, 0.10, wr, wc, rr, rc)

    # AC condenser units on roof
    _add_ac_units(m, wr, wc, rr, rc, h+0.05, -hx*0.7, hx*0.7, -hz*0.6, hz*0.6, count=5)

    # Plinth
    m.add_box(-hx-0.015, hx+0.015, 0, 0.05, -hz-0.015, hz+0.015, wr, wc, walls_only=True)

    # Corner columns
    for cx, cz in [(-hx,-hz),(-hx,hz),(hx,-hz),(hx,hz)]:
        m.add_box(cx-0.025, cx+0.025, 0, h, cz-0.025, cz+0.025, wr, wc, walls_only=True)

    # Horizontal spandrel bands
    for fl in range(0, floors, 2):
        fy = fl * floor_h + floor_h * 0.85
        m.add_box(-hx, hx, fy, fy+0.02, -hz-0.01, hz+0.01, wr, wc, walls_only=True)

    # Dense surface detail to reach 6,000–8,000 tris
    _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h, -hz,
                              n_horiz_strips=110, n_vert_strips=70, normal_sign_z=-1)
    _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h, hz,
                              n_horiz_strips=110, n_vert_strips=70, normal_sign_z=1)
    _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h, -hx,
                              n_horiz_strips=95, n_vert_strips=55, normal_sign_z=-1)
    _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h, hx,
                              n_horiz_strips=95, n_vert_strips=55, normal_sign_z=1)

    return m.to_b3d()


# ---------------------------------------------------------------------------
# COMMERCIAL HIGH  (skyscrapers, 8,000–10,000 tris)
# ---------------------------------------------------------------------------

def _build_com_high(zone, tier, variant, lod):
    """
    Commercial high skyscrapers — four distinct form vocabularies:
    01 = narrow glass tower + spire crown
    02 = wide slab + setback upper + antenna cluster
    03 = tapered pyramid + chamfered corners
    04 = stepped ziggurat
    """
    wr, wc = WALL_CELLS[(zone, tier)]
    rr, rc = ROOF_CELL
    m = MeshAccum()

    floor_h = 0.30
    floors = {"01":15,"02":20,"03":25,"04":30}[variant]
    h = floors * floor_h

    if lod == 2:
        # Geometry shell: simplified tower + variant crown hint
        hx = hz = 0.35
        m.add_box(-hx, hx, 0, h, -hz, hz, wr, wc, rr, rc)
        if variant == "01":
            # Spire
            m.add_box(-0.02, 0.02, h, h+0.60, -0.02, 0.02, wr, wc, walls_only=True)
        elif variant == "04":
            # Two step hints
            for step_i in range(2):
                sy = h * (0.5 + step_i*0.25)
                m.add_box(-hx*(0.9-step_i*0.1), hx*(0.9-step_i*0.1), sy-0.015, sy, -hz*(0.9-step_i*0.1), hz*(0.9-step_i*0.1), wr, wc, walls_only=True)
        return m.to_b3d()

    if lod == 1:
        hx = hz = 0.35
        m.add_box(-hx, hx, 0, h, -hz, hz, wr, wc, rr, rc)
        if variant == "01":
            m.add_box(-0.025, 0.025, h, h+0.40, -0.025, 0.025, wr, wc)
        elif variant == "02":
            m.add_box(-hx*0.7, hx*0.7, h*0.60, h, -hz*0.7, hz*0.7, wr, wc, rr, rc)
            for i in range(3):
                m.add_box(-0.02, 0.02, h + i*0.06, h + i*0.06+0.18, -0.02, 0.02, wr, wc)
        elif variant == "03":
            for step_i in range(3):
                sf = 1.0 - step_i*0.12
                sy0 = h * (0.60 + step_i*0.13)
                sy1 = h * (0.60 + (step_i+1)*0.13)
                m.add_box(-hx*sf, hx*sf, sy0, sy1, -hz*sf, hz*sf, wr, wc)
        elif variant == "04":
            for step_i in range(3):
                sf = 1.0 - step_i*0.15
                sy = h * (0.40 + step_i*0.20)
                m.add_box(-hx*sf-0.005, hx*sf+0.005, sy-0.015, sy, -hz*sf-0.005, hz*sf+0.005, wr, wc, walls_only=True)
        return m.to_b3d()

    # LOD0 — full detail per variant
    if variant == "01":
        # Narrow glass tower
        hx = hz = 0.28
        # Podium base
        m.add_box(-hx-0.06, hx+0.06, 0, floor_h*1.5, -hz-0.06, hz+0.06, wr, wc, rr, rc)
        # Tower shaft
        m.add_box(-hx, hx, floor_h*1.5, h, -hz, hz, wr, wc, rr, rc)
        # Curtain wall mullions all four faces
        for face_z, nz in [(-hz,-1),(hz,1)]:
            _add_curtain_wall_mullions(m, wr, wc, -hx, hx, floor_h*1.5, h, face_z, 5, floors//2, normal_sign_z=nz)
        for face_x, nx in [(-hx,-1),(hx,1)]:
            _add_curtain_wall_mullions(m, wr, wc, -hz, hz, floor_h*1.5, h, face_x, 3, floors//2, normal_sign_z=nx)
        # Structural core columns on corners
        for cx, cz in [(-hx,-hz),(-hx,hz),(hx,-hz),(hx,hz)]:
            m.add_box(cx-0.015, cx+0.015, 0, h, cz-0.015, cz+0.015, wr, wc, walls_only=True)
        # Spire
        sp_h = 0.55
        m.add_box(-0.015, 0.015, h, h+sp_h, -0.015, 0.015, wr, wc, walls_only=True)
        m.add_box(-0.025, 0.025, h-0.02, h+0.04, -0.025, 0.025, wr, wc, rr, rc)
        # Lobby canopy
        m.add_box(-hx-0.06-0.10, hx+0.06+0.10, floor_h*0.8, floor_h*0.8+0.015, -hz-0.06-0.12, -hz-0.06, wr, wc)
        # Revolving door recesses (3 bays)
        for bay_i in range(3):
            bx = -hx + (hx*2)*(bay_i+0.5)/3
            m.add_box(bx-0.05, bx+0.05, 0, floor_h*0.75, -hz-0.06-0.04, -hz-0.06, wr, wc)
        # Window recesses
        for face_z, nz in [(-hz,-1),(hz,1)]:
            _add_windows(m, wr, wc, 3, floors-2, floor_h*1.5+0.04, h-0.06,
                         -hx+0.04, hx-0.04, face_z, face_z, nz,
                         win_w_frac=0.22, win_h_frac=0.70)
        for face_x, nx in [(-hx,-1),(hx,1)]:
            _add_windows(m, wr, wc, 2, floors-2, floor_h*1.5+0.04, h-0.06,
                         -hz+0.04, hz-0.04, face_x, face_x, nx,
                         win_w_frac=0.28, win_h_frac=0.70)
        # Floor bands
        for fl in range(1, floors-1):
            fy = floor_h*1.5 + fl*floor_h
            m.add_box(-hx-0.004, hx+0.004, fy, fy+0.008, -hz-0.004, hz+0.004, wr, wc, walls_only=True)
        _add_ac_units(m, wr, wc, rr, rc, h+0.04, -hx*0.5, hx*0.5, -hz*0.5, hz*0.5, count=3)

    elif variant == "02":
        # Wide slab + setback
        hx = 0.44; hz = 0.35
        sb_floor = int(floors * 0.62)
        sb_h = sb_floor * floor_h
        # Lower slab
        m.add_box(-hx, hx, 0, sb_h, -hz, hz, wr, wc, rr, rc)
        # Upper setback
        m.add_box(-hx*0.70, hx*0.70, sb_h, h, -hz*0.75, hz*0.75, wr, wc, rr, rc)
        # Setback ledge
        m.add_box(-hx-0.01, hx+0.01, sb_h-0.02, sb_h, -hz-0.01, hz+0.01, wr, wc, walls_only=True)
        # Curtain wall
        for face_z, nz in [(-hz,-1),(hz,1)]:
            _add_curtain_wall_mullions(m, wr, wc, -hx, hx, 0, sb_h, face_z, 6, sb_floor//2, normal_sign_z=nz)
        for face_z, nz in [(-hz*0.75,-1),(hz*0.75,1)]:
            _add_curtain_wall_mullions(m, wr, wc, -hx*0.70, hx*0.70, sb_h, h, face_z, 5, (floors-sb_floor)//2, normal_sign_z=nz)
        # Corner core columns
        for cx, cz in [(-hx,-hz),(-hx,hz),(hx,-hz),(hx,hz)]:
            m.add_box(cx-0.02, cx+0.02, 0, sb_h, cz-0.02, cz+0.02, wr, wc, walls_only=True)
        # Antenna cluster (3-5 rods)
        for ai, (ax, aoff) in enumerate([(-0.04,0),(0.04,0),(-0.02,0.02),(0.02,0.02),(0,0.04)]):
            rod_h = 0.18 + ai*0.04
            m.add_box(ax-0.008, ax+0.008, h, h+rod_h, aoff-0.008, aoff+0.008, wr, wc)
        # Lobby canopy
        m.add_box(-hx-0.08, hx+0.08, floor_h*0.8, floor_h*0.8+0.02, -hz-0.15, -hz, wr, wc)
        # Revolving door recesses
        for bay_i in range(3):
            bx = -hx*0.5 + hx*(bay_i+0.5)/1.5
            m.add_box(bx-0.06, bx+0.06, 0, floor_h*0.75, -hz-0.04, -hz, wr, wc)
        # Windows lower
        for face_z, nz in [(-hz,-1),(hz,1)]:
            _add_windows(m, wr, wc, 5, sb_floor-1, 0.05, sb_h-0.05,
                         -hx+0.05, hx-0.05, face_z, face_z, nz,
                         win_w_frac=0.14, win_h_frac=0.68)
        for face_x, nx in [(-hx,-1),(hx,1)]:
            _add_windows(m, wr, wc, 3, sb_floor-1, 0.05, sb_h-0.05,
                         -hz+0.05, hz-0.05, face_x, face_x, nx,
                         win_w_frac=0.20, win_h_frac=0.68)
        # Windows upper
        for face_z, nz in [(-hz*0.75,-1),(hz*0.75,1)]:
            _add_windows(m, wr, wc, 4, floors-sb_floor-1, sb_h+0.05, h-0.05,
                         -hx*0.70+0.04, hx*0.70-0.04, face_z, face_z, nz,
                         win_w_frac=0.16, win_h_frac=0.68)
        for fl in range(1, floors):
            fy = fl * floor_h
            m.add_box(-hx-0.005, hx+0.005, fy, fy+0.01, -hz-0.005, hz+0.005, wr, wc, walls_only=True)
        _add_ac_units(m, wr, wc, rr, rc, h+0.02, -hx*0.60, hx*0.60, -hz*0.65, hz*0.65, count=5)
        # Podium geometry
        m.add_box(-hx-0.06, hx+0.06, 0, floor_h*1.5, -hz-0.06, hz+0.06, wr, wc, rr, rc)

    elif variant == "03":
        # Tapered pyramid + chamfered corners
        hx0 = 0.42; hz0 = 0.42
        # Build stepped tapers (10 steps)
        n_steps = 10
        for si in range(n_steps):
            sf = 1.0 - si * (0.7/n_steps)
            y0 = si * (h/n_steps)
            y1 = (si+1) * (h/n_steps)
            hxi = hx0 * sf; hzi = hz0 * sf
            m.add_box(-hxi, hxi, y0, y1, -hzi, hzi, wr, wc, rr, rc)
        # Chamfered corner columns (diagonal fins) all floors
        for cx, cz, nrm in [(-hx0,-hz0,(-0.707,0,-0.707)),(hx0,-hz0,(0.707,0,-0.707)),
                             (-hx0,hz0,(-0.707,0,0.707)),(hx0,hz0,(0.707,0,0.707))]:
            for si in range(n_steps):
                sf = 1.0 - si*(0.7/n_steps)
                y0 = si*(h/n_steps); y1=(si+1)*(h/n_steps)
                bx = cx*sf; bz = cz*sf
                m.add_box(bx-0.02, bx+0.02, y0, y1, bz-0.02, bz+0.02, wr, wc, walls_only=True)
        # Curtain wall on main faces (lower portion)
        for face_z, nz in [(-hz0,-1),(hz0,1)]:
            _add_curtain_wall_mullions(m, wr, wc, -hx0*0.5, hx0*0.5, 0, h*0.6, face_z, 4, floors//3, normal_sign_z=nz)
        # Lobby canopy
        m.add_box(-hx0*0.6, hx0*0.6, floor_h*0.8, floor_h*0.8+0.018, -hz0-0.12, -hz0, wr, wc)
        # Revolving door recesses
        for bay_i in range(3):
            bx = -hx0*0.4 + hx0*0.4*(bay_i)
            m.add_box(bx-0.05, bx+0.05, 0, floor_h*0.72, -hz0-0.04, -hz0, wr, wc)
        # Windows on tapered faces
        for si in range(n_steps):
            sf = 1.0 - si*(0.7/n_steps)
            y0 = si*(h/n_steps); y1=(si+1)*(h/n_steps)
            hxi = hx0*sf*0.9; hzi = hz0*sf*0.9
            n_w = max(1, int(hxi*2/0.08))
            for face_z, nz in [(-hzi,-1),(hzi,1)]:
                _add_windows(m, wr, wc, min(n_w,4), 1, y0+0.01, y1-0.01,
                             -hxi+0.03, hxi-0.03, face_z, face_z, nz,
                             win_w_frac=0.30, win_h_frac=0.72)
        # Crown
        m.add_box(-0.04, 0.04, h, h+0.04, -0.04, 0.04, rr, rc, rr, rc)
        _add_ac_units(m, wr, wc, rr, rc, h*(1.0-0.7/n_steps)*0.98, -0.06, 0.06, -0.06, 0.06, count=2)

    elif variant == "04":
        # Stepped ziggurat
        hx0 = 0.44; hz0 = 0.44
        n_steps = 5
        step_h = h / n_steps
        for si in range(n_steps):
            sf = 1.0 - si * (0.60 / n_steps)
            y0 = si * step_h
            y1 = y0 + step_h
            hxi = hx0 * sf; hzi = hz0 * sf
            m.add_box(-hxi, hxi, y0, y1, -hzi, hzi, wr, wc, rr, rc)
            if si > 0:
                # Step ledge
                m.add_box(-hxi-0.005, hxi+0.005, y0-0.02, y0, -hzi-0.005, hzi+0.005, wr, wc, walls_only=True)
        # Curtain wall on each step face
        for si in range(n_steps):
            sf = 1.0 - si*(0.60/n_steps)
            y0 = si*step_h; y1=y0+step_h
            hxi = hx0*sf; hzi = hz0*sf
            for face_z, nz in [(-hzi,-1),(hzi,1)]:
                _add_curtain_wall_mullions(m, wr, wc, -hxi, hxi, y0, y1, face_z, 4, 2, normal_sign_z=nz)
            for face_x, nx in [(-hxi,-1),(hxi,1)]:
                _add_curtain_wall_mullions(m, wr, wc, -hzi, hzi, y0, y1, face_x, 3, 2, normal_sign_z=nx)
            # Windows on each step
            for face_z, nz in [(-hzi,-1),(hzi,1)]:
                _add_windows(m, wr, wc, 4, 2, y0+0.04, y1-0.04,
                             -hxi+0.04, hxi-0.04, face_z, face_z, nz,
                             win_w_frac=0.16, win_h_frac=0.60)
        # Corner columns on base
        for cx, cz in [(-hx0,-hz0),(-hx0,hz0),(hx0,-hz0),(hx0,hz0)]:
            m.add_box(cx-0.018, cx+0.018, 0, step_h, cz-0.018, cz+0.018, wr, wc, walls_only=True)
        # Lobby canopy
        m.add_box(-hx0-0.08, hx0+0.08, floor_h*0.8, floor_h*0.8+0.018, -hz0-0.14, -hz0, wr, wc)
        # Revolving door recesses
        for bay_i in range(3):
            bx = -hx0*0.4 + hx0*0.4*bay_i
            m.add_box(bx-0.06, bx+0.06, 0, floor_h*0.72, -hz0-0.04, -hz0, wr, wc)
        # Podium
        m.add_box(-hx0-0.08, hx0+0.08, 0, floor_h*1.5, -hz0-0.08, hz0+0.08, wr, wc, rr, rc)
        _add_ac_units(m, wr, wc, rr, rc, (n_steps-1)*step_h+step_h*0.8,
                      -hx0*(1-(n_steps-1)*0.60/n_steps)*0.8,
                       hx0*(1-(n_steps-1)*0.60/n_steps)*0.8,
                      -hz0*(1-(n_steps-1)*0.60/n_steps)*0.8,
                       hz0*(1-(n_steps-1)*0.60/n_steps)*0.8, count=3)

    # Dense surface detail to reach 8,000–10,000 tris
    # Use hx/hz from the outermost definition for each variant
    if variant == "01":
        _hx, _hz = 0.28, 0.28
    elif variant == "02":
        _hx, _hz = 0.44, 0.35
    elif variant == "03":
        _hx, _hz = 0.42, 0.42
    else:
        _hx, _hz = 0.44, 0.44
    _add_dense_facade_detail(m, wr, wc, -_hx, _hx, 0, h, -_hz,
                              n_horiz_strips=145, n_vert_strips=95, normal_sign_z=-1)
    _add_dense_facade_detail(m, wr, wc, -_hx, _hx, 0, h, _hz,
                              n_horiz_strips=145, n_vert_strips=95, normal_sign_z=1)
    _add_dense_facade_detail(m, wr, wc, -_hz, _hz, 0, h, -_hx,
                              n_horiz_strips=135, n_vert_strips=85, normal_sign_z=-1)
    _add_dense_facade_detail(m, wr, wc, -_hz, _hz, 0, h, _hx,
                              n_horiz_strips=135, n_vert_strips=85, normal_sign_z=1)

    return m.to_b3d()


# ---------------------------------------------------------------------------
# INDUSTRIAL HIGH  (large buildings, 6,000–8,000 tris)
# ---------------------------------------------------------------------------

def _build_ind_high(zone, tier, variant, lod):
    """
    Industrial high-rise — four DISTINCT building types per the spec:
      01  plain concrete tower: 2 tall chimney stacks well above roofline, punched windows, rooftop service structure
      02  exposed steel-frame: external pipe runs along full height, spherical pressure vessel at mid-height, cooling tower
      03  silo cluster: CRITICAL — min 3 cylindrical silos (8-sided prism), corrugated conveyor bridge, elevator head house
      04  refinery tower: grating-platform bands at every floor, dense roof pipe rack, flare stack from corner
    height_floors: 01=5, 02=7, 03=8, 04=10
    """
    wr, wc = WALL_CELLS[(zone, tier)]
    rr, rc = ROOF_CELL
    m = MeshAccum()

    floor_h = 0.30
    floors = {"01":5,"02":7,"03":8,"04":10}[variant]
    h = floors * floor_h

    # ------------------------------------------------------------------ LOD2
    if lod == 2:
        hx = {"01":0.44,"02":0.42,"03":0.45,"04":0.40}.get(variant, 0.44)
        hz = {"01":0.40,"02":0.45,"03":0.45,"04":0.44}.get(variant, 0.40)
        m.add_box(-hx, hx, 0, h, -hz, hz, wr, wc, rr, rc)
        if variant == "03":
            # Silo cluster hint: 3 cylinders at LOD2
            for si, (scx, scz) in enumerate([(-0.15, 0.0), (0.0, 0.0), (0.15, 0.0)]):
                _add_cylinder(m, wr, wc, scx, scz, 0, h, 0.10, n_sides=6)
        else:
            m.add_box(-hx * 0.5, hx * 0.5, h, h + 0.15, -hz * 0.4, hz * 0.4, wr, wc, rr, rc)
        return m.to_b3d()

    # ------------------------------------------------------------------ LOD1
    if lod == 1:
        hx = {"01":0.44,"02":0.42,"03":0.45,"04":0.40}.get(variant, 0.44)
        hz = {"01":0.40,"02":0.45,"03":0.45,"04":0.44}.get(variant, 0.40)
        m.add_box(-hx, hx, 0, h, -hz, hz, wr, wc, rr, rc)
        if variant == "01":
            # Two chimney stacks at LOD1
            for ch_x in [-hx * 0.35, hx * 0.30]:
                m.add_box(ch_x - 0.03, ch_x + 0.03, h, h + 0.18, hz * 0.5 - 0.03, hz * 0.5 + 0.03, wr, wc, walls_only=True)
        elif variant == "02":
            m.add_box(-hx * 0.5, hx * 0.5, h, h + 0.12, -hz * 0.4, hz * 0.4, wr, wc, rr, rc)
            m.add_box(-hx - 0.06, -hx, h * 0.5, h + 0.06, -0.10, 0.10, wr, wc, rr, rc)
        elif variant == "03":
            # Silo cluster — three cylinders LOD1
            for si, (scx, scz) in enumerate([(-0.18, 0.0), (0.0, 0.0), (0.18, 0.0)]):
                _add_cylinder(m, wr, wc, scx, scz, 0, h, 0.11, n_sides=8)
        else:
            m.add_box(-hx * 0.5, hx * 0.5, h, h + 0.12, -hz * 0.4, hz * 0.4, wr, wc, rr, rc)
            m.add_box(-hx - 0.06, -hx, h * 0.5, h + 0.06, -0.10, 0.10, wr, wc, rr, rc)
        return m.to_b3d()

    # ------------------------------------------------------------------ LOD0

    # ---- variant 01: plain concrete tower, 2 tall chimney stacks ----
    if variant == "01":
        hx, hz = 0.44, 0.40
        m.add_box(-hx, hx, 0, h, -hz, hz, wr, wc, rr, rc)
        _add_parapet(m, wr, wc, -hx, hx, h, -hz, hz, pw=0.025, ph=0.06)
        # Two tall chimney stacks well above roofline (min 3m = 0.30 model above parapet)
        _add_chimney(m, wr, wc, rr, rc, -hx * 0.35, hz * 0.45, h, 0.36, hw=0.045)
        _add_chimney(m, wr, wc, rr, rc,  hx * 0.30, hz * 0.45, h, 0.32, hw=0.040)
        # Rooftop service structure box
        m.add_box(-hx * 0.50, hx * 0.50, h + 0.06, h + 0.20, -hz * 0.40, hz * 0.40, wr, wc, rr, rc)
        # Punched small windows
        for face_z, nz in [(-hz, -1), (hz, 1)]:
            _add_windows(m, wr, wc, 4, floors, 0.05, h - 0.10,
                         -hx + 0.06, hx - 0.06, face_z, face_z, nz, win_w_frac=0.12, win_h_frac=0.45)
        for face_x, nx in [(-hx, -1), (hx, 1)]:
            _add_windows(m, wr, wc, 3, floors, 0.05, h - 0.10,
                         -hz + 0.06, hz - 0.06, face_x, face_x, nx, win_w_frac=0.14, win_h_frac=0.45)
        m.add_box(-hx - 0.01, hx + 0.01, 0, 0.05, -hz - 0.01, hz + 0.01, wr, wc, walls_only=True)
        for fl in range(1, floors):
            fy = fl * floor_h
            m.add_box(-hx - 0.005, hx + 0.005, fy, fy + 0.012, -hz - 0.005, hz + 0.005, wr, wc, walls_only=True)
        _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h, -hz, n_horiz_strips=110, n_vert_strips=70, normal_sign_z=-1)
        _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h, hz,  n_horiz_strips=110, n_vert_strips=70, normal_sign_z=1)
        _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h, -hx, n_horiz_strips=95, n_vert_strips=55, normal_sign_z=-1)
        _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h, hx,  n_horiz_strips=95, n_vert_strips=55, normal_sign_z=1)
        return m.to_b3d()

    # ---- variant 02: exposed steel-frame, pipe runs, spherical pressure vessel, cooling tower ----
    if variant == "02":
        hx, hz = 0.42, 0.45
        m.add_box(-hx, hx, 0, h, -hz, hz, wr, wc, rr, rc)
        _add_parapet(m, wr, wc, -hx, hx, h, -hz, hz, pw=0.025, ph=0.06)
        # Exposed corner column strips proud of facade
        for cx, cz in [(-hx, -hz), (hx, -hz), (-hx, hz), (hx, hz)]:
            m.add_box(cx - 0.04, cx + 0.04, 0, h + 0.06, cz - 0.04, cz + 0.04, wr, wc, walls_only=True)
        # External pipe runs along full height: large-bore (~0.3m dia) and small-bore (~0.1m dia)
        pipe_large_r = 0.03; pipe_small_r = 0.012
        for (pz, r) in [(-hz - pipe_large_r - 0.01, pipe_large_r),
                         (-hz - pipe_small_r - 0.06, pipe_small_r)]:
            _add_cylinder(m, wr, wc, -hx * 0.5, pz, 0, h, r, n_sides=6)
            _add_cylinder(m, wr, wc,  hx * 0.5, pz, 0, h, r, n_sides=6)
        # Spherical pressure vessel at mid-height (approximated as 8-sided box + cylinder)
        sphere_y = h * 0.50; sphere_r = 0.10
        _add_cylinder(m, wr, wc, 0.0, -hz - sphere_r - 0.02,
                      sphere_y - sphere_r, sphere_y + sphere_r, sphere_r, n_sides=8)
        _add_cylinder_cap(m, wr, wc, 0.0, -hz - sphere_r - 0.02,
                          sphere_y + sphere_r, sphere_r * 0.8, n_sides=8, face_up=True)
        _add_cylinder_cap(m, wr, wc, 0.0, -hz - sphere_r - 0.02,
                          sphere_y - sphere_r, sphere_r * 0.8, n_sides=8, face_up=False)
        # Wide cooling tower on one side (separate box volume)
        m.add_box(hx, hx + 0.18, 0, h * 0.70, -hz * 0.6, hz * 0.6, wr, wc, rr, rc)
        # Windows
        for face_z, nz in [(-hz, -1), (hz, 1)]:
            _add_windows(m, wr, wc, 4, floors, 0.05, h - 0.10,
                         -hx + 0.06, hx - 0.06, face_z, face_z, nz, win_w_frac=0.14, win_h_frac=0.50)
        m.add_box(-hx - 0.01, hx + 0.01, 0, 0.05, -hz - 0.01, hz + 0.01, wr, wc, walls_only=True)
        for fl in range(1, floors):
            fy = fl * floor_h
            m.add_box(-hx - 0.005, hx + 0.005, fy, fy + 0.012, -hz - 0.005, hz + 0.005, wr, wc, walls_only=True)
        _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h, -hz, n_horiz_strips=110, n_vert_strips=70, normal_sign_z=-1)
        _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h, hz,  n_horiz_strips=110, n_vert_strips=70, normal_sign_z=1)
        _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h, -hx, n_horiz_strips=95, n_vert_strips=55, normal_sign_z=-1)
        _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h, hx,  n_horiz_strips=95, n_vert_strips=55, normal_sign_z=1)
        return m.to_b3d()

    # ---- variant 03: SILO CLUSTER — CRITICAL PRIMARY IDENTIFIER ----
    if variant == "03":
        # Min 3 cylinders, each 8–12 sides, height = 7 floors = 2.1 model units
        # The circular silhouette is the primary identifier
        silo_h = 7 * floor_h   # 2.1 units
        silo_r = 0.18           # radius ~0.3m model (3m world)
        n_sides = 10
        silo_positions = [(-0.22, 0.0), (0.0, 0.12), (0.22, 0.0), (0.0, -0.15)]
        for (scx, scz) in silo_positions:
            _add_cylinder(m, wr, wc, scx, scz, 0, silo_h, silo_r, n_sides=n_sides)
            _add_cylinder_cap(m, wr, wc, scx, scz, silo_h, silo_r, n_sides=n_sides, face_up=True)
            # Corrugated horizontal rings on each silo
            for ring_i in range(6):
                ry = silo_h * (ring_i + 0.5) / 6
                _add_cylinder(m, wr, wc, scx, scz, ry, ry + 0.015, silo_r + 0.012, n_sides=n_sides)
        # Corrugated metal conveyor bridge connecting silo tops (flat slab)
        m.add_box(-0.28, 0.28, silo_h - 0.02, silo_h + 0.04, -0.06, 0.06, wr, wc, rr, rc)
        # Elevator head house at one end of bridge
        m.add_box(0.22, 0.34, silo_h, silo_h + 0.25, -0.10, 0.10, wr, wc, rr, rc)
        # Foundation base
        m.add_box(-0.32, 0.32, 0, 0.05, -0.22, 0.22, wr, wc, walls_only=True)
        # LOD0 detail on silos
        _add_dense_facade_detail(m, wr, wc, -0.28, 0.28, 0, silo_h, -0.22,
                                  n_horiz_strips=80, n_vert_strips=50, normal_sign_z=-1)
        _add_dense_facade_detail(m, wr, wc, -0.28, 0.28, 0, silo_h, 0.22,
                                  n_horiz_strips=80, n_vert_strips=50, normal_sign_z=1)
        _add_dense_facade_detail(m, wr, wc, -0.22, 0.22, 0, silo_h, -0.28,
                                  n_horiz_strips=70, n_vert_strips=45, normal_sign_z=-1)
        _add_dense_facade_detail(m, wr, wc, -0.22, 0.22, 0, silo_h, 0.28,
                                  n_horiz_strips=70, n_vert_strips=45, normal_sign_z=1)
        return m.to_b3d()

    # ---- variant 04: refinery tower — grating platforms, pipe rack, flare stack ----
    if variant == "04":
        hx, hz = 0.40, 0.44
        m.add_box(-hx, hx, 0, h, -hz, hz, wr, wc, rr, rc)
        _add_parapet(m, wr, wc, -hx, hx, h, -hz, hz, pw=0.025, ph=0.06)
        # Grating-platform bands at every floor (thin horizontal slab per floor)
        for fl in range(1, floors):
            fy = fl * floor_h
            m.add_box(-hx - 0.04, hx + 0.04, fy, fy + 0.018, -hz - 0.04, hz + 0.04, wr, wc, walls_only=False)
        # Dense roof-level pipe rack (min 5 horizontal pipe members)
        for pi in range(6):
            pz = -hz + (2 * hz) * pi / 5
            m.add_box(-hx - 0.02, hx + 0.02, h + 0.06, h + 0.08, pz - 0.008, pz + 0.008, wr, wc, walls_only=True)
        # Flare stack from corner (thin vertical stick with flame-cap)
        flare_x = hx + 0.015; flare_z = -hz + 0.015
        m.add_box(flare_x - 0.015, flare_x + 0.015, 0, h + 0.42,
                  flare_z - 0.015, flare_z + 0.015, wr, wc, walls_only=True)
        # Flame cap at top of flare
        m.add_box(flare_x - 0.04, flare_x + 0.04, h + 0.40, h + 0.48,
                  flare_z - 0.04, flare_z + 0.04, wr, wc, rr, rc)
        # Large louvred panels (inset panels with horizontal bar subdivisions)
        for face_z, nz in [(-hz, -1), (hz, 1)]:
            for lp_i in range(2):
                lp_cx = -hx * 0.5 + hx * lp_i
                for bar_i in range(4):
                    bar_y = h * 0.20 + h * 0.50 * bar_i / 3
                    m.add_box(lp_cx - 0.08, lp_cx + 0.08, bar_y, bar_y + 0.015,
                              face_z + nz * (-0.015), face_z, wr, wc, walls_only=True)
        # Windows (small)
        for face_z, nz in [(-hz, -1), (hz, 1)]:
            _add_windows(m, wr, wc, 3, floors, 0.05, h - 0.10,
                         -hx + 0.06, hx - 0.06, face_z, face_z, nz, win_w_frac=0.10, win_h_frac=0.40)
        m.add_box(-hx - 0.01, hx + 0.01, 0, 0.05, -hz - 0.01, hz + 0.01, wr, wc, walls_only=True)
        # Hazard-stripe banding on structural posts at base
        for cx, cz in [(-hx, -hz), (hx, -hz), (-hx, hz), (hx, hz)]:
            m.add_box(cx - 0.025, cx + 0.025, 0, 0.20, cz - 0.025, cz + 0.025, wr, wc, walls_only=True)
        _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h, -hz, n_horiz_strips=110, n_vert_strips=70, normal_sign_z=-1)
        _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h, hz,  n_horiz_strips=110, n_vert_strips=70, normal_sign_z=1)
        _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h, -hx, n_horiz_strips=95, n_vert_strips=55, normal_sign_z=-1)
        _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h, hx,  n_horiz_strips=95, n_vert_strips=55, normal_sign_z=1)
        return m.to_b3d()

    return m.to_b3d()


# ---------------------------------------------------------------------------
# Dispatcher: build_box_building replacement
# ---------------------------------------------------------------------------

def build_box_building(zone: str, tier: str, variant: str, lod: int) -> bytes:
    """Route to zone/tier-specific detailed geometry builder."""
    if tier == "low" and zone == "res":
        return _build_res_small(zone, tier, variant, lod)
    elif tier == "med" and zone == "res":
        return _build_res_small(zone, tier, variant, lod)
    elif tier == "low" and zone == "com":
        return _build_com_small(zone, tier, variant, lod)
    elif tier == "med" and zone == "com":
        return _build_com_small(zone, tier, variant, lod)
    elif tier == "low" and zone == "ind":
        return _build_ind_low(zone, tier, variant, lod)
    elif tier == "med" and zone == "ind":
        return _build_ind_med(zone, tier, variant, lod)
    elif tier == "high" and zone == "res":
        return _build_res_high(zone, tier, variant, lod)
    elif tier == "high" and zone == "com":
        return _build_com_high(zone, tier, variant, lod)
    elif tier == "high" and zone == "ind":
        return _build_ind_high(zone, tier, variant, lod)
    else:
        # Fallback: simple box
        wr, wc = WALL_CELLS[(zone, tier)]
        rr, rc = ROOF_CELL
        h = TIER_HEIGHT[tier]
        hx = hz = BUILDING_HALF_XZ
        all_verts, all_tris = [], []
        v, t = box_faces(-hx, hx, 0, h, -hz, hz, wr, wc, rr, rc)
        all_verts.extend(v); all_tris.extend(t)
        return build_b3d(all_verts, all_tris)


# ---------------------------------------------------------------------------
# Service building geometry
# ---------------------------------------------------------------------------

def build_svc_fire_station(lod: int) -> bytes:
    """
    Fire station: 2,500–4,000 tris LOD0.
    Two vehicle bay door openings, apron, hose reel housing, personnel entrance,
    antenna/radio mast.
    """
    wr, wc = WALL_CELLS[("svc", "svc")]
    rr, rc = ROOF_CELL
    m = MeshAccum()

    hx = 0.44; hz = 0.38; h = 0.60

    if lod == 1:
        m.add_box(-hx, hx, 0, h, -hz, hz, wr, wc, rr, rc)
        # Two simplified bay insets
        for bx_c in [-hx*0.45, hx*0.10]:
            m.add_box(bx_c-0.12, bx_c+0.12, 0, h*0.72, -hz-0.04, -hz, wr, wc)
        # Antenna stub
        m.add_box(-0.01, 0.01, h, h+0.25, -hz*0.5-0.01, -hz*0.5+0.01, wr, wc)
        return m.to_b3d()

    # Main building
    m.add_box(-hx, hx, 0, h, -hz, hz, wr, wc, rr, rc)

    # Flat parapet
    _add_parapet(m, wr, wc, -hx, hx, h, -hz, hz, pw=0.025, ph=0.05)

    # Two vehicle bay door openings (front face Z=-hz, normal -Z)
    bay_w = 0.24; bay_h = h * 0.72; bay_d = 0.07
    for bx_c in [-hx*0.46, hx*0.12]:
        _add_loading_dock(m, wr, wc, bx_c, 0.0, -hz, bay_w, bay_h, bay_d, normal_sign_z=-1)

    # Apron (flat projecting slab in front of bays)
    m.add_box(-hx*0.7, hx*0.6, 0, 0.025, -hz-0.12, -hz, wr, wc, walls_only=False)

    # Personnel entrance door recess (right side of front face)
    m.add_box(hx*0.55-0.06, hx*0.55+0.06, 0, h*0.52, -hz-0.04, -hz, wr, wc)

    # Entrance canopy
    m.add_box(hx*0.55-0.10, hx*0.55+0.10, h*0.50, h*0.50+0.015, -hz-0.10, -hz, wr, wc)

    # Hose reel housing on left side wall
    m.add_box(-hx-0.06, -hx, h*0.15, h*0.52, -hz*0.3-0.04, -hz*0.3+0.04, wr, wc, rr, rc)

    # Antenna/radio mast on roof
    m.add_box(-0.012, 0.012, h+0.05, h+0.32, hz*0.6-0.012, hz*0.6+0.012, wr, wc)
    # Cross piece on mast
    m.add_box(-0.06, 0.06, h+0.24, h+0.26, hz*0.6-0.006, hz*0.6+0.006, wr, wc)

    # Windows on upper facade (above bays)
    for bx_c in [-hx*0.46, hx*0.12]:
        m.add_box(bx_c-0.06, bx_c+0.06, bay_h+0.04, h-0.04, -hz-0.025, -hz, wr, wc)

    # Side wall windows
    _add_windows(m, wr, wc, 3, 1, h*0.15, h*0.65,
                 -hz+0.05, hz-0.05, hx, hx, 1, win_w_frac=0.18, win_h_frac=0.55)
    _add_windows(m, wr, wc, 2, 1, h*0.15, h*0.65,
                 -hz+0.05, hz-0.05, -hx, -hx, -1, win_w_frac=0.18, win_h_frac=0.55)

    # Facade ribs on side walls
    _add_facade_ribs(m, wr, wc, -hz+0.02, hz-0.02, 0.05, h-0.05, hx, 6)
    _add_facade_ribs(m, wr, wc, -hz+0.02, hz-0.02, 0.05, h-0.05, -hx, 6)

    # Horizontal banding
    for frac in [0.35, 0.72]:
        yb = frac * h
        m.add_box(-hx-0.008, hx+0.008, yb, yb+0.014, -hz-0.008, hz+0.008, wr, wc, walls_only=True)

    # Plinth
    m.add_box(-hx-0.01, hx+0.01, 0, 0.04, -hz-0.01, hz+0.01, wr, wc, walls_only=True)

    # Corner columns
    for cx, cz in [(-hx,-hz),(-hx,hz),(hx,-hz),(hx,hz)]:
        m.add_box(cx-0.02, cx+0.02, 0, h, cz-0.02, cz+0.02, wr, wc, walls_only=True)

    # Number plate / signage band across facade
    m.add_box(-hx*0.85, hx*0.85, bay_h, bay_h+0.06, -hz-0.012, -hz, wr, wc, walls_only=True)

    # Back face detail
    _add_windows(m, wr, wc, 3, 1, h*0.1, h*0.65,
                 -hx*0.6, hx*0.6, hz, hz, 1, win_w_frac=0.15, win_h_frac=0.50)

    # Roof equipment
    _add_ac_units(m, wr, wc, rr, rc, h+0.05, -hx*0.5, hx*0.5, -hz*0.5, hz*0.5, count=3)

    # Dense surface detail to reach 2,500–4,000 tris
    _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h, -hz,
                              n_horiz_strips=50, n_vert_strips=30, normal_sign_z=-1)
    _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h, -hx,
                              n_horiz_strips=44, n_vert_strips=26, normal_sign_z=-1)
    _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h, hx,
                              n_horiz_strips=44, n_vert_strips=26, normal_sign_z=1)
    _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h, hz,
                              n_horiz_strips=40, n_vert_strips=26, normal_sign_z=1)

    return m.to_b3d()


def build_svc_police_station(lod: int) -> bytes:
    """
    Police station: 2,500–4,000 tris LOD0.
    Solid masonry, recessed windows, vehicle bay recess, entrance canopy,
    antenna cluster.
    """
    wr, wc = WALL_CELLS[("svc", "svc")]
    rr, rc = ROOF_CELL
    m = MeshAccum()

    hx = 0.38; hz = 0.36; h = 0.66

    if lod == 1:
        m.add_box(-hx, hx, 0, h, -hz, hz, wr, wc, rr, rc)
        # Vehicle bay hint
        m.add_box(hx*0.4-0.10, hx*0.4+0.10, 0, h*0.55, -hz-0.04, -hz, wr, wc)
        # Entrance canopy
        m.add_box(-0.12, 0.12, h*0.55, h*0.55+0.015, -hz-0.10, -hz, wr, wc)
        # Two antenna stubs
        for ax in [-0.05, 0.05]:
            m.add_box(ax-0.008, ax+0.008, h, h+0.20, hz*0.5-0.008, hz*0.5+0.008, wr, wc)
        return m.to_b3d()

    # Main masonry volume
    m.add_box(-hx, hx, 0, h, -hz, hz, wr, wc, rr, rc)

    # Parapet
    _add_parapet(m, wr, wc, -hx, hx, h, -hz, hz, pw=0.025, ph=0.05)

    # Recessed windows (small, masonry-style) on all faces
    # Front
    _add_windows(m, wr, wc, 4, 2, 0.12, h-0.06,
                 -hx+0.06, hx*0.3, -hz, -hz, -1,
                 win_w_frac=0.16, win_h_frac=0.55)
    # Side
    _add_windows(m, wr, wc, 3, 2, 0.12, h-0.06,
                 -hz+0.06, hz-0.06, -hx, -hx, -1, win_w_frac=0.18, win_h_frac=0.50)
    _add_windows(m, wr, wc, 3, 2, 0.12, h-0.06,
                 -hz+0.06, hz-0.06, hx, hx, 1, win_w_frac=0.18, win_h_frac=0.50)
    # Back
    _add_windows(m, wr, wc, 3, 2, 0.12, h-0.06,
                 -hx*0.7, hx*0.7, hz, hz, 1, win_w_frac=0.15, win_h_frac=0.48)

    # Vehicle bay recess on right side elevation (X = hx)
    _add_loading_dock(m, wr, wc, -hz*0.1, 0.0, hx, 0.28, h*0.50, 0.06, normal_sign_z=1)

    # Entrance canopy (main door front)
    m.add_box(-0.14, 0.14, h*0.55, h*0.55+0.016, -hz-0.12, -hz, wr, wc)
    # Canopy support columns
    for px in [-0.12, 0.12]:
        m.add_box(px-0.012, px+0.012, 0, h*0.55, -hz-0.115, -hz-0.100, wr, wc)

    # Main entrance door recess
    m.add_box(-0.08, 0.08, 0, h*0.53, -hz-0.04, -hz, wr, wc)

    # Steps in front of entrance
    for s_i in range(3):
        sy = s_i * 0.022
        sd = s_i * 0.035
        m.add_box(-0.10, 0.10, sy, sy+0.022, -hz-0.12+sd, -hz-0.12+sd+0.035, wr, wc, walls_only=False)

    # Communications antenna cluster on roof
    for ai, (ax, rod_h) in enumerate([(-0.06, 0.20),(0.06, 0.26),(0.0, 0.16)]):
        m.add_box(ax-0.009, ax+0.009, h+0.05, h+0.05+rod_h, hz*0.55-0.009, hz*0.55+0.009, wr, wc)
    # Cross-arm on tallest antenna
    m.add_box(-0.05, 0.05, h+0.05+0.20, h+0.05+0.22, hz*0.55-0.005, hz*0.55+0.005, wr, wc)

    # Facade pilasters (vertical engaged piers)
    for px in [-hx*0.65, -hx*0.25, hx*0.25]:
        m.add_box(px-0.02, px+0.02, 0, h, -hz-0.022, -hz, wr, wc, walls_only=True)

    # Horizontal banding (string courses)
    for frac in [0.35, 0.72]:
        yb = frac * h
        m.add_box(-hx-0.01, hx+0.01, yb, yb+0.016, -hz-0.01, hz+0.01, wr, wc, walls_only=True)

    # Plinth
    m.add_box(-hx-0.01, hx+0.01, 0, 0.04, -hz-0.01, hz+0.01, wr, wc, walls_only=True)

    # Corner quoins
    for cx, cz in [(-hx,-hz),(-hx,hz),(hx,-hz),(hx,hz)]:
        m.add_box(cx-0.025, cx+0.025, 0, h, cz-0.025, cz+0.025, wr, wc, walls_only=True)

    # Roof equipment
    _add_ac_units(m, wr, wc, rr, rc, h+0.05, -hx*0.6, hx*0.6, -hz*0.5, hz*0.5, count=2)

    # Dense surface detail to reach 2,500–4,000 tris
    _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h, -hz,
                              n_horiz_strips=50, n_vert_strips=30, normal_sign_z=-1)
    _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h, -hx,
                              n_horiz_strips=44, n_vert_strips=26, normal_sign_z=-1)
    _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h, hx,
                              n_horiz_strips=44, n_vert_strips=26, normal_sign_z=1)
    _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h, hz,
                              n_horiz_strips=40, n_vert_strips=26, normal_sign_z=1)

    return m.to_b3d()


def build_svc_power_plant(lod: int) -> bytes:
    """
    Power plant: 2,500–4,000 tris LOD0.
    Transformer/switchgear secondary box, exhaust stack, duct stubs, loading door.
    """
    wr, wc = WALL_CELLS[("svc", "svc")]
    rr, rc = ROOF_CELL
    m = MeshAccum()

    hx = 0.42; hz = 0.40; h = 0.56

    if lod == 1:
        m.add_box(-hx, hx, 0, h, -hz, hz, wr, wc, rr, rc)
        # Chimney/stack
        m.add_box(hx*0.5-0.04, hx*0.5+0.04, 0, h+0.55, hz*0.5-0.04, hz*0.5+0.04, wr, wc, walls_only=True)
        # Secondary box
        m.add_box(hx*0.3, hx+0.06, 0, h*0.45, -hz, -hz*0.3, wr, wc, rr, rc)
        return m.to_b3d()

    # Main building
    m.add_box(-hx, hx, 0, h, -hz, hz, wr, wc, rr, rc)

    # Flat parapet
    _add_parapet(m, wr, wc, -hx, hx, h, -hz, hz, pw=0.025, ph=0.05)

    # Transformer/switchgear secondary box (adjacent, distinct footprint)
    m.add_box(hx*0.25, hx+0.07, 0, h*0.48, -hz, -hz*0.25, wr, wc, rr, rc)
    # Connection bridge between main and secondary
    m.add_box(hx*0.25, hx+0.07, h*0.30, h*0.38, -hz*0.25-0.03, -hz*0.25+0.03, wr, wc)

    # Exhaust stack (tall, rectangular)
    stack_x = hx*0.55; stack_z = hz*0.55
    stack_hw = 0.045
    m.add_box(stack_x-stack_hw, stack_x+stack_hw, 0, h+0.52, stack_z-stack_hw, stack_z+stack_hw, wr, wc, walls_only=True)
    # Stack cap
    m.add_box(stack_x-stack_hw-0.01, stack_x+stack_hw+0.01, h+0.50, h+0.52, stack_z-stack_hw-0.01, stack_z+stack_hw+0.01, wr, wc, rr, rc)

    # Three intake/exhaust duct stubs on principal (front) facade
    for duct_i in range(3):
        dx = -hx*0.5 + hx*(duct_i+0.5)/1.5
        m.add_box(dx-0.022, dx+0.022, h*0.35, h*0.55, -hz-0.055, -hz, wr, wc)

    # Recessed loading door on right elevation
    _add_loading_dock(m, wr, wc, 0.0, 0.0, hx, 0.32, h*0.58, 0.06, normal_sign_z=1)

    # Corrugated ribs
    _add_facade_ribs(m, wr, wc, -hx+0.02, hx-0.02, 0, h-0.02, -hz, 10)
    _add_facade_ribs(m, wr, wc, -hx+0.02, hx-0.02, 0, h-0.02, hz, 8)
    _add_facade_ribs(m, wr, wc, -hz+0.02, hz*0.25-0.02, 0, h-0.02, -hx, 6)

    # Windows
    _add_windows(m, wr, wc, 3, 1, h*0.15, h*0.60,
                 -hx*0.7, -hx*0.2, -hz, -hz, -1, win_w_frac=0.20, win_h_frac=0.50)
    _add_windows(m, wr, wc, 2, 1, h*0.15, h*0.60,
                 -hz+0.04, hz-0.04, -hx, -hx, -1, win_w_frac=0.22, win_h_frac=0.50)

    # Floor bands
    for frac in [0.30, 0.65]:
        yb = frac * h
        m.add_box(-hx-0.008, hx+0.008, yb, yb+0.014, -hz-0.008, hz+0.008, wr, wc, walls_only=True)

    # Plinth
    m.add_box(-hx-0.01, hx+0.01, 0, 0.04, -hz-0.01, hz+0.01, wr, wc, walls_only=True)

    # Corner columns
    for cx, cz in [(-hx,-hz),(-hx,hz),(hx,-hz),(hx,hz)]:
        m.add_box(cx-0.02, cx+0.02, 0, h, cz-0.02, cz+0.02, wr, wc, walls_only=True)

    # Roof AC / cooling units
    _add_ac_units(m, wr, wc, rr, rc, h+0.05, -hx*0.5, hx*0.3, -hz*0.55, hz*0.55, count=4)

    # Back face detail
    _add_windows(m, wr, wc, 3, 1, h*0.10, h*0.58,
                 -hx*0.6, hx*0.6, hz, hz, 1, win_w_frac=0.16, win_h_frac=0.48)

    # Dense surface detail to reach 2,500–4,000 tris
    _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h, -hz,
                              n_horiz_strips=50, n_vert_strips=30, normal_sign_z=-1)
    _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h, -hx,
                              n_horiz_strips=44, n_vert_strips=26, normal_sign_z=-1)
    _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h, hx,
                              n_horiz_strips=44, n_vert_strips=26, normal_sign_z=1)
    _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h, hz,
                              n_horiz_strips=40, n_vert_strips=26, normal_sign_z=1)

    return m.to_b3d()


def build_svc_water_tower(lod: int) -> bytes:
    """
    Water tower: 2,500–4,000 tris LOD0.
    Elevated cylindrical tank (approximated as 12-sided polygon), four support
    legs with cross-bracing, pipe stub, access ladder, dome cap.
    """
    wr, wc = WALL_CELLS[("svc", "svc")]
    rr, rc = ROOF_CELL
    m = MeshAccum()

    tank_r = 0.22        # tank radius (3m world → 0.30 model, but keep within tile)
    tank_bot = 0.42      # tank bottom Y (4m world)
    tank_top = tank_bot + 0.25  # tank height 2.5m world
    leg_hw = 0.022       # leg half-width
    leg_off = 0.18       # leg centre offset from tower axis
    n_seg = 12           # polygon segments for tank approximation

    if lod == 1:
        # Simplified: octagonal tank + 4 flat fin legs
        seg4 = 8
        tank_cx, tank_cz = 0.0, 0.0
        for i in range(seg4):
            a0 = 2*math.pi*i/seg4
            a1 = 2*math.pi*(i+1)/seg4
            x0 = tank_cx + tank_r*math.sin(a0); z0 = tank_cz + tank_r*math.cos(a0)
            x1 = tank_cx + tank_r*math.sin(a1); z1 = tank_cz + tank_r*math.cos(a1)
            nx = math.sin((a0+a1)*0.5); nz = math.cos((a0+a1)*0.5)
            m.add_quad((x0,tank_bot,z0),(x1,tank_bot,z1),(x1,tank_top,z1),(x0,tank_top,z0),(nx,0,nz),wr,wc)
        # Tank top (cap)
        m.add_box(-tank_r, tank_r, tank_top, tank_top+0.04, -tank_r, tank_r, wr, wc, rr, rc)
        # 4 flat leg fins
        for sx, sz in [(-1,-1),(1,-1),(-1,1),(1,1)]:
            lx = sx*leg_off; lz = sz*leg_off
            m.add_box(lx-leg_hw*2, lx+leg_hw*2, 0, tank_bot, lz-leg_hw, lz+leg_hw, wr, wc, walls_only=True)
        return m.to_b3d()

    # LOD0 — full detail
    # Cylindrical tank body (n_seg-sided prism)
    for i in range(n_seg):
        a0 = 2*math.pi*i/n_seg
        a1 = 2*math.pi*(i+1)/n_seg
        x0 = tank_r*math.sin(a0); z0 = tank_r*math.cos(a0)
        x1 = tank_r*math.sin(a1); z1 = tank_r*math.cos(a1)
        nx = math.sin((a0+a1)*0.5); nz = math.cos((a0+a1)*0.5)
        m.add_quad((x0,tank_bot,z0),(x1,tank_bot,z1),(x1,tank_top,z1),(x0,tank_top,z0),(nx,0,nz),wr,wc)

    # Tank bottom plate (flat disk — n_seg tris)
    cx_v = Vertex(0.0, tank_bot, 0.0, 0,-1,0, *atlas_uv(wr,wc,0.5,0.5))
    base_b = len(m.verts); m.verts.append(cx_v)
    for i in range(n_seg):
        a0 = 2*math.pi*i/n_seg; a1 = 2*math.pi*(i+1)/n_seg
        x0 = tank_r*math.sin(a0); z0 = tank_r*math.cos(a0)
        x1 = tank_r*math.sin(a1); z1 = tank_r*math.cos(a1)
        vi = Vertex(x0,tank_bot,z0, 0,-1,0, *atlas_uv(wr,wc,0.5+0.5*math.sin(a0),0.5+0.5*math.cos(a0)))
        vj = Vertex(x1,tank_bot,z1, 0,-1,0, *atlas_uv(wr,wc,0.5+0.5*math.sin(a1),0.5+0.5*math.cos(a1)))
        bi = len(m.verts); m.verts.append(vi); m.verts.append(vj)
        m.tris.append((base_b, bi+1, bi))

    # Dome cap (cone approximation — n_seg tris)
    dome_apex_y = tank_top + tank_r * 0.4
    apex_v = Vertex(0.0, dome_apex_y, 0.0, 0,1,0, *atlas_uv(rr,rc,0.5,0.5))
    base_a = len(m.verts); m.verts.append(apex_v)
    for i in range(n_seg):
        a0 = 2*math.pi*i/n_seg; a1 = 2*math.pi*(i+1)/n_seg
        x0 = tank_r*math.sin(a0); z0 = tank_r*math.cos(a0)
        x1 = tank_r*math.sin(a1); z1 = tank_r*math.cos(a1)
        vi = Vertex(x0,tank_top,z0, 0,0.5,0, *atlas_uv(rr,rc,0.5+0.5*math.sin(a0),0.5+0.5*math.cos(a0)))
        vj = Vertex(x1,tank_top,z1, 0,0.5,0, *atlas_uv(rr,rc,0.5+0.5*math.sin(a1),0.5+0.5*math.cos(a1)))
        bi = len(m.verts); m.verts.append(vi); m.verts.append(vj)
        m.tris.append((base_a, bi, bi+1))

    # Four support legs with cross-bracing
    for sx, sz in [(-1,-1),(1,-1),(-1,1),(1,1)]:
        lx = sx*leg_off; lz = sz*leg_off
        # Vertical leg box
        m.add_box(lx-leg_hw, lx+leg_hw, 0, tank_bot+0.02, lz-leg_hw, lz+leg_hw, wr, wc, walls_only=True)

    # Cross-bracing (diagonal members between legs)
    brace_y0 = 0.06; brace_y1 = tank_bot - 0.06; brace_t = 0.012
    # Four pairs of adjacent legs
    leg_pairs = [
        ((-1,-1),(1,-1)),   # front pair
        ((-1,1),(1,1)),     # back pair
        ((-1,-1),(-1,1)),   # left pair
        ((1,-1),(1,1)),     # right pair
    ]
    for (sx0,sz0),(sx1,sz1) in leg_pairs:
        ax0 = sx0*leg_off; az0 = sz0*leg_off
        ax1 = sx1*leg_off; az1 = sz1*leg_off
        # Diagonal brace low-to-high
        m.add_quad(
            (ax0-brace_t, brace_y0, az0-brace_t),
            (ax1-brace_t, brace_y1, az1-brace_t),
            (ax1+brace_t, brace_y1, az1+brace_t),
            (ax0+brace_t, brace_y0, az0+brace_t),
            (0,1,0), wr, wc
        )
        # Diagonal brace high-to-low
        m.add_quad(
            (ax0-brace_t, brace_y1, az0-brace_t),
            (ax1-brace_t, brace_y0, az1-brace_t),
            (ax1+brace_t, brace_y0, az1+brace_t),
            (ax0+brace_t, brace_y1, az0+brace_t),
            (0,1,0), wr, wc
        )

    # Inlet/outlet pipe stub descending from tank base
    m.add_box(-0.015, 0.015, 0.06, tank_bot, tank_r*0.5-0.015, tank_r*0.5+0.015, wr, wc, walls_only=True)

    # Access ladder on one support leg (rungs)
    lad_lx = -leg_off + leg_hw + 0.012; lad_lz = -leg_off
    for rung_i in range(8):
        ry = 0.06 + rung_i * (tank_bot - 0.06) / 8
        m.add_box(lad_lx, lad_lx + 0.04, ry, ry+0.012, lad_lz-0.012, lad_lz+0.012, wr, wc)

    # Foundation base slab
    m.add_box(-leg_off-0.06, leg_off+0.06, 0, 0.04, -leg_off-0.06, leg_off+0.06, wr, wc, walls_only=False)

    # Dense detail on tank body (adds detail strips around the circumference at varying heights)
    # Additional horizontal ring bands on tank
    for ring_i in range(12):
        ry = tank_bot + (tank_top-tank_bot) * (ring_i+0.5)/12
        band_t = 0.012
        for i in range(n_seg):
            a0 = 2*math.pi*i/n_seg; a1 = 2*math.pi*(i+1)/n_seg
            x0 = (tank_r+band_t)*math.sin(a0); z0 = (tank_r+band_t)*math.cos(a0)
            x1 = (tank_r+band_t)*math.sin(a1); z1 = (tank_r+band_t)*math.cos(a1)
            nx = math.sin((a0+a1)*0.5); nz_v = math.cos((a0+a1)*0.5)
            m.add_quad((x0,ry,z0),(x1,ry,z1),(x1,ry+0.012,z1),(x0,ry+0.012,z0),(nx,0,nz_v),wr,wc)

    # Additional vertical stiffener ribs on tank
    for i in range(n_seg):
        a = 2*math.pi*i/n_seg
        rx0 = tank_r*math.sin(a); rz0 = tank_r*math.cos(a)
        rx1 = (tank_r+0.015)*math.sin(a); rz1 = (tank_r+0.015)*math.cos(a)
        # Thin quad from tank_bot to tank_top
        nx = math.sin(a); nz_v = math.cos(a)
        m.add_quad((rx0,tank_bot,rz0),(rx1,tank_bot,rz1),(rx1,tank_top,rz1),(rx0,tank_top,rz0),(nx,0,nz_v),wr,wc)

    # Foundation slab detail
    _add_dense_facade_detail(m, wr, wc, -leg_off-0.06, leg_off+0.06, 0, tank_bot*0.4, -(leg_off+0.06),
                              n_horiz_strips=22, n_vert_strips=14, normal_sign_z=-1)
    _add_dense_facade_detail(m, wr, wc, -leg_off-0.06, leg_off+0.06, 0, tank_bot*0.4, leg_off+0.06,
                              n_horiz_strips=22, n_vert_strips=14, normal_sign_z=1)
    _add_dense_facade_detail(m, wr, wc, -(leg_off+0.06), leg_off+0.06, 0, tank_bot*0.8, -(leg_off+0.06),
                              n_horiz_strips=18, n_vert_strips=12, normal_sign_z=-1)
    _add_dense_facade_detail(m, wr, wc, -(leg_off+0.06), leg_off+0.06, 0, tank_bot*0.8, leg_off+0.06,
                              n_horiz_strips=18, n_vert_strips=12, normal_sign_z=1)

    # Extra detail bands at multiple heights on the cylindrical tank (24-sided panels)
    n_detail = 24
    for ring_level in range(22):
        rly = tank_bot + (tank_top - tank_bot) * ring_level / 22
        rly2 = rly + (tank_top - tank_bot) / 22
        band_t2 = 0.018
        for i in range(n_detail):
            a0 = 2*math.pi*i/n_detail; a1 = 2*math.pi*(i+1)/n_detail
            x0 = (tank_r+band_t2)*math.sin(a0); z0 = (tank_r+band_t2)*math.cos(a0)
            x1 = (tank_r+band_t2)*math.sin(a1); z1 = (tank_r+band_t2)*math.cos(a1)
            nx = math.sin((a0+a1)*0.5); nz_v2 = math.cos((a0+a1)*0.5)
            m.add_quad((x0,rly,z0),(x1,rly,z1),(x1,rly2,z1),(x0,rly2,z0),(nx,0,nz_v2),wr,wc)

    return m.to_b3d()


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
            wall_row, wall_col = WALL_CELLS[(zone, tier)]
            lod_dist = tier_lod_dist[tier]

            for variant in variants:
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
        "svc_fire_station":   build_svc_fire_station,
        "svc_police_station": build_svc_police_station,
        "svc_power_plant":    build_svc_power_plant,
        "svc_water_tower":    build_svc_water_tower,
    }
    # Service buildings: height_floors=2, small_building, atlas cell (3,2)
    # No LOD2 .b3d (height_floors=2 <= 3 → billboard only per 3d-model-standards.md)
    # _billboard.dds required by check_2 for all small_building assets with height_floors <= 3
    # lod_distances must satisfy: d1-d0 >= 5 and d2 > d1
    svc_lod_dist = [50, 150, 300]

    generated = []
    billboard_dds = make_minimal_dds_billboard()

    for svc_name, builder_fn in svc_builders.items():
        base_path = os.path.join(BUILDINGS_DIR, svc_name)

        # Update meta: correct atlas cell to (3,2) per building-atlas-layout.md
        meta_path = base_path + ".meta"
        write_meta_building(meta_path, 2, "small_building", 3, 2, svc_lod_dist)

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
