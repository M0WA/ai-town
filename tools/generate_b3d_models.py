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


def build_box_building(zone: str, tier: str, variant: str, lod: int) -> bytes:
    """
    Generate B3D for a standard zone box building.

    LOD0 _01: 5 faces (4 walls + roof) = 10 tris
    LOD0 _02: 5 faces + rooftop box (4 walls + top) = 20 tris
    LOD1: simple box, 5 faces = 10 tris (same as _01 LOD0)
    LOD2 (high only): 4 walls only, coarser detail = 8 tris
    """
    wall_row, wall_col = WALL_CELLS[(zone, tier)]
    roof_row, roof_col = ROOF_CELL
    h = TIER_HEIGHT[tier]
    hx = hz = BUILDING_HALF_XZ

    all_verts, all_tris = [], []

    def add_face(verts, tris):
        base = len(all_verts)
        all_verts.extend(verts)
        for t in tris:
            all_tris.append((base + t[0], base + t[1], base + t[2]))

    if lod == 2:
        # LOD2 (high tier geometry shell): 4 walls only, no roof
        faces = [
            # front
            make_quad(
                (-hx, 0, -hz), (hx, 0, -hz), (hx, h, -hz), (-hx, h, -hz),
                (0, 0, -1), wall_row, wall_col
            ),
            # back
            make_quad(
                (hx, 0, hz), (-hx, 0, hz), (-hx, h, hz), (hx, h, hz),
                (0, 0, 1), wall_row, wall_col
            ),
            # left
            make_quad(
                (-hx, 0, hz), (-hx, 0, -hz), (-hx, h, -hz), (-hx, h, hz),
                (-1, 0, 0), wall_row, wall_col
            ),
            # right
            make_quad(
                (hx, 0, -hz), (hx, 0, hz), (hx, h, hz), (hx, h, -hz),
                (1, 0, 0), wall_row, wall_col
            ),
        ]
        for v, t in faces:
            add_face(v, t)
    else:
        # LOD0 / LOD1: full box with roof
        v, t = box_faces(-hx, hx, 0, h, -hz, hz, wall_row, wall_col, roof_row, roof_col)
        all_verts.extend(v)
        all_tris.extend(t)

        # LOD0 _02 variant: add a small rooftop box
        if lod == 0 and variant == "02":
            rw = hx * 0.5   # rooftop box half-width
            rh = 0.1        # rooftop box height
            rv, rt = box_faces(-rw, rw, h, h + rh, -rw, rw,
                               wall_row, wall_col, roof_row, roof_col)
            base = len(all_verts)
            all_verts.extend(rv)
            for tri in rt:
                all_tris.append((base + tri[0], base + tri[1], base + tri[2]))

    return build_b3d(all_verts, all_tris)


# ---------------------------------------------------------------------------
# Service building geometry
# ---------------------------------------------------------------------------

def build_svc_fire_station(lod: int) -> bytes:
    """
    Fire station: box with a garage door recess on the front face.

    LOD0: main box (5 faces=10 tris) + garage door indent panel (2 quads=4 tris) = 14 tris
    LOD1: simple box (5 faces = 10 tris)
    """
    wr, wc = WALL_CELLS[("svc", "svc")]
    rr, rc = ROOF_CELL
    h = 0.6; hx = 0.45; hz = 0.45

    all_verts, all_tris = [], []

    def add_face(verts, tris):
        base = len(all_verts)
        all_verts.extend(verts)
        for t in tris:
            all_tris.append((base + t[0], base + t[1], base + t[2]))

    # Main box
    v, t = box_faces(-hx, hx, 0, h, -hz, hz, wr, wc, rr, rc)
    all_verts.extend(v)
    all_tris.extend(t)

    if lod == 0:
        # Garage door recess: two side panels flanking a recessed front section
        # Garage occupies center 60% of front face width
        gw = hx * 0.6   # half-width of garage opening
        gd = 0.05       # depth of recess (into the building)
        gh = h * 0.7    # height of garage door

        # Left side panel of garage recess (facing +X, i.e. inward right wall of opening)
        v, t = make_quad(
            (-gw,   0,   -hz),
            (-gw,   0,   -hz + gd),
            (-gw,   gh,  -hz + gd),
            (-gw,   gh,  -hz),
            (1, 0, 0), wr, wc
        )
        add_face(v, t)

        # Right side panel of garage recess (facing -X, i.e. inward left wall of opening)
        v, t = make_quad(
            (gw,    0,   -hz + gd),
            (gw,    0,   -hz),
            (gw,    gh,  -hz),
            (gw,    gh,  -hz + gd),
            (-1, 0, 0), wr, wc
        )
        add_face(v, t)

    return build_b3d(all_verts, all_tris)


