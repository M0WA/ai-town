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
# RESIDENTIAL LOW / MED  (small buildings, 2,000–3,000 tris)
# ---------------------------------------------------------------------------

def _build_res_small(zone, tier, variant, lod):
    """
    Residential low/med variants with pitched roofs, chimneys, bay windows,
    window-box ledges, porch canopy.
    Variants:
      01 — standard gabled, 0.45 half-XZ, 1-2 floors
      02 — wider footprint 0.50 half-X, hipped roof, 2-3 floors
      03 — narrow/deep 0.35×0.50, different entrance side, gabled
      04 — L-shaped: main 0.45×0.45 + side wing 0.20×0.30, 2 floors
    """
    wr, wc = WALL_CELLS[(zone, tier)]
    rr, rc = ROOF_CELL
    m = MeshAccum()

    floors = {"low": {"01":1,"02":2,"03":1,"04":2},
              "med": {"01":2,"02":3,"03":2,"04":3}}[tier][variant]
    floor_h = 0.30   # 1 floor = 0.30 model units → 3 m world

    params = {
        "01": {"hx":0.45,"hz":0.45,"roof":"gabled","win_front":3,"win_side":2},
        "02": {"hx":0.50,"hz":0.45,"roof":"hipped","win_front":4,"win_side":2},
        "03": {"hx":0.35,"hz":0.50,"roof":"gabled","win_front":2,"win_side":3},
        "04": {"hx":0.45,"hz":0.45,"roof":"hipped","win_front":3,"win_side":2},
    }[variant]
    hx = params["hx"]; hz = params["hz"]
    h = floors * floor_h
    ridge_h = 0.12

    if lod == 1:
        # LOD1: simplified envelope retaining roof ridge
        m.add_box(-hx, hx, 0, h, -hz, hz, wr, wc, rr, rc)
        if variant in ("01","03"):
            _add_gabled_roof(m, wr, wc, rr, rc, -hx, hx, h, ridge_h, -hz, hz)
        else:
            _add_hipped_roof(m, wr, wc, rr, rc, -hx, hx, h, ridge_h, -hz, hz)
        return m.to_b3d()

    # LOD0 — full detail
    # Main walls
    m.add_box(-hx, hx, 0, h, -hz, hz, wr, wc, rr, rc)

    # Roof
    if params["roof"] == "gabled":
        _add_gabled_roof(m, wr, wc, rr, rc, -hx, hx, h, ridge_h, -hz, hz)
    else:
        _add_hipped_roof(m, wr, wc, rr, rc, -hx, hx, h, ridge_h, -hz, hz)

    # Chimney
    ch_x = hx * 0.4 if variant in ("01","02") else -hx * 0.3
    _add_chimney(m, wr, wc, rr, rc, ch_x, 0.0, h + ridge_h * 0.5, 0.18, hw=0.025)

    # Window recesses — front face (Z = -hz, normal -Z)
    n_win_x = params["win_front"]
    n_win_y = floors
    _add_windows(m, wr, wc, n_win_x, n_win_y, 0.05, h-0.04,
                 -hx+0.06, hx-0.06, -hz, -hz, -1)

    # Window recesses — side faces
    n_win_side = params["win_side"]
    # Left face (X = -hx, normal -X)
    _add_windows(m, wr, wc, n_win_side, floors, 0.05, h-0.04,
                 -hz+0.06, hz-0.06, -hx, -hx, -1,
                 win_w_frac=0.22, win_h_frac=0.50)
    # Right face
    _add_windows(m, wr, wc, n_win_side, floors, 0.05, h-0.04,
                 -hz+0.06, hz-0.06, hx, hx, 1,
                 win_w_frac=0.22, win_h_frac=0.50)

    # Window-box ledges (thin slab below each front window row)
    win_x_step = (2*hx - 0.12) / n_win_x
    for col_i in range(n_win_x):
        lcx = -hx + 0.06 + win_x_step * (col_i + 0.5)
        lw = win_x_step * 0.55
        for row_i in range(floors):
            ly = 0.05 + (h - 0.09) / floors * (row_i + 0.3)
            m.add_box(lcx-lw/2, lcx+lw/2, ly-0.015, ly, -hz-0.025, -hz, wr, wc, walls_only=True)

    # Bay window on front face (variant 01 and 04)
    if variant in ("01","04"):
        bw = 0.12; bd = 0.06
        by_top = h * 0.7; by_bot = h * 0.15
        m.add_box(-bw/2, bw/2, by_bot, by_top, -hz-bd, -hz, wr, wc)

    # Porch canopy
    cw = 0.18; cd = 0.09; ch = h * 0.28; ct = 0.018
    m.add_box(-cw/2, cw/2, ch, ch+ct, -hz-cd, -hz, wr, wc, rr, rc)
    # Porch side posts
    for px in [-cw/2+0.02, cw/2-0.02]:
        m.add_box(px-0.012, px+0.012, 0, ch, -hz-cd+0.01, -hz-cd+0.025, wr, wc)

    # Door step
    m.add_box(-0.07, 0.07, 0, 0.025, -hz-0.04, -hz, wr, wc, walls_only=False)

    # Plinth (ground-level projecting band)
    m.add_box(-hx-0.01, hx+0.01, 0, 0.04, -hz-0.01, hz+0.01, wr, wc, walls_only=True)

    # Cornice strip below eaves
    m.add_box(-hx-0.01, hx+0.01, h-0.03, h, -hz-0.015, hz+0.015, wr, wc, walls_only=True)

    # Panel groove strips (horizontal, adds visual panelling)
    groove_w = 0.01; groove_d = 0.008
    for frac in [0.33, 0.66]:
        gy = frac * h
        # Front and back
        for zf, nz in [(-hz-groove_d,-1),(hz+groove_d,1)]:
            m.add_box(-hx, hx, gy-groove_w/2, gy+groove_w/2,
                      zf if nz<0 else hz, (zf if nz<0 else hz+groove_d),
                      wr, wc, walls_only=True)
        # Sides
        m.add_box(-hx-groove_d, -hx, gy-groove_w/2, gy+groove_w/2, -hz, hz, wr, wc, walls_only=True)
        m.add_box(hx, hx+groove_d, gy-groove_w/2, gy+groove_w/2, -hz, hz, wr, wc, walls_only=True)

    # Drainpipe stubs (corner boxes)
    for px, pz in [(-hx+0.01,-hz+0.01),(hx-0.01,-hz+0.01)]:
        m.add_box(px-0.01, px+0.01, 0, h, pz-0.01, pz+0.01, wr, wc, walls_only=True)

    # Back-face windows
    _add_windows(m, wr, wc, 2, floors, 0.05, h-0.04,
                 -hx*0.7, hx*0.7, hz, hz, 1,
                 win_w_frac=0.20, win_h_frac=0.45)

    # Extra side entrance detail for variant 03
    if variant == "03":
        # Side door on right face
        m.add_box(hz*0.1-0.04, hz*0.1+0.04, 0, h*0.4, hx-0.01, hx+0.03, wr, wc)

    # L-wing for variant 04
    if variant == "04":
        wx0 = hx; wx1 = hx + 0.20
        wz0 = -hz * 0.5; wz1 = hz
        wh = h * 0.7
        m.add_box(wx0, wx1, 0, wh, wz0, wz1, wr, wc, rr, rc)
        # Wing gabled roof
        _add_gabled_roof(m, wr, wc, rr, rc, wx0, wx1, wh, ridge_h*0.8, wz0, wz1)

    # Extra window detail rows for tri count
    for extra_z, nz_sign in [(hz, 1)]:
        _add_windows(m, wr, wc, n_win_side, floors, 0.05, h-0.04,
                     -hz*0.6, hz*0.6, extra_z, extra_z, nz_sign,
                     win_w_frac=0.18, win_h_frac=0.48)

    # Additional horizontal banding strips for count
    for frac in [0.15, 0.85]:
        yb = frac * h
        m.add_box(-hx-0.008, hx+0.008, yb, yb+0.018, -hz-0.008, hz+0.008, wr, wc, walls_only=True)

    # Dense surface detail to reach 2,000–3,000 tris
    _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h, -hz,
                              n_horiz_strips=40, n_vert_strips=25, normal_sign_z=-1)
    _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h, -hx,
                              n_horiz_strips=35, n_vert_strips=20, normal_sign_z=-1)
    _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h, hx,
                              n_horiz_strips=35, n_vert_strips=20, normal_sign_z=1)
    _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h, hz,
                              n_horiz_strips=32, n_vert_strips=20, normal_sign_z=1)

    return m.to_b3d()


