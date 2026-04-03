"""
Blender 4.3 headless pipeline: commercial-high FBX -> B3D (LOD0 + LOD1 + LOD2)

Tripo3D building assets are imported, scaled to the 30 m × 30 m (3×3 tile) footprint,
a ground plate is added, then decimated to three LOD levels and exported as Irrlicht B3D.
The Tripo3D basecolor JPG is pasted into the buildings_atlas_d.png at the asset's atlas cell.

Usage:
  blender --background --python tools/convert_building_fbx.py -- \\
    <fbx_path> <basecolor_jpg> <asset_name> <atlas_row> <atlas_col>

  fbx_path      : path to the source .fbx file (Tripo3D export)
  basecolor_jpg : path to the matching basecolor JPG
  asset_name    : e.g. com_high_01, com_high_02
  atlas_row     : row in 8×8 building atlas grid (row 2 for com_high)
  atlas_col     : col in 8×8 building atlas grid (col 4/5/6 for com_high_01/02/03)

Coordinate system:
  Tripo3D exports Z-up. Irrlicht is Y-up, Z-forward.
  Apply -90° X rotation to convert Z-up → Y-up.

Building footprint:
  com_high (3×3 tiles) → ±15 m in X and Z, Y ≥ 0.
  Ground plate: 30 m × 30 m quad at Y = 0.

Atlas layout:
  buildings_atlas_d.png — 2048×2048, 8×8 grid, 256×256 px per cell.
  Cell UV: U = [col/8, (col+1)/8], V = [row/8, (row+1)/8].

LOD budgets (Tripo3D source):
  LOD0: up to 50,000 tris (high-fidelity from Tripo3D)
  LOD1: up to  5,000 tris (decimated for mid-range)
  LOD2: up to    500 tris (simplified shell for far distance)
"""

import bpy
import bmesh
import math
import os
import struct
import sys

# ---------------------------------------------------------------------------
# Args
# ---------------------------------------------------------------------------
_argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
if len(_argv) < 5:
    sys.exit(
        "Usage: blender --background --python convert_building_fbx.py -- "
        "<fbx_path> <basecolor_jpg> <asset_name> <atlas_row> <atlas_col>"
    )

FBX_PATH     = _argv[0]
BASECOLOR    = _argv[1]
ASSET_NAME   = _argv[2]
ATLAS_ROW    = int(_argv[3])
ATLAS_COL    = int(_argv[4])

_tools_dir   = os.path.dirname(os.path.abspath(__file__))
_assets_dir  = os.path.join(_tools_dir, "..", "assets")
OUT_LOD0     = os.path.join(_assets_dir, "3d", "buildings", f"{ASSET_NAME}_lod0.b3d")
OUT_LOD1     = os.path.join(_assets_dir, "3d", "buildings", f"{ASSET_NAME}_lod1.b3d")
OUT_LOD2     = os.path.join(_assets_dir, "3d", "buildings", f"{ASSET_NAME}_lod2.b3d")
ATLAS_PNG    = os.path.join(_assets_dir, "textures", "buildings", "buildings_atlas_d.png")

# Atlas constants (2048×2048, 8×8 cells of 256×256 px)
ATLAS_GRID   = 8
ATLAS_SIZE_PX = 2048
CELL_PX      = ATLAS_SIZE_PX // ATLAS_GRID          # 256
CELL_U       = 1.0 / ATLAS_GRID
CELL_V       = 1.0 / ATLAS_GRID
CELL_U_MIN   = ATLAS_COL * CELL_U
CELL_V_MIN   = ATLAS_ROW * CELL_V

ATLAS_TEXTURE = "buildings_atlas_d.dds"

# Footprint: com_high = 3×3 tiles = 30 m × 30 m → ±15 m in X/Z
HALF_EXTENT   = 15.0   # metres

LOD0_TARGET   = 50000
LOD1_TARGET   = 5000
LOD2_TARGET   = 500