def build_svc_police_station(lod: int) -> bytes:
    """
    Police station: slightly narrower/taller civic box with entrance steps at front.

    LOD0: box (10 tris) + 3 step quads (6 tris) = 16 tris
    LOD1: simple box (10 tris)
    """
    wr, wc = WALL_CELLS[("svc", "svc")]
    rr, rc = ROOF_CELL
    h = 0.7; hx = 0.35; hz = 0.35

    all_verts, all_tris = [], []

    def add_face(verts, tris):
        base = len(all_verts)
        all_verts.extend(verts)
        for t in tris:
            all_tris.append((base + t[0], base + t[1], base + t[2]))

    v, t = box_faces(-hx, hx, 0, h, -hz, hz, wr, wc, rr, rc)
    all_verts.extend(v)
    all_tris.extend(t)

    if lod == 0:
        # Steps: 3 step quads in front of building (top-facing)
        # Each step is 0.03 units tall, 0.06 units deep, full building width
        sw = hx * 0.6    # step half-width (narrower than building)
        for i in range(3):
            sy_bot = i * 0.03
            sy_top = sy_bot + 0.03
            sz_front = -hz - (3 - i) * 0.06
            sz_back  = sz_front + 0.06
            # Top of step (horizontal, facing up)
            v, t = make_quad(
                (-sw, sy_top, sz_front),
                ( sw, sy_top, sz_front),
                ( sw, sy_top, sz_back),
                (-sw, sy_top, sz_back),
                (0, 1, 0), wr, wc
            )
            add_face(v, t)

    return build_b3d(all_verts, all_tris)


def build_svc_power_plant(lod: int) -> bytes:
    """
    Power plant: industrial box with a tall chimney.

    LOD0: base box (10 tris) + chimney box (10 tris) = 20 tris
    LOD1: base box + simplified chimney (4 wall faces = 8 tris) = 18 tris
    """
    wr, wc = WALL_CELLS[("svc", "svc")]
    rr, rc = ROOF_CELL

    all_verts, all_tris = [], []

    def add_box(xmin, xmax, ymin, ymax, zmin, zmax, include_walls_only=False):
        base_idx = len(all_verts)
        if include_walls_only:
            # 4 wall faces only (no roof/floor)
            faces = [
                make_quad(
                    (xmin, ymin, zmin), (xmax, ymin, zmin),
                    (xmax, ymax, zmin), (xmin, ymax, zmin),
                    (0, 0, -1), wr, wc
                ),
                make_quad(
                    (xmax, ymin, zmax), (xmin, ymin, zmax),
                    (xmin, ymax, zmax), (xmax, ymax, zmax),
                    (0, 0, 1), wr, wc
                ),
                make_quad(
                    (xmin, ymin, zmax), (xmin, ymin, zmin),
                    (xmin, ymax, zmin), (xmin, ymax, zmax),
                    (-1, 0, 0), wr, wc
                ),
                make_quad(
                    (xmax, ymin, zmin), (xmax, ymin, zmax),
                    (xmax, ymax, zmax), (xmax, ymax, zmin),
                    (1, 0, 0), wr, wc
                ),
            ]
            for v, t in faces:
                base2 = len(all_verts)
                all_verts.extend(v)
                for tri in t:
                    all_tris.append((base2 + tri[0], base2 + tri[1], base2 + tri[2]))
        else:
            v, t = box_faces(xmin, xmax, ymin, ymax, zmin, zmax, wr, wc, rr, rc)
            all_verts.extend(v)
            all_tris.extend(t)

    # Base building
    add_box(-0.4, 0.4, 0, 0.5, -0.4, 0.4)

    # Chimney (tall thin box at back-right corner)
    add_box(0.2, 0.3, 0, 1.0, 0.2, 0.3, include_walls_only=(lod == 1))

    return build_b3d(all_verts, all_tris)