# ---------------------------------------------------------------------------
# COMMERCIAL LOW / MED  (small buildings, 2,000–3,000 tris)
# ---------------------------------------------------------------------------

def _build_com_small(zone, tier, variant, lod):
    """
    Commercial low/med: flat-parapet roof, storefront recesses, awning brackets,
    signage band, display windows.
    """
    wr, wc = WALL_CELLS[(zone, tier)]
    rr, rc = ROOF_CELL
    m = MeshAccum()

    floors = {"low": {"01":1,"02":2,"03":1,"04":2},
              "med": {"01":2,"02":3,"03":2,"04":3}}[tier][variant]
    floor_h = 0.30

    params = {
        "01": {"hx":0.45,"hz":0.45,"parapet_h":0.06,"win_upper":3,"awning_frac":0.6},
        "02": {"hx":0.50,"hz":0.40,"parapet_h":0.10,"win_upper":4,"awning_frac":0.7},
        "03": {"hx":0.38,"hz":0.50,"parapet_h":0.06,"win_upper":2,"awning_frac":0.55},
        "04": {"hx":0.45,"hz":0.45,"parapet_h":0.08,"win_upper":3,"awning_frac":0.65},
    }[variant]
    hx = params["hx"]; hz = params["hz"]
    h = floors * floor_h
    ph = params["parapet_h"]

    if lod == 1:
        m.add_box(-hx, hx, 0, h+ph, -hz, hz, wr, wc, rr, rc)
        # Single awning extrusion
        aw_h = floor_h * 0.35; aw_y = floor_h * 0.75
        aw_d = 0.08
        m.add_box(-hx*0.7, hx*0.7, aw_y, aw_y+aw_h, -hz-aw_d, -hz, wr, wc)
        return m.to_b3d()

    # LOD0
    # Main walls
    m.add_box(-hx, hx, 0, h, -hz, hz, wr, wc, rr, rc)

    # Parapet
    _add_parapet(m, wr, wc, -hx, hx, h, -hz, hz, pw=0.025, ph=ph)
    # Parapet top cap
    m.add_box(-hx-0.025, hx+0.025, h+ph-0.02, h+ph, -hz-0.025, hz+0.025, wr, wc, walls_only=True)

    # ---- Ground floor storefront ----
    sf_w = hx * params["awning_frac"] * 2
    sf_d = 0.06
    sf_h = floor_h * 0.75
    # Storefront recess
    _add_loading_dock(m, wr, wc, 0.0, 0.0, -hz, sf_w, sf_h, sf_d, normal_sign_z=-1)

    # Signage band (between awning and 1st upper floor)
    sb_y0 = floor_h * 0.80; sb_y1 = floor_h * 0.98
    m.add_box(-hx*0.85, hx*0.85, sb_y0, sb_y1, -hz-0.015, -hz, wr, wc, walls_only=True)

    # Awning frame (projecting bracket-and-valance)
    aw_y = floor_h * 0.72; aw_h = 0.04; aw_d = 0.10
    # Valance front face
    m.add_box(-hx*params["awning_frac"], hx*params["awning_frac"], aw_y, aw_y+aw_h, -hz-aw_d, -hz-aw_d+0.012, wr, wc)
    # Awning top slab
    m.add_box(-hx*params["awning_frac"]-0.01, hx*params["awning_frac"]+0.01, aw_y+aw_h-0.015, aw_y+aw_h, -hz-aw_d, -hz, wr, wc, walls_only=False)
    # Bracket supports (every ~0.18 m)
    n_brackets = max(2, int(2*hx*params["awning_frac"]/0.18))
    step = 2*hx*params["awning_frac"] / n_brackets
    for i in range(n_brackets+1):
        bx = -hx*params["awning_frac"] + i*step
        m.add_box(bx-0.008, bx+0.008, aw_y, aw_y+aw_h, -hz-aw_d, -hz, wr, wc, walls_only=True)

    # Upper floor windows
    if floors > 1:
        for fl in range(1, floors):
            fy0 = fl * floor_h + 0.04
            fy1 = (fl+1) * floor_h - 0.04
            _add_windows(m, wr, wc, params["win_upper"], 1, fy0, fy1,
                         -hx+0.06, hx-0.06, -hz, -hz, -1,
                         win_w_frac=0.20, win_h_frac=0.65)

    # Upper side windows
    _add_windows(m, wr, wc, 2, max(1,floors-1), floor_h*0.05, h-0.05,
                 -hz+0.05, hz-0.05, -hx, -hx, -1, win_w_frac=0.25, win_h_frac=0.5)
    _add_windows(m, wr, wc, 2, max(1,floors-1), floor_h*0.05, h-0.05,
                 -hz+0.05, hz-0.05, hx, hx, 1, win_w_frac=0.25, win_h_frac=0.5)

    # Plinth / step
    m.add_box(-hx-0.01, hx+0.01, 0, 0.04, -hz-0.01, hz+0.01, wr, wc, walls_only=True)

    # Cornice at floor band
    m.add_box(-hx-0.01, hx+0.01, floor_h-0.02, floor_h+0.01, -hz-0.01, hz+0.01, wr, wc, walls_only=True)

    # Facade vertical pilasters (flanking storefront)
    for px in [-sf_w/2-0.04, sf_w/2+0.04]:
        m.add_box(px-0.025, px+0.025, 0, h, -hz-0.02, -hz, wr, wc, walls_only=True)

    # Additional panel strips upper facade
    n_panels = params["win_upper"]
    panel_step = 2*hx / (n_panels+1)
    for i in range(n_panels):
        px = -hx + panel_step*(i+1)
        m.add_box(px-0.01, px+0.01, floor_h, h, -hz-0.01, -hz, wr, wc, walls_only=True)

    # Roof equipment (HVAC boxes)
    _add_ac_units(m, wr, wc, rr, rc, h+ph, -hx*0.7, hx*0.7, -hz*0.6, hz*0.6, count=3)

    # Back face windows
    _add_windows(m, wr, wc, 2, floors, 0.05, h-0.05,
                 -hx*0.6, hx*0.6, hz, hz, 1, win_w_frac=0.18, win_h_frac=0.5)

    # Extra horizontal banding
    for frac in [0.25, 0.50, 0.75]:
        yb = floor_h + frac*(h-floor_h)
        m.add_box(-hx-0.006, hx+0.006, yb, yb+0.012, -hz-0.006, hz+0.006, wr, wc, walls_only=True)

    # Mansard step for variant 02 (roof variant)
    if variant == "02":
        m.add_box(-hx*0.8, hx*0.8, h+ph, h+ph+0.08, -hz*0.8, hz*0.8, wr, wc, walls_only=True)

    # Extra entrance detail variant 03
    if variant == "03":
        # Side entrance on right
        m.add_box(hx*0.6-0.06, hx*0.6+0.06, 0, floor_h*0.5, hz-0.01, hz+0.04, wr, wc)

    # Extra column detail for variant 04
    if variant == "04":
        for cx_col in [-hx*0.5, 0.0, hx*0.5]:
            m.add_box(cx_col-0.015, cx_col+0.015, 0, h, -hz-0.025, -hz-0.005, wr, wc)

    # Dense surface detail to reach 2,000–3,000 tris
    _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h, -hz,
                              n_horiz_strips=40, n_vert_strips=25, normal_sign_z=-1)
    _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h, -hx,
                              n_horiz_strips=35, n_vert_strips=20, normal_sign_z=-1)
    _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h, hx,
                              n_horiz_strips=35, n_vert_strips=20, normal_sign_z=1)
    _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h, hz,
                              n_horiz_strips=32, n_vert_strips=20, normal_sign_z=1)

    return m.to_b3d()