# ---------------------------------------------------------------------------
# B3D writer (identical to convert_vehicle_fbx.py)
# ---------------------------------------------------------------------------
class B3DWriter:
    @staticmethod
    def _chunk(tag: str, payload: bytes) -> bytes:
        assert len(tag) == 4
        return tag.encode('ascii') + struct.pack('<I', len(payload)) + payload

    MAX_VERTS_PER_BUFFER = 65535

    @classmethod
    def _make_mesh_payload(cls, verts, flat_indices):
        C = cls._chunk
        MAX = cls.MAX_VERTS_PER_BUFFER
        n_tris = len(flat_indices) // 3
        groups, cur_vset, cur_tris = [], {}, []
        for t in range(n_tris):
            i0, i1, i2 = flat_indices[t*3], flat_indices[t*3+1], flat_indices[t*3+2]
            new_v = [v for v in (i0, i1, i2) if v not in cur_vset]
            if cur_tris and len(cur_vset) + len(new_v) > MAX:
                groups.append((cur_vset, cur_tris)); cur_vset = {}; cur_tris = []
                new_v = [v for v in (i0, i1, i2) if v not in cur_vset]
            for v in new_v: cur_vset[v] = len(cur_vset)
            cur_tris.append((cur_vset[i0], cur_vset[i1], cur_vset[i2]))
        if cur_tris: groups.append((cur_vset, cur_tris))
        print(f"    {len(groups)} buffer(s), {n_tris} tris")
        offsets, running = [], 0
        for vset, _ in groups: offsets.append(running); running += len(vset)
        pay = struct.pack('<i', 0)
        for gi, (vset, tris) in enumerate(groups):
            gvo = offsets[gi]
            vp = struct.pack('<III', 1, 1, 2)
            for gv, _ in sorted(vset.items(), key=lambda x: x[1]):
                px,py,pz,nx,ny,nz,u,v = verts[gv]
                vp += struct.pack('<8f', px,py,pz,nx,ny,nz,u,v)
            tp = struct.pack('<i', -1)
            for li0,li1,li2 in tris: tp += struct.pack('<III', gvo+li0, gvo+li1, gvo+li2)
            pay += C('VRTS', vp) + C('TRIS', tp)
        return pay

    @classmethod
    def write(cls, filepath, verts, flat_indices, tex_name):
        C = cls._chunk
        tp  = (tex_name+'\x00').encode('ascii')
        tp += struct.pack('<II', 1, 0)
        tp += struct.pack('<4f', 0,0,1,1)
        tp += struct.pack('<f', 0)
        texs = C('TEXS', tp)
        bp  = struct.pack('<I', 1) + b'\x00'
        bp += struct.pack('<4f', 1,1,1,1)
        bp += struct.pack('<f', 0)
        bp += struct.pack('<II', 0, 0)
        bp += struct.pack('<i', 0)
        brus = C('BRUS', bp)
        mesh_ch = C('MESH', cls._make_mesh_payload(verts, flat_indices))
        np_ = b'\x00' + struct.pack('<10f', 0,0,0, 1,1,1, 1,0,0,0) + mesh_ch
        root_pay = struct.pack('<I', 2) + texs + brus + C('NODE', np_)
        data = C('BB3D', root_pay)
        os.makedirs(os.path.dirname(filepath), exist_ok=True)
        with open(filepath, 'wb') as f: f.write(data)
        sz = os.path.getsize(filepath)
        print(f"  Written: {filepath}  ({sz:,} bytes)")
        return sz


# ---------------------------------------------------------------------------
# Blender helpers
# ---------------------------------------------------------------------------
def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def import_fbx(path):
    before = set(bpy.context.scene.objects)
    bpy.ops.import_scene.fbx(filepath=path, axis_forward='-Z', axis_up='Y')
    return [o for o in bpy.context.scene.objects if o not in before]


def select_only(obj):
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj


def join_meshes(objects):
    """Join all mesh objects into one, return the resulting object."""
    meshes = [o for o in objects if o.type == 'MESH']
    if not meshes:
        return None
    for o in meshes:
        o.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    bpy.ops.object.join()
    return bpy.context.view_layer.objects.active


def apply_transforms(obj):
    select_only(obj)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)


def get_bounds(obj):
    coords = [obj.matrix_world @ v.co for v in obj.data.vertices]
    xs = [c.x for c in coords]
    ys = [c.y for c in coords]
    zs = [c.z for c in coords]
    return (min(xs),max(xs), min(ys),max(ys), min(zs),max(zs))


def scale_to_footprint(obj):
    """Scale obj so X and Z extents fit within ±HALF_EXTENT, Y ≥ 0."""
    apply_transforms(obj)
    xmin,xmax, ymin,ymax, zmin,zmax = get_bounds(obj)
    w = xmax - xmin
    d = zmax - zmin
    scale_xz = (HALF_EXTENT * 2.0) / max(w, d)
    # Move so base (ymin) sits at Y=0, center X/Z
    cx = (xmin + xmax) / 2
    cz = (zmin + zmax) / 2
    for v in obj.data.vertices:
        v.co.x = (v.co.x - cx) * scale_xz
        v.co.y = (v.co.y - ymin) * scale_xz
        v.co.z = (v.co.z - cz) * scale_xz
    print(f"  Scaled ×{scale_xz:.4f}: footprint {w:.1f}m×{d:.1f}m → {w*scale_xz:.1f}m×{d*scale_xz:.1f}m")
    _,_, _, ymax2, _, _ = get_bounds(obj)
    print(f"  Height: {ymax2:.1f} m")