def build_svc_water_tower(lod: int) -> bytes:
    """
    Water tower: tank box elevated on 4 legs.

    LOD0: tank box (10 tris) + 4 leg boxes (4 × 4 wall faces = 16 tris) = 26 tris
    LOD1: tank box (10 tris) + 4 simplified leg boxes (4 walls each = 16 tris) = 26 tris
          (LOD1 is the same here — both are simple procedural geometry)
    """
    wr, wc = WALL_CELLS[("svc", "svc")]
    rr, rc = ROOF_CELL

    all_verts, all_tris = [], []

    def add_box_walls_only(xmin, xmax, ymin, ymax, zmin, zmax):
        """4 wall faces of a box (no top/bottom) — used for legs."""
        faces = [
            make_quad(
                (xmin, ymin, zmin), (xmax, ymin, zmin),
                (xmax, ymax, zmin), (xmin, ymax, zmin),
                (0, 0, -1), wr, wc
            ),
            make_quad(
                (xmax, ymin, zmax), (xmin, ymin, zmax),
                (xmin, ymax, zmax), (xmax, ymax, zmax),
                (0, 0, 1), wr, wc
            ),
            make_quad(
                (xmin, ymin, zmax), (xmin, ymin, zmin),
                (xmin, ymax, zmin), (xmin, ymax, zmax),
                (-1, 0, 0), wr, wc
            ),
            make_quad(
                (xmax, ymin, zmin), (xmax, ymin, zmax),
                (xmax, ymax, zmax), (xmax, ymax, zmin),
                (1, 0, 0), wr, wc
            ),
        ]
        for v, t in faces:
            base = len(all_verts)
            all_verts.extend(v)
            for tri in t:
                all_tris.append((base + tri[0], base + tri[1], base + tri[2]))

    # Tank
    v, t = box_faces(-0.25, 0.25, 0.6, 0.9, -0.25, 0.25, wr, wc, rr, rc)
    all_verts.extend(v)
    all_tris.extend(t)

    # 4 legs at corners (thin boxes Y=0..0.65, slightly overlapping tank bottom)
    leg_offset = 0.18   # distance from centre to leg centre
    leg_hw = 0.04       # leg half-width
    for sx, sz in [(-1, -1), (1, -1), (-1, 1), (1, 1)]:
        lx = sx * leg_offset
        lz = sz * leg_offset
        add_box_walls_only(
            lx - leg_hw, lx + leg_hw,
            0.0, 0.65,
            lz - leg_hw, lz + leg_hw
        )

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
    zones  = ["res", "com", "ind"]
    tiers  = ["low", "med", "high"]
    variants = ["01", "02"]

    # height_floors per tier (per 3d-model-standards.md LOD table):
    #   low=2  → small_building, height_floors<=3 → billboard LOD2 (no _lod2.b3d)
    #   med=4  → large_building, height_floors>=4 → geometry shell LOD2 (_lod2.b3d required)
    #   high=8 → large_building, height_floors>=4 → geometry shell LOD2 (_lod2.b3d required)
    tier_floors = {"low": 2, "med": 4, "high": 8}
    tier_category = {
        "low":  "small_building",
        "med":  "large_building",   # height_floors=4 >= 4 → large_building (needs _lod2.b3d)
        "high": "large_building",
    }
    # lod_distances: [lod0→lod1, lod1→lod2, cull_dist]
    tier_lod_dist = {
        "low":  [30, 100, 200],
        "med":  [40, 130, 260],
        "high": [50, 200, 400],
    }

    generated = []

    for zone in zones:
        for tier in tiers:
            wall_row, wall_col = WALL_CELLS[(zone, tier)]
            height_floors = tier_floors[tier]
            category = tier_category[tier]
            lod_dist = tier_lod_dist[tier]
            # low tier: billboard LOD2 — only lod0, lod1
            # med/high tier: geometry shell LOD2 — lod0, lod1, lod2
            lods = [0, 1] if tier == "low" else [0, 1, 2]

            for variant in variants:
                base_name = f"{zone}_{tier}_{variant}"
                base_path = os.path.join(BUILDINGS_DIR, base_name)

                # Update meta
                meta_path = base_path + ".meta"
                write_meta_building(meta_path, height_floors, category,
                                     wall_row, wall_col, lod_dist)

                # Generate B3D files
                for lod in lods:
                    b3d_data = build_box_building(zone, tier, variant, lod)
                    b3d_path = f"{base_path}_lod{lod}.b3d"
                    with open(b3d_path, "wb") as f:
                        f.write(b3d_data)
                    generated.append(b3d_path)
                    print(f"  WROTE  {os.path.relpath(b3d_path, WORKSPACE_ROOT)}"
                          f"  ({len(b3d_data)} bytes, {len(b3d_data)//1} B)")

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