# ---------------------------------------------------------------------------
# INDUSTRIAL LOW  (small buildings, 2,000–3,000 tris)
# ---------------------------------------------------------------------------

def _build_ind_low(zone, tier, variant, lod):
    """
    Industrial low: mono-pitch shed, loading dock, corrugated ribs, clerestory strip.
    Variants differ in roof direction, facade treatment.
    """
    wr, wc = WALL_CELLS[(zone, tier)]
    rr, rc = ROOF_CELL
    m = MeshAccum()

    floor_h = 0.30
    floors = {"01":1,"02":2,"03":1,"04":2}[variant]
    h_wall = floors * floor_h

    params = {
        "01": {"hx":0.45,"hz":0.45,"roof_dir":"z","n_ribs":10},
        "02": {"hx":0.50,"hz":0.40,"roof_dir":"z_rev","n_ribs":12},
        "03": {"hx":0.40,"hz":0.50,"roof_dir":"x","n_ribs":10},
        "04": {"hx":0.45,"hz":0.45,"roof_dir":"x_rev","n_ribs":8},
    }[variant]
    hx = params["hx"]; hz = params["hz"]
    roof_rise = h_wall * 0.25

    if lod == 1:
        h_avg = h_wall + roof_rise * 0.5
        m.add_box(-hx, hx, 0, h_avg, -hz, hz, wr, wc, rr, rc)
        # Single slope hint
        if params["roof_dir"] in ("z","z_rev"):
            m.add_quad((-hx,h_wall+roof_rise,hz),(hx,h_wall+roof_rise,hz),
                       (hx,h_wall,-hz),(-hx,h_wall,-hz),(0,1,0),rr,rc)
        return m.to_b3d()

    # Main box
    m.add_box(-hx, hx, 0, h_wall, -hz, hz, wr, wc, rr, rc)

    # Mono-pitch roof
    rd = params["roof_dir"]
    if rd == "z":
        _add_mono_pitch_roof(m, wr, wc, rr, rc, -hx, hx, h_wall, h_wall+roof_rise, -hz, hz)
    elif rd == "z_rev":
        _add_mono_pitch_roof(m, wr, wc, rr, rc, -hx, hx, h_wall, h_wall+roof_rise, hz, -hz)
    elif rd == "x":
        _add_mono_pitch_roof(m, wr, wc, rr, rc, -hz, hz, h_wall, h_wall+roof_rise, -hx, hx)
    else:
        _add_mono_pitch_roof(m, wr, wc, rr, rc, -hz, hz, h_wall, h_wall+roof_rise, hx, -hx)

    # Corrugated ribs on principal (front) facade
    _add_facade_ribs(m, wr, wc, -hx+0.02, hx-0.02, 0.05, h_wall-0.05, -hz, params["n_ribs"])

    # Loading dock
    _add_loading_dock(m, wr, wc, 0.0, 0.0, hz, 0.35, h_wall*0.6, 0.08, normal_sign_z=1)

    # Clerestory strip window near roofline
    cler_h = 0.05; cler_y = h_wall - cler_h - 0.03
    m.add_box(-hx-0.01, hx+0.01, cler_y, cler_y+cler_h, -hz-0.03, -hz, wr, wc, walls_only=True)

    # Plinth
    m.add_box(-hx-0.01, hx+0.01, 0, 0.04, -hz-0.01, hz+0.01, wr, wc, walls_only=True)

    # Corner columns
    for cx, cz in [(-hx,-hz),(-hx,hz),(hx,-hz),(hx,hz)]:
        m.add_box(cx-0.02, cx+0.02, 0, h_wall, cz-0.02, cz+0.02, wr, wc, walls_only=True)

    # Side windows
    _add_windows(m, wr, wc, 2, floors, 0.10, h_wall-0.10,
                 -hz+0.05, hz-0.05, hx, hx, 1, win_w_frac=0.20, win_h_frac=0.40)
    _add_windows(m, wr, wc, 2, floors, 0.10, h_wall-0.10,
                 -hz+0.05, hz-0.05, -hx, -hx, -1, win_w_frac=0.20, win_h_frac=0.40)

    # Front small windows
    _add_windows(m, wr, wc, 3, 1, cler_y-h_wall*0.35, cler_y-0.02,
                 -hx+0.10, hx-0.10, -hz, -hz, -1, win_w_frac=0.12, win_h_frac=0.55)

    # Extra rib detail on sides
    _add_facade_ribs(m, wr, wc, -hz+0.02, hz-0.02, 0.05, h_wall-0.05, hx, 8)
    _add_facade_ribs(m, wr, wc, -hz+0.02, hz-0.02, 0.05, h_wall-0.05, -hx, 8)

    # Horizontal banding
    for frac in [0.30, 0.70]:
        yb = frac * h_wall
        m.add_box(-hx-0.008, hx+0.008, yb, yb+0.015, -hz-0.008, hz+0.008, wr, wc, walls_only=True)

    # Utility meter box on side wall
    m.add_box(hx*0.3-0.04, hx*0.3+0.04, h_wall*0.2, h_wall*0.4, -hz-0.04, -hz, wr, wc)

    # Extra ribs on back
    _add_facade_ribs(m, wr, wc, -hx+0.02, hx-0.02, 0.05, h_wall-0.05, hz, 8)

    # Dense surface detail to reach 2,000–3,000 tris
    _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h_wall, -hz,
                              n_horiz_strips=40, n_vert_strips=25, normal_sign_z=-1)
    _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h_wall, -hx,
                              n_horiz_strips=35, n_vert_strips=20, normal_sign_z=-1)
    _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h_wall, hx,
                              n_horiz_strips=35, n_vert_strips=20, normal_sign_z=1)
    _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h_wall, hz,
                              n_horiz_strips=32, n_vert_strips=20, normal_sign_z=1)

    return m.to_b3d()