def add_ground_plate(obj):
    """Add a 30m × 30m ground plate at Y=0, joined into obj."""
    bpy.ops.mesh.primitive_plane_add(size=HALF_EXTENT * 2, location=(0, 0, 0))
    plate = bpy.context.view_layer.objects.active
    # Rotate so plane is in XZ plane at Y=0 (Blender plane is XY by default)
    plate.rotation_euler = (math.radians(90), 0, 0)
    apply_transforms(plate)
    # Join into main mesh
    plate.select_set(True)
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.join()
    return bpy.context.view_layer.objects.active


def decimate(obj, target_tris):
    """Decimate obj to approximately target_tris. Returns new tri count."""
    me = obj.data
    me.calc_loop_triangles()
    current = len(me.loop_triangles)
    if current <= target_tris:
        print(f"  No decimate needed: {current} ≤ {target_tris}")
        return current
    ratio = target_tris / current
    mod = obj.modifiers.new(name="DECIMATE", type='DECIMATE')
    mod.ratio = max(ratio, 0.001)
    bpy.ops.object.modifier_apply(modifier="DECIMATE")
    obj.data.calc_loop_triangles()
    result = len(obj.data.loop_triangles)
    print(f"  Decimated {current} → {result} tris (ratio {ratio:.4f})")
    return result


def smart_unwrap(obj):
    """Smart UV project onto the object."""
    select_only(obj)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=0.01)
    bpy.ops.object.mode_set(mode='OBJECT')


def remap_uvs_to_cell(obj):
    """Remap all UVs from [0,1]×[0,1] into the atlas cell."""
    uv_layer = obj.data.uv_layers.active
    if not uv_layer:
        return
    for loop in obj.data.loops:
        uv = uv_layer.data[loop.index].uv
        # Clamp and remap
        u = max(0.0, min(1.0, uv.x))
        v = max(0.0, min(1.0, 1.0 - uv.y))   # flip V (Blender → OpenGL convention)
        uv.x = CELL_U_MIN + u * CELL_U
        uv.y = CELL_V_MIN + v * CELL_V


def extract_verts_tris(obj):
    """Return (verts, flat_indices) in Irrlicht's left-handed coordinate system."""
    me = obj.data
    me.calc_loop_triangles()
    # Blender 4.x: corner_normals replaces the deprecated calc_normals_split API

    uv_layer = me.uv_layers.active

    vert_key = {}
    verts_out = []
    indices_out = []

    corner_normals = me.corner_normals
    for tri in me.loop_triangles:
        tri_indices = []
        for li in tri.loops:
            loop = me.loops[li]
            v    = me.vertices[loop.vertex_index]
            nx, ny, nz = corner_normals[li].vector

            # Irrlicht left-handed: negate X
            px = -v.co.x
            py =  v.co.y
            pz =  v.co.z
            nx_ = -nx
            ny_ =  ny
            nz_ =  nz

            u = uv_layer.data[li].uv.x if uv_layer else 0.0
            v_ = uv_layer.data[li].uv.y if uv_layer else 0.0

            key = (round(px,5), round(py,5), round(pz,5),
                   round(nx_,4), round(ny_,4), round(nz_,4),
                   round(u,5), round(v_,5))
            if key not in vert_key:
                vert_key[key] = len(verts_out)
                verts_out.append((px,py,pz, nx_,ny_,nz_, u,v_))
            tri_indices.append(vert_key[key])

        # Flip winding (negate X → reverse winding)
        indices_out += [tri_indices[0], tri_indices[2], tri_indices[1]]

    print(f"  Extracted {len(verts_out)} verts, {len(indices_out)//3} tris")
    return verts_out, indices_out


def duplicate_obj(src_obj, name):
    """Duplicate a mesh object (apply modifiers), return new object."""
    new_mesh = src_obj.data.copy()
    new_obj  = src_obj.copy()
    new_obj.data = new_mesh
    new_obj.name = name
    bpy.context.scene.collection.objects.link(new_obj)
    return new_obj


# ---------------------------------------------------------------------------
# Paste basecolor into atlas PNG
# ---------------------------------------------------------------------------
def bake_to_atlas():
    try:
        from PIL import Image
    except ImportError:
        print("  WARNING: Pillow not available — skipping atlas bake")
        return

    atlas = Image.open(ATLAS_PNG).convert("RGB")
    bc    = Image.open(BASECOLOR).convert("RGB")

    # Cell pixel region
    x0 = ATLAS_COL * CELL_PX
    y0 = ATLAS_ROW * CELL_PX
    x1 = x0 + CELL_PX
    y1 = y0 + CELL_PX

    # Resize basecolor to cell size and paste
    bc_resized = bc.resize((CELL_PX, CELL_PX), Image.LANCZOS)
    atlas.paste(bc_resized, (x0, y0, x1, y1))
    atlas.save(ATLAS_PNG)
    print(f"  Baked basecolor into atlas cell ({ATLAS_ROW},{ATLAS_COL}) px [{x0},{y0}]–[{x1},{y1}]")


# ---------------------------------------------------------------------------
# Main pipeline
# ---------------------------------------------------------------------------
def main():
    print(f"\n=== convert_building_fbx: {ASSET_NAME} ===")
    print(f"  FBX:       {FBX_PATH}")
    print(f"  Basecolor: {BASECOLOR}")
    print(f"  Atlas cell: row={ATLAS_ROW} col={ATLAS_COL}")
    print(f"  Cell UV:   U=[{CELL_U_MIN:.4f},{CELL_U_MIN+CELL_U:.4f}] V=[{CELL_V_MIN:.4f},{CELL_V_MIN+CELL_V:.4f}]")

    reset_scene()

    # 1. Import FBX
    print("\n[1] Importing FBX...")
    imported = import_fbx(FBX_PATH)
    print(f"  Imported {len(imported)} objects: {[o.name for o in imported]}")

    # 2. Remove non-mesh objects (cameras, lights, armatures)
    for o in list(imported):
        if o.type != 'MESH':
            bpy.data.objects.remove(o, do_unlink=True)
    meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
    print(f"  {len(meshes)} mesh object(s) after cleanup")

    if not meshes:
        sys.exit("ERROR: No mesh objects found in FBX")

    # 3. Join all meshes
    print("\n[2] Joining meshes...")
    for o in meshes: o.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    if len(meshes) > 1:
        bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    apply_transforms(obj)

    # 4. Scale to fit 30m × 30m footprint, base at Y=0
    print("\n[3] Scaling to footprint...")
    scale_to_footprint(obj)

    # 5. UV unwrap the scaled mesh
    print("\n[4] UV unwrapping...")
    smart_unwrap(obj)
    remap_uvs_to_cell(obj)

    # 6. Bake basecolor into atlas PNG
    print("\n[5] Baking to atlas PNG...")
    bake_to_atlas()

    # 7. LOD0 — full Tripo3D fidelity, no decimation
    print("\n[6] LOD0 (no decimation)...")
    obj_lod0 = duplicate_obj(obj, f"{ASSET_NAME}_lod0")
    select_only(obj_lod0)
    obj_lod0.data.calc_loop_triangles()
    print(f"  LOD0 tris: {len(obj_lod0.data.loop_triangles)}")
    # Add ground plate to LOD0 only
    print("  Adding ground plate...")
    obj_lod0 = add_ground_plate(obj_lod0)
    select_only(obj_lod0)
    smart_unwrap(obj_lod0)
    remap_uvs_to_cell(obj_lod0)
    v0, i0 = extract_verts_tris(obj_lod0)
    B3DWriter.write(OUT_LOD0, v0, i0, ATLAS_TEXTURE)

    # 8. LOD1 — mid-range (up to 5k tris)
    print("\n[7] LOD1...")
    obj_lod1 = duplicate_obj(obj, f"{ASSET_NAME}_lod1")
    select_only(obj_lod1)
    decimate(obj_lod1, LOD1_TARGET)
    remap_uvs_to_cell(obj_lod1)   # re-remap after decimate (topology preserved)
    v1, i1 = extract_verts_tris(obj_lod1)
    B3DWriter.write(OUT_LOD1, v1, i1, ATLAS_TEXTURE)

    # 9. LOD2 — far shell (up to 500 tris)
    print("\n[8] LOD2...")
    obj_lod2 = duplicate_obj(obj, f"{ASSET_NAME}_lod2")
    select_only(obj_lod2)
    decimate(obj_lod2, LOD2_TARGET)
    remap_uvs_to_cell(obj_lod2)
    v2, i2 = extract_verts_tris(obj_lod2)
    B3DWriter.write(OUT_LOD2, v2, i2, ATLAS_TEXTURE)

    print(f"\n=== {ASSET_NAME} done ===")


if __name__ == "__main__":
    main()