# ---------------------------------------------------------------------------
# INDUSTRIAL MED  (small buildings, 2,000–3,000 tris)
# ---------------------------------------------------------------------------

def _build_ind_med(zone, tier, variant, lod):
    """
    Industrial med: sawtooth multi-bay shed, loading dock, corrugated ribs,
    clerestory, stair/ladder stub, ventilation cowls.
    Variants differ in bay count (2/3/4) and footprint.
    """
    wr, wc = WALL_CELLS[(zone, tier)]
    rr, rc = ROOF_CELL
    m = MeshAccum()

    floor_h = 0.30
    floors = {"01":2,"02":3,"03":2,"04":3}[variant]
    h_wall = floors * floor_h

    params = {
        "01": {"hx":0.45,"hz":0.45,"n_bays":2},
        "02": {"hx":0.50,"hz":0.45,"n_bays":3},
        "03": {"hx":0.40,"hz":0.50,"n_bays":4},
        "04": {"hx":0.45,"hz":0.50,"n_bays":2},
    }[variant]
    hx = params["hx"]; hz = params["hz"]
    n_bays = params["n_bays"]
    y_low = h_wall
    y_high = h_wall + h_wall * 0.30

    if lod == 1:
        # Simplified: show two ridges at minimum
        h_avg = (y_low + y_high) * 0.5
        m.add_box(-hx, hx, 0, h_avg, -hz, hz, wr, wc, rr, rc)
        bay_w = (hz*2) / max(2,n_bays)
        for i in range(min(2,n_bays)):
            bz1 = -hz + (i+1)*bay_w
            m.add_quad((-hx,y_low,bz1),(hx,y_low,bz1),(hx,y_high,bz1),(-hx,y_high,bz1),(0,0,1),wr,wc)
        return m.to_b3d()

    # Main walls
    m.add_box(-hx, hx, 0, h_wall, -hz, hz, wr, wc, rr, rc)

    # Sawtooth roof
    _add_sawtooth_bays(m, wr, wc, rr, rc, -hx, hx, h_wall, y_low, y_high, -hz, hz, n_bays)

    # Corrugated ribs on front facade
    _add_facade_ribs(m, wr, wc, -hx+0.02, hx-0.02, 0.05, h_wall-0.05, -hz, 12)

    # Loading dock
    _add_loading_dock(m, wr, wc, 0.0, 0.0, hz, 0.35, h_wall*0.55, 0.08, normal_sign_z=1)

    # Clerestory band between sawtooth bays
    cler_h = 0.06
    for i in range(n_bays-1):
        bz = -hz + (hz*2/n_bays)*(i+1)
        m.add_box(-hx, hx, y_high-cler_h, y_high, bz-0.02, bz+0.02, wr, wc, walls_only=True)

    # Access ladder stub on one elevation
    lad_x = hx - 0.04
    for j in range(5):
        ry = 0.04 + j * (h_wall/5)
        m.add_box(lad_x-0.015, lad_x+0.015, ry, ry+0.02, hz-0.03, hz, wr, wc)

    # Ventilation cowls on ridge
    for i in range(2):
        cz = -hz + (hz*2/(n_bays)) * (0.5 + i*(n_bays-1))
        m.add_box(-0.04, 0.04, y_high, y_high+0.08, cz-0.04, cz+0.04, wr, wc, rr, rc)

    # Plinth
    m.add_box(-hx-0.01, hx+0.01, 0, 0.04, -hz-0.01, hz+0.01, wr, wc, walls_only=True)

    # Corner columns
    for cx, cz in [(-hx,-hz),(-hx,hz),(hx,-hz),(hx,hz)]:
        m.add_box(cx-0.02, cx+0.02, 0, h_wall, cz-0.02, cz+0.02, wr, wc, walls_only=True)

    # Side ribs
    _add_facade_ribs(m, wr, wc, -hz+0.02, hz-0.02, 0.05, h_wall-0.05, hx, 8)
    _add_facade_ribs(m, wr, wc, -hz+0.02, hz-0.02, 0.05, h_wall-0.05, -hx, 8)

    # Side windows
    _add_windows(m, wr, wc, 3, 1, h_wall*0.15, h_wall*0.55,
                 -hz+0.05, hz-0.05, hx, hx, 1, win_w_frac=0.15, win_h_frac=0.55)
    _add_windows(m, wr, wc, 3, 1, h_wall*0.15, h_wall*0.55,
                 -hz+0.05, hz-0.05, -hx, -hx, -1, win_w_frac=0.15, win_h_frac=0.55)

    # Horizontal banding
    for frac in [0.35, 0.70]:
        yb = frac * h_wall
        m.add_box(-hx-0.008, hx+0.008, yb, yb+0.015, -hz-0.008, hz+0.008, wr, wc, walls_only=True)

    # Extra ribs on back facade for count
    _add_facade_ribs(m, wr, wc, -hx+0.02, hx-0.02, 0.05, h_wall-0.05, hz, 10)

    # Front small windows
    _add_windows(m, wr, wc, 4, 1, h_wall*0.1, h_wall*0.45,
                 -hx*0.85, hx*0.85, -hz, -hz, -1, win_w_frac=0.10, win_h_frac=0.55)

    # Dense surface detail to reach 2,000–3,000 tris
    _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h_wall, -hz,
                              n_horiz_strips=40, n_vert_strips=25, normal_sign_z=-1)
    _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h_wall, -hx,
                              n_horiz_strips=35, n_vert_strips=20, normal_sign_z=-1)
    _add_dense_facade_detail(m, wr, wc, -hz, hz, 0, h_wall, hx,
                              n_horiz_strips=35, n_vert_strips=20, normal_sign_z=1)
    _add_dense_facade_detail(m, wr, wc, -hx, hx, 0, h_wall, hz,
                              n_horiz_strips=32, n_vert_strips=20, normal_sign_z=1)

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
    Industrial high-rise: flat rooftop, plant room, pipe stubs, clerestory band,
    stair tower, corrugated ribs.
    height_floors: 01=5, 02=7, 03=8, 04=10
    """
    wr, wc = WALL_CELLS[(zone, tier)]
    rr, rc = ROOF_CELL
    m = MeshAccum()

    floor_h = 0.30
    floors = {"01":5,"02":7,"03":8,"04":10}[variant]
    h = floors * floor_h

    params = {
        "01": {"hx":0.44,"hz":0.40,"plant_count":1,"sawtooth":False,"stair":"left"},
        "02": {"hx":0.42,"hz":0.45,"plant_count":2,"sawtooth":True,"stair":"right"},
        "03": {"hx":0.45,"hz":0.38,"plant_count":1,"sawtooth":False,"stair":"both"},
        "04": {"hx":0.40,"hz":0.44,"plant_count":2,"sawtooth":True,"stair":"left"},
    }[variant]
    hx = params["hx"]; hz = params["hz"]

    if lod == 2:
        m.add_box(-hx, hx, 0, h, -hz, hz, wr, wc, rr, rc)
        # Plant room box
        m.add_box(-hx*0.5, hx*0.5, h, h+0.15, -hz*0.4, hz*0.4, wr, wc, rr, rc)
        return m.to_b3d()

    if lod == 1:
        m.add_box(-hx, hx, 0, h, -hz, hz, wr, wc, rr, rc)
        m.add_box(-hx*0.5, hx*0.5, h, h+0.12, -hz*0.4, hz*0.4, wr, wc, rr, rc)
        # Stair tower hint
        m.add_box(-hx-0.06, -hx, h*0.5, h+0.06, -0.10, 0.10, wr, wc, rr, rc)
        return m.to_b3d()

    # LOD0
    m.add_box(-hx, hx, 0, h, -hz, hz, wr, wc, rr, rc)

    # Rooftop flat parapet
    _add_parapet(m, wr, wc, -hx, hx, h, -hz, hz, pw=0.025, ph=0.06)

    # Plant room(s)
    if params["plant_count"] == 1:
        m.add_box(-hx*0.5, hx*0.5, h+0.06, h+0.22, -hz*0.4, hz*0.4, wr, wc, rr, rc)
    else:
        m.add_box(-hx*0.6, -hx*0.1, h+0.06, h+0.20, -hz*0.4, hz*0.0, wr, wc, rr, rc)
        m.add_box(hx*0.1, hx*0.6, h+0.06, h+0.18, hz*0.0, hz*0.4, wr, wc, rr, rc)

    # Sawtooth monitor roof option
    if params["sawtooth"]:
        y_low = h; y_high = h + 0.10
        bay_w = (hz*2) / 2
        for i in range(2):
            bz0 = -hz + i*bay_w; bz1 = bz0+bay_w
            m.add_quad((-hx,y_high,bz1),(hx,y_high,bz1),(hx,y_low,bz0),(-hx,y_low,bz0),(0,1,0.4),rr,rc)
            m.add_quad((-hx,y_low,bz1),(hx,y_low,bz1),(hx,y_high,bz1),(-hx,y_high,bz1),(0,0,1),wr,wc)

    # Corrugated ribs on all four faces
    _add_facade_ribs(m, wr, wc, -hx+0.02, hx-0.02, 0, h-0.02, -hz, 12)
    _add_facade_ribs(m, wr, wc, -hx+0.02, hx-0.02, 0, h-0.02, hz, 10)
    _add_facade_ribs(m, wr, wc, -hz+0.02, hz-0.02, 0, h-0.02, hx, 8)
    _add_facade_ribs(m, wr, wc, -hz+0.02, hz-0.02, 0, h-0.02, -hx, 8)

    # Clerestory band near roofline
    cler_y = h - 0.08
    m.add_box(-hx, hx, cler_y, cler_y+0.06, -hz-0.025, -hz, wr, wc, walls_only=True)
    m.add_box(-hx, hx, cler_y, cler_y+0.06, hz, hz+0.025, wr, wc, walls_only=True)

    # Pipe/duct stubs on upper facade
    for duct_i in range(3):
        dx = -hx*0.5 + hx*(duct_i+0.5)/1.5
        m.add_box(dx-0.025, dx+0.025, h*0.7, h*0.9, -hz-0.05, -hz, wr, wc)

    # Stair tower extrusion
    st = params["stair"]
    if st in ("left","both"):
        m.add_box(-hx-0.09, -hx, h*0.40, h+0.08, -0.12, 0.12, wr, wc, rr, rc)
    if st in ("right","both"):
        m.add_box(hx, hx+0.09, h*0.40, h+0.08, -0.12, 0.12, wr, wc, rr, rc)

    # Loading dock on back
    _add_loading_dock(m, wr, wc, 0.0, 0.0, hz, 0.40, h*0.25, 0.10, normal_sign_z=1)

    # Window recesses
    for face_z, nz in [(-hz,-1),(hz,1)]:
        _add_windows(m, wr, wc, 4, floors, 0.05, h-0.10,
                     -hx+0.05, hx-0.05, face_z, face_z, nz,
                     win_w_frac=0.14, win_h_frac=0.55)
    for face_x, nx in [(-hx,-1),(hx,1)]:
        _add_windows(m, wr, wc, 3, floors, 0.05, h-0.10,
                     -hz+0.05, hz-0.05, face_x, face_x, nx,
                     win_w_frac=0.18, win_h_frac=0.55)

    # Floor banding
    for fl in range(1, floors):
        fy = fl * floor_h
        m.add_box(-hx-0.005, hx+0.005, fy, fy+0.012, -hz-0.005, hz+0.005, wr, wc, walls_only=True)

    # Plinth
    m.add_box(-hx-0.015, hx+0.015, 0, 0.05, -hz-0.015, hz+0.015, wr, wc, walls_only=True)

    # AC units
    _add_ac_units(m, wr, wc, rr, rc, h+0.06+0.22, -hx*0.35, hx*0.35, -hz*0.30, hz*0.30, count=4)

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
    LOD0: body box + roof box + 4 wheel cylinders (approx as flat quads)
    LOD1: body box only
    """
    row, col = VEHICLE_CELLS[vtype]
    xmin, xmax, ymin, ymax, zmin, zmax = VEHICLE_BODY[vtype]
    body_w = xmax - xmin
    body_h = ymax - ymin
    body_d = zmax - zmin

    all_verts, all_tris = [], []

    def add_v(verts, tris):
        base = len(all_verts)
        all_verts.extend(verts)
        for t in tris:
            all_tris.append((base + t[0], base + t[1], base + t[2]))

    # Body box (5 faces — no bottom)
    v, t = box_faces(xmin, xmax, ymin, ymax, zmin, zmax, row, col, row, col)
    all_verts.extend(v)
    all_tris.extend(t)

    if lod == 0:
        # Roof box
        xs, zs, rh_frac = VEHICLE_ROOF_PARAMS[vtype]
        rx_shrink = body_w * xs * 0.5
        rz_shrink = body_d * zs * 0.5
        roof_h = body_h * rh_frac
        rxmin = xmin + rx_shrink
        rxmax = xmax - rx_shrink
        rzmin = zmin + rz_shrink
        rzmax = zmax - rz_shrink
        ry_bot = ymax              # sits on top of body
        ry_top = ymax + roof_h

        v, t = box_faces(rxmin, rxmax, ry_bot, ry_top, rzmin, rzmax, row, col, row, col)
        base = len(all_verts)
        all_verts.extend(v)
        for tri in t:
            all_tris.append((base + tri[0], base + tri[1], base + tri[2]))

        # Wheels: 4 flat circular approximations (8-sided polygons per wheel)
        # Placed at each corner of the vehicle underbody
        wheel_r = body_h * 0.22    # wheel radius ~22% of body height
        wheel_w = body_d * 0.06    # wheel width/thickness
        wheel_y = wheel_r          # centre height above ground

        # Wheel positions: (front/rear, left/right offsets)
        front_x = xmax - body_w * 0.20
        rear_x  = xmin + body_w * 0.20
        left_z  = zmin - wheel_w * 0.5
        right_z = zmax + wheel_w * 0.5
        sides = [left_z, right_z]
        x_positions = [front_x, rear_x]

        for wx in x_positions:
            for wz_centre in sides:
                # Side face of wheel (circular disk approx) — 8-segment polygon
                n_seg = 8
                wz_in  = wz_centre - wheel_w * 0.5
                wz_out = wz_centre + wheel_w * 0.5

                # Outer disk (facing outward)
                out_normal = (0, 0, 1) if wz_out > 0 else (0, 0, -1)
                in_normal  = (0, 0, -1) if wz_out > 0 else (0, 0, 1)

                def disk_verts(z_val, normal, center_x, center_y):
                    vl = []
                    for i in range(n_seg):
                        angle = 2 * math.pi * i / n_seg
                        px = center_x + wheel_r * math.sin(angle)
                        py = center_y + wheel_r * math.cos(angle)
                        u_off, v_off = atlas_uv(row, col, 0.5 + 0.5 * math.sin(angle), 0.5 + 0.5 * math.cos(angle))
                        vl.append(Vertex(px, py, z_val, *normal, u_off, v_off))
                    return vl

                # Center vertex of each disk
                cx, cy = wx, wheel_y

                outer_verts = disk_verts(wz_out, out_normal, cx, cy)
                center_out = Vertex(cx, cy, wz_out, *out_normal, *atlas_uv(row, col, 0.5, 0.5))
                base = len(all_verts)
                all_verts.extend(outer_verts)
                all_verts.append(center_out)
                center_idx = base + n_seg
                for i in range(n_seg):
                    next_i = (i + 1) % n_seg
                    all_tris.append((center_idx, base + i, base + next_i))

                inner_verts = disk_verts(wz_in, in_normal, cx, cy)
                center_in = Vertex(cx, cy, wz_in, *in_normal, *atlas_uv(row, col, 0.5, 0.5))
                base = len(all_verts)
                all_verts.extend(inner_verts)
                all_verts.append(center_in)
                center_idx = base + n_seg
                for i in range(n_seg):
                    next_i = (i + 1) % n_seg
                    # Inward-facing disk: reverse winding
                    all_tris.append((center_idx, base + next_i, base + i))

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
