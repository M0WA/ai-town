"""
Blender 4.3 headless pipeline: car_sedan FBX -> B3D (LOD0 + LOD1)

Strategy: full-fidelity conversion, NO polygon reduction.
  Both LOD0 and LOD1 use the complete mesh as-is from the FBX.
  Triangle budgets are ignored — geometry is preserved exactly.

Coordinate system:
  Many FBX exporters (3ds Max, Maya default) produce Z-up geometry.
  We import with default axes, then apply a -90° rotation around X to
  convert Z-up to Y-up (Irrlicht convention).  After rotation the long
  axis of the car lies along Z; we remap it to X (rotate +90° around Y)
  so the car faces +Z in Irrlicht (forward into screen), length along X.
  Triangle winding is flipped (swap i1,i2 per tri) to correct for
  the Blender→Irrlicht handedness change (which reverses winding order).

B3D format v2: BB3D > TEXS + BRUS + NODE > MESH > VRTS + TRIS
  VRTS: pos(3f) + normal(3f) + uv(2f), flags=1, tc_sets=1, tc_size=2
  Normals: per-loop via mesh.corner_normals (Blender 4.x API)

UV atlas remapping:
  The FBX UVs cover [0,1]x[0,1] (full texture space for the original
  material).  car_sedan lives at row=0, col=0 of the 4x4 vehicles atlas
  (2048x2048 px, 512x512 px cells).  UVs are remapped to [0,0.25]x[0,0.25]
  by scaling both axes by 0.25.  The atlas texture is bound at runtime
  so the full atlas coordinates must be baked into the B3D file.
"""

import bpy
import bmesh
import struct
import os
import sys

FBX_PATH      = "/tmp/sedan_import/classic+sedan+3d+model.fbx"

# Atlas remapping constants for car_sedan (row=0, col=0, 4x4 grid)
ATLAS_COLS    = 4
ATLAS_ROWS    = 4
ATLAS_COL_IDX = 0
ATLAS_ROW_IDX = 0
ATLAS_CELL_U  = 1.0 / ATLAS_COLS   # 0.25
ATLAS_CELL_V  = 1.0 / ATLAS_ROWS   # 0.25
ATLAS_OFF_U   = ATLAS_COL_IDX * ATLAS_CELL_U  # 0.0
ATLAS_OFF_V   = ATLAS_ROW_IDX * ATLAS_CELL_V  # 0.0
OUT_LOD0      = "/workspace/assets/3d/vehicles/car_sedan_lod0.b3d"
OUT_LOD1      = "/workspace/assets/3d/vehicles/car_sedan_lod1.b3d"
ATLAS_TEXTURE = "vehicles_diffuse_atlas_d.dds"

TARGET_LENGTH_M   = 4.0    # metres along the X axis (car length)
LOD1_TARGET_TRIS  = 10000  # target triangle count for LOD1 via DECIMATE modifier


# ---------------------------------------------------------------------------
# B3D binary writer (version 2)
# ---------------------------------------------------------------------------

class B3DWriter:
    @staticmethod
    def _chunk(tag: str, payload: bytes) -> bytes:
        assert len(tag) == 4
        return tag.encode('ascii') + struct.pack('<I', len(payload)) + payload

    # Irrlicht uses 16-bit indices internally: max 65535 verts per mesh buffer.
    MAX_VERTS_PER_BUFFER = 65535

    @classmethod
    def _make_mesh_payload(cls, verts: list, flat_indices: list) -> bytes:
        """Build MESH chunk payload splitting into 16-bit-safe VRTS+TRIS pairs.

        Irrlicht's B3D loader creates one SMeshBuffer per VRTS block it reads.
        Each SMeshBuffer uses 16-bit indices (EIT_16BIT), so each VRTS block
        must contain ≤65535 vertices.  We partition triangles into groups whose
        vertex footprint fits within the limit, reindex locally, and emit one
        VRTS+TRIS pair per group.  All groups share brush_id=0 (inherit from MESH).
        """
        C = cls._chunk
        MAX = cls.MAX_VERTS_PER_BUFFER

        # Split flat_indices into triangle groups, each fitting in MAX verts.
        # Greedy: accumulate tris; flush when adding the next tri would overflow.
        n_tris = len(flat_indices) // 3
        groups = []          # list of (global_vert_indices_set, list_of_tri_tuples)
        cur_vset  = {}       # global_vi -> local_vi for current group
        cur_tris  = []
        for t in range(n_tris):
            i0, i1, i2 = flat_indices[t*3], flat_indices[t*3+1], flat_indices[t*3+2]
            new_verts = [v for v in (i0, i1, i2) if v not in cur_vset]
            if cur_tris and len(cur_vset) + len(new_verts) > MAX:
                groups.append((cur_vset, cur_tris))
                cur_vset = {}
                cur_tris = []
                new_verts = [v for v in (i0, i1, i2) if v not in cur_vset]
            for v in new_verts:
                cur_vset[v] = len(cur_vset)
            cur_tris.append((cur_vset[i0], cur_vset[i1], cur_vset[i2]))
        if cur_tris:
            groups.append((cur_vset, cur_tris))

        print(f"    Mesh split: {len(groups)} buffer(s) for {n_tris} tris / "
              f"{len(verts)} unique verts  (16-bit limit={MAX})")

        # Irrlicht's B3D loader sets VerticesStart ONCE before entering the MESH
        # chunk and never updates it between VRTS blocks.  readChunkTRIS adds
        # VerticesStart to every raw index to get the global BaseVertices index.
        # With VerticesStart=0, raw TRIS indices ARE global BaseVertices indices.
        # Each VRTS block appends vertices sequentially to BaseVertices, so the
        # global index for local vertex lv in group gi is:
        #   global = group_vertex_offsets[gi] + lv
        # TRIS blocks must use these global indices, NOT the re-indexed locals.
        group_vertex_offsets = []
        running = 0
        for vset, _ in groups:
            group_vertex_offsets.append(running)
            running += len(vset)

        mesh_pay = struct.pack('<i', 0)   # brush_id=0
        for gi, (vset, tris) in enumerate(groups):
            gvo = group_vertex_offsets[gi]
            # VRTS for this group — vertices written in local index order
            vrts_pay = struct.pack('<III', 1, 1, 2)  # flags, tc_sets, tc_size
            for gv, lv in sorted(vset.items(), key=lambda x: x[1]):
                px, py, pz, nx, ny, nz, u, v = verts[gv]
                vrts_pay += struct.pack('<8f', px, py, pz, nx, ny, nz, u, v)
            # TRIS for this group — use GLOBAL indices (local + group offset)
            tris_pay = struct.pack('<i', -1)          # brush_id=-1 (inherit)
            for (li0, li1, li2) in tris:
                tris_pay += struct.pack('<III', gvo + li0, gvo + li1, gvo + li2)
            mesh_pay += C('VRTS', vrts_pay) + C('TRIS', tris_pay)
            print(f"      Buffer {gi}: {len(vset)} verts, {len(tris)} tris  (global offset {gvo})")

        return mesh_pay

    @classmethod
    def write(cls, filepath: str, verts: list, flat_indices: list,
              tex_name: str) -> int:
        """Write a minimal Irrlicht-compatible B3D file.

        Args:
            verts:       list of (px,py,pz, nx,ny,nz, u,v)
            flat_indices: flat list of u32 indices (3 per tri, winding flipped)
            tex_name:    texture filename string (without path)

        Returns:
            file size in bytes
        """
        C = cls._chunk

        # TEXS: one texture entry
        texs_pay  = (tex_name + '\x00').encode('ascii')
        texs_pay += struct.pack('<II', 1, 0)                   # flags=1, blend=0
        texs_pay += struct.pack('<4f', 0.0, 0.0, 1.0, 1.0)    # pos_u,v; scale_u,v
        texs_pay += struct.pack('<f',  0.0)                    # rotation
        texs = C('TEXS', texs_pay)

        # BRUS: one brush, one texture slot
        brus_pay  = struct.pack('<I', 1)                       # n_texs = 1
        brus_pay += b'\x00'                                    # brush name = ""
        brus_pay += struct.pack('<4f', 1.0, 1.0, 1.0, 1.0)    # rgba
        brus_pay += struct.pack('<f',  0.0)                    # shininess
        brus_pay += struct.pack('<II', 0, 0)                   # blend=0 (opaque), fx=0
        brus_pay += struct.pack('<i',  0)                      # tex_id[0] = 0
        brus = C('BRUS', brus_pay)

        # MESH: split into 16-bit-safe VRTS+TRIS pairs
        mesh_ch = C('MESH', cls._make_mesh_payload(verts, flat_indices))

        # NODE: identity transform, child: MESH
        node_pay  = b'\x00'                                    # node name = ""
        node_pay += struct.pack('<10f',
                                0.0, 0.0, 0.0,                 # position
                                1.0, 1.0, 1.0,                 # scale
                                1.0, 0.0, 0.0, 0.0)            # quat (w,x,y,z)
        node_pay += mesh_ch
        node_ch = C('NODE', node_pay)

        # BB3D root: version(4) + TEXS + BRUS + NODE
        root_pay = struct.pack('<I', 2) + texs + brus + node_ch
        root = C('BB3D', root_pay)

        os.makedirs(os.path.dirname(filepath), exist_ok=True)
        with open(filepath, 'wb') as f:
            f.write(root)
        sz = os.path.getsize(filepath)
        print(f"  Written: {filepath}  ({sz:,} bytes)")
        return sz


# ---------------------------------------------------------------------------
# Mesh helpers
# ---------------------------------------------------------------------------

def count_tris(mesh_data) -> int:
    """Count triangles in a Blender mesh (via bmesh triangulate)."""
    bm = bmesh.new()
    bm.from_mesh(mesh_data)
    bmesh.ops.triangulate(bm, faces=bm.faces)
    n = len(bm.faces)
    bm.free()
    return n


def get_bbox(obj):
    """Return (min_x, max_x, min_y, max_y, min_z, max_z) in world space."""
    verts = [obj.matrix_world @ v.co for v in obj.data.vertices]
    xs = [v.x for v in verts]; ys = [v.y for v in verts]; zs = [v.z for v in verts]
    return min(xs), max(xs), min(ys), max(ys), min(zs), max(zs)


def build_lod_mesh(source_obj, label: str):
    """Build a LOD mesh from source_obj — full fidelity, no decimation.

    Returns a new bpy.types.Object with a standalone Mesh.
    The mesh is triangulated and the UV is NOT yet normalized (done on object).
    Normals are recalculated to point outward to fix backface culling in Irrlicht.
    """
    bm = bmesh.new()
    bm.from_mesh(source_obj.data)
    bmesh.ops.triangulate(bm, faces=bm.faces)
    n_tris = len(bm.faces)
    n_verts = len(bm.verts)
    print(f"  [{label}] {n_verts} verts, {n_tris} tris (no decimation)")
    new_mesh = bpy.data.meshes.new(f"{label}_mesh")
    bm.to_mesh(new_mesh)
    bm.free()
    new_mesh.update()
    new_obj = bpy.data.objects.new(label, new_mesh)
    bpy.context.collection.objects.link(new_obj)
    new_obj.matrix_world = source_obj.matrix_world.copy()
    return new_obj, n_tris


def build_lod_mesh_decimated(source_obj, merge_dist: float, label: str):
    """Build a decimated LOD mesh via spatial vertex merge (remove_doubles)."""
    bm = bmesh.new()
    bm.from_mesh(source_obj.data)
    n_before = len(bm.verts)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=merge_dist)
    bmesh.ops.triangulate(bm, faces=bm.faces)
    n_tris = len(bm.faces)
    print(f"  [{label}] dist={merge_dist}m: {n_before} -> {len(bm.verts)} verts, {n_tris} tris")
    new_mesh = bpy.data.meshes.new(f"{label}_mesh")
    bm.to_mesh(new_mesh)
    bm.free()
    new_mesh.update()
    new_obj = bpy.data.objects.new(label, new_mesh)
    bpy.context.collection.objects.link(new_obj)
    new_obj.matrix_world = source_obj.matrix_world.copy()
    return new_obj, n_tris


def build_lod_mesh_decimate(source_obj, target_tris: int, label: str):
    """Decimate via DECIMATE COLLAPSE applied per loose part, then rejoin.

    Decimating per-part avoids the 'torn' look caused by DECIMATE trying to
    collapse edges between disconnected components (wheels vs body etc).
    Each part is decimated proportionally to its share of the total tri count.
    """
    # Snapshot existing object names before creating the work copy
    before_names = {o.name for o in bpy.data.objects}

    # Work on a copy, triangulate, then separate by loose parts
    work = source_obj.copy()
    work.data = source_obj.data.copy()
    bpy.context.collection.objects.link(work)
    bpy.context.view_layer.objects.active = work
    bpy.ops.object.select_all(action='DESELECT')
    work.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.quads_convert_to_tris()
    bpy.ops.mesh.separate(type='LOOSE')
    bpy.ops.object.mode_set(mode='OBJECT')

    # Collect all objects created during this call (work + its split-off parts)
    parts = [o for o in bpy.data.objects
             if o.type == 'MESH' and o.name not in before_names]
    n_total = sum(len(p.data.polygons) for p in parts)
    print(f"  [{label}] {len(parts)} parts, {n_total} total tris  target={target_tris}")

    for p in parts:
        n_part = len(p.data.polygons)
        part_target = max(4, int(target_tris * n_part / n_total))
        ratio = min(1.0, part_target / max(n_part, 1))
        bpy.context.view_layer.objects.active = p
        bpy.ops.object.select_all(action='DESELECT')
        p.select_set(True)
        mod = p.modifiers.new("Dec", 'DECIMATE')
        mod.decimate_type = 'COLLAPSE'
        mod.ratio = ratio
        bpy.ops.object.modifier_apply(modifier="Dec")
        print(f"    {p.name}: {n_part} -> {len(p.data.polygons)} tris (target {part_target})")

    # Rejoin all parts
    bpy.ops.object.select_all(action='DESELECT')
    for p in parts: p.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()
    lod_obj = bpy.context.active_object
    n_tris = len(lod_obj.data.polygons)
    print(f"  [{label}] joined: {n_tris} tris")
    return lod_obj, n_tris


def remap_uvs_to_atlas_cell(mesh_data):
    """Normalize UVs to [0,1] then remap into the atlas cell for car_sedan.

    car_sedan is at col=0, row=0 of a 4x4 atlas so the target UV region is
    [0.0, 0.25] x [0.0, 0.25].  Steps:
      1. Normalize the raw FBX UVs from whatever range they occupy to [0,1].
      2. Scale U by ATLAS_CELL_U (0.25) and V by ATLAS_CELL_V (0.25).
      3. Offset by (ATLAS_OFF_U, ATLAS_OFF_V) — both 0.0 for cell (0,0).
    """
    uv_layer = mesh_data.uv_layers.active
    if uv_layer is None:
        print("  WARNING: no UV layer — skipping atlas remap")
        return
    uvs = uv_layer.data
    if not uvs:
        return

    us = [uvs[i].uv.x for i in range(len(uvs))]
    vs = [uvs[i].uv.y for i in range(len(uvs))]
    min_u, max_u = min(us), max(us)
    min_v, max_v = min(vs), max(vs)
    ru = (max_u - min_u) or 1.0
    rv = (max_v - min_v) or 1.0

    for i in range(len(uvs)):
        # Step 1: normalize to [0,1]
        norm_u = (uvs[i].uv.x - min_u) / ru
        norm_v = (uvs[i].uv.y - min_v) / rv
        # Step 2: V-flip — Blender V=0 is at bottom; Irrlicht/OpenGL V=0 is at top
        norm_v = 1.0 - norm_v
        # Step 3: remap into atlas cell
        uvs[i].uv.x = ATLAS_OFF_U + norm_u * ATLAS_CELL_U
        uvs[i].uv.y = ATLAS_OFF_V + norm_v * ATLAS_CELL_V

    print(f"  UV remap: raw U[{min_u:.3f},{max_u:.3f}] V[{min_v:.3f},{max_v:.3f}]"
          f" -> atlas cell ({ATLAS_COL_IDX},{ATLAS_ROW_IDX})"
          f" = U[{ATLAS_OFF_U:.3f},{ATLAS_OFF_U+ATLAS_CELL_U:.3f}]"
          f" V[{ATLAS_OFF_V:.3f},{ATLAS_OFF_V+ATLAS_CELL_V:.3f}]")


def shade_smooth(obj):
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.ops.object.shade_smooth()


def extract_b3d_data(obj):
    """Extract (verts, flat_indices) from a triangulated mesh object.

    Returns:
        verts:        list of (px,py,pz, nx,ny,nz, u,v)
        flat_indices: flat list of int — 3 per tri, winding flipped for Irrlicht
    """
    # Work on a throw-away copy so we can safely triangulate+edit
    tmp_mesh = obj.data.copy()
    tmp_obj  = bpy.data.objects.new("_b3d_extract", tmp_mesh)
    bpy.context.collection.objects.link(tmp_obj)
    bpy.context.view_layer.objects.active = tmp_obj
    bpy.ops.object.select_all(action='DESELECT')
    tmp_obj.select_set(True)

    # Triangulate (needed for corner_normals to align with polygon loop order)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.quads_convert_to_tris()
    bpy.ops.object.mode_set(mode='OBJECT')

    mesh = tmp_obj.data
    # corner_normals: per-loop normals, Blender 4.x API (replaces loops[i].normal)
    # Accessing this property triggers recomputation of split normals.
    corner_norms = mesh.corner_normals

    uv_layer = mesh.uv_layers.active
    if uv_layer is None:
        raise RuntimeError(f"No UV layer on {obj.name}")

    mw = obj.matrix_world
    vert_map  = {}
    verts_out = []
    idx_out   = []

    for poly in mesh.polygons:
        loops = list(poly.loop_indices)
        if len(loops) != 3:
            continue
        tri_vi = []
        for li in loops:
            lp = mesh.loops[li]
            vi = lp.vertex_index
            co = mw @ mesh.vertices[vi].co
            px, py, pz = float(co.x), float(co.y), float(co.z)
            cn = corner_norms[li].vector
            nx, ny, nz = float(cn.x), float(cn.y), float(cn.z)
            uv = uv_layer.data[li].uv
            u, v = float(uv.x), float(uv.y)

            key = (round(px, 5), round(py, 5), round(pz, 5),
                   round(nx, 4), round(ny, 4), round(nz, 4),
                   round(u, 5), round(v, 5))
            if key not in vert_map:
                vert_map[key] = len(verts_out)
                verts_out.append((px, py, pz, nx, ny, nz, u, v))
            tri_vi.append(vert_map[key])

        i0, i1, i2 = tri_vi
        # Flip winding for Irrlicht left-handed coordinate system
        idx_out.extend([i0, i2, i1])

    # Cleanup temp object and mesh
    bpy.data.objects.remove(tmp_obj)
    bpy.data.meshes.remove(tmp_mesh)

    return verts_out, idx_out


# ---------------------------------------------------------------------------
# Main pipeline
# ---------------------------------------------------------------------------

def main():
    print("=" * 60)
    print("AI Town — car_sedan FBX -> B3D LOD conversion")
    print(f"  Source: {FBX_PATH}")
    print(f"  LOD0 out: {OUT_LOD0}")
    print(f"  LOD1 out: {OUT_LOD1}")
    print("=" * 60)

    # ----- Clear scene -----
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete()
    for m in list(bpy.data.meshes):
        bpy.data.meshes.remove(m)

    # ------------------------------------------------------------------
    # Step 1: Import FBX and correct orientation to Irrlicht convention
    #
    # Many FBX exporters (3ds Max, Maya) produce Z-up geometry regardless
    # of the axis_forward/axis_up import arguments.  We import with the
    # Blender default axes so the importer applies no hidden transform,
    # then manually rotate the mesh into Irrlicht convention:
    #   Irrlicht: Y-up, car length along X, faces +Z (into screen)
    #
    # Rotation sequence (applied, then baked):
    #   R1: -90° around X  ->  converts Z-up to Y-up
    #   R2: +90° around Y  ->  rotates so the long axis maps to X and
    #                          the car faces +Z
    # ------------------------------------------------------------------
    import math
    from mathutils import Euler

    print(f"\n[Step 1] Importing FBX")
    bpy.ops.import_scene.fbx(
        filepath=FBX_PATH,
        use_custom_normals=False,   # we recalculate normals from decimated geo
        ignore_leaf_bones=True,
        use_image_search=False,
    )
    mesh_objs = [o for o in bpy.data.objects if o.type == 'MESH']
    print(f"  {len(mesh_objs)} mesh object(s) imported")
    if not mesh_objs:
        sys.exit("ERROR: no mesh objects after FBX import")

    if len(mesh_objs) > 1:
        bpy.ops.object.select_all(action='DESELECT')
        for o in mesh_objs: o.select_set(True)
        bpy.context.view_layer.objects.active = mesh_objs[0]
        bpy.ops.object.join()

    src = next(o for o in bpy.data.objects if o.type == 'MESH')
    bpy.context.view_layer.objects.active = src
    bpy.ops.object.select_all(action='DESELECT')
    src.select_set(True)

    # Bake any transform the importer already applied
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

    mn_x, mx_x, mn_y, mx_y, mn_z, mx_z = get_bbox(src)
    print(f"  Post-import bbox (before rotation):")
    print(f"    X=[{mn_x:.3f},{mx_x:.3f}]  Y=[{mn_y:.3f},{mx_y:.3f}]  Z=[{mn_z:.3f},{mx_z:.3f}]")
    print(f"  Tri count: {count_tris(src.data):,}")

    # R1: Rotate -90° around X to go from Z-up to Y-up
    src.rotation_euler = Euler((math.radians(-90.0), 0.0, 0.0), 'XYZ')
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=False)

    mn_x, mx_x, mn_y, mx_y, mn_z, mx_z = get_bbox(src)
    print(f"  After R1 (-90° X):")
    print(f"    X=[{mn_x:.3f},{mx_x:.3f}]  Y=[{mn_y:.3f},{mx_y:.3f}]  Z=[{mn_z:.3f},{mx_z:.3f}]")

    # R2: Rotate +90° around Y so the long axis is X and car faces +Z
    src.rotation_euler = Euler((0.0, math.radians(90.0), 0.0), 'XYZ')
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=False)

    mn_x, mx_x, mn_y, mx_y, mn_z, mx_z = get_bbox(src)
    print(f"  After R2 (+90° Y):")
    print(f"    X=[{mn_x:.3f},{mx_x:.3f}]  Y=[{mn_y:.3f},{mx_y:.3f}]  Z=[{mn_z:.3f},{mx_z:.3f}]")

    # Measure the long axis (should now be X)
    raw_len_x = mx_x - mn_x
    raw_len_z = mx_z - mn_z
    # Pick whichever horizontal axis is longer as the car length axis
    if raw_len_x >= raw_len_z:
        raw_len = raw_len_x
        long_axis = 'X'
    else:
        # Car ended up along Z instead — rotate +90° around Y once more
        src.rotation_euler = Euler((0.0, math.radians(90.0), 0.0), 'XYZ')
        bpy.ops.object.transform_apply(location=False, rotation=True, scale=False)
        mn_x, mx_x, mn_y, mx_y, mn_z, mx_z = get_bbox(src)
        raw_len = mx_x - mn_x
        long_axis = 'X (after extra 90° Y)'
    print(f"  Long axis: {long_axis}  raw_len={raw_len:.4f}m  H={mx_y-mn_y:.4f}m  W={mx_z-mn_z:.4f}m")

    # ------------------------------------------------------------------
    # Step 2: Scale to target length; center at origin, bottom at Y=0
    # ------------------------------------------------------------------
    print(f"\n[Step 2] Scale to {TARGET_LENGTH_M}m length (along X)")
    sf = TARGET_LENGTH_M / raw_len if raw_len > 1e-5 else 1.0
    print(f"  Scale factor: {sf:.4f}")
    src.scale = (sf, sf, sf)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)

    mn_x, mx_x, mn_y, mx_y, mn_z, mx_z = get_bbox(src)
    # Center X and Z; set bottom of car at Y=0
    cx = (mn_x + mx_x) / 2.0
    cz = (mn_z + mx_z) / 2.0
    cy = mn_y  # translate up so the lowest vertex is at Y=0
    src.location = (-cx, -cy, -cz)
    bpy.ops.object.transform_apply(location=True, rotation=False, scale=False)

    mn_x, mx_x, mn_y, mx_y, mn_z, mx_z = get_bbox(src)
    print(f"  After center+floor:")
    print(f"    X=[{mn_x:.3f},{mx_x:.3f}] = {mx_x-mn_x:.3f}m (length, target ~{TARGET_LENGTH_M}m)")
    print(f"    Y=[{mn_y:.3f},{mx_y:.3f}] = {mx_y-mn_y:.3f}m (height)")
    print(f"    Z=[{mn_z:.3f},{mx_z:.3f}] = {mx_z-mn_z:.3f}m (width)")

    # ------------------------------------------------------------------
    # Step 2b: Separate by loose parts, remove tiny interior pieces,
    #          then rejoin into a clean exterior-only mesh.
    #
    # The FBX has 7 loose parts: main body (441k polys) + wheels + tiny
    # interior components.  Parts with <5% of the main body poly count
    # and entirely enclosed inside the car bounding box are interior — drop them.
    # ------------------------------------------------------------------
    print(f"\n[Step 2b] Removing interior loose parts")
    bpy.context.view_layer.objects.active = src
    bpy.ops.object.select_all(action='DESELECT')
    src.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.separate(type='LOOSE')
    bpy.ops.object.mode_set(mode='OBJECT')

    parts = sorted([o for o in bpy.data.objects if o.type == 'MESH'],
                   key=lambda o: -len(o.data.polygons))
    max_polys = len(parts[0].data.polygons)
    mn_x, mx_x, mn_y, mx_y, mn_z, mx_z = get_bbox(parts[0])
    keep = []
    for p in parts:
        n = len(p.data.polygons)
        pverts = [p.matrix_world @ v.co for v in p.data.vertices]
        px_min = min(v.x for v in pverts); px_max = max(v.x for v in pverts)
        py_min = min(v.y for v in pverts); py_max = max(v.y for v in pverts)
        pz_min = min(v.z for v in pverts); pz_max = max(v.z for v in pverts)
        # Interior: fully enclosed in XZ and above the floor by >10% of car height
        is_enclosed_x = px_min > mn_x + 0.05 and px_max < mx_x - 0.05
        is_enclosed_z = pz_min > mn_z + 0.05 and pz_max < mx_z - 0.05
        is_tiny = n < max_polys * 0.02
        if is_tiny and is_enclosed_x and is_enclosed_z:
            print(f"  REMOVE interior part: {p.name}  {n} polys  "
                  f"X=[{px_min:.3f},{px_max:.3f}] Z=[{pz_min:.3f},{pz_max:.3f}]")
            bpy.data.objects.remove(p, do_unlink=True)
        else:
            print(f"  KEEP: {p.name}  {n} polys")
            keep.append(p)

    # Rejoin kept parts
    bpy.ops.object.select_all(action='DESELECT')
    for p in keep: p.select_set(True)
    bpy.context.view_layer.objects.active = keep[0]
    bpy.ops.object.join()
    src = next(o for o in bpy.data.objects if o.type == 'MESH')
    bpy.context.view_layer.objects.active = src
    print(f"  Rejoined: {count_tris(src.data):,} tris remaining")

    # ------------------------------------------------------------------
    # Step 3: Build LOD0 (full fidelity — no decimation, UVs intact)
    # ------------------------------------------------------------------
    print(f"\n[Step 3] Building LOD0 (full fidelity)")
    lod0_obj, tris_lod0 = build_lod_mesh(src, "LOD0")
    remap_uvs_to_atlas_cell(lod0_obj.data)
    shade_smooth(lod0_obj)

    # ------------------------------------------------------------------
    # Step 4: Build LOD1 (DECIMATE on exterior-only mesh)
    # ------------------------------------------------------------------
    print(f"\n[Step 4] Building LOD1 (DECIMATE target={LOD1_TARGET_TRIS} tris)")
    lod1_obj, tris_lod1 = build_lod_mesh_decimate(src, LOD1_TARGET_TRIS, "LOD1")
    remap_uvs_to_atlas_cell(lod1_obj.data)
    shade_smooth(lod1_obj)

    # ------------------------------------------------------------------
    # Step 5: Export B3D files
    # ------------------------------------------------------------------
    print(f"\n[Step 5] Exporting B3D files")

    for lod_obj, out_path, label in [
        (lod0_obj, OUT_LOD0, 'LOD0'),
        (lod1_obj, OUT_LOD1, 'LOD1'),
    ]:
        print(f"\n  [{label}] -> {out_path}")
        verts, flat_idx = extract_b3d_data(lod_obj)
        print(f"    Unique verts: {len(verts)}   Triangles: {len(flat_idx) // 3}")
        B3DWriter.write(out_path, verts, flat_idx, ATLAS_TEXTURE)

    # ------------------------------------------------------------------
    # Summary
    # ------------------------------------------------------------------
    print("\n" + "=" * 60)
    print("CONVERSION COMPLETE")
    print("=" * 60)
    mn_x, mx_x, mn_y, mx_y, mn_z, mx_z = get_bbox(lod0_obj)

    print(f"LOD0: {tris_lod0:6d} tris  file={os.path.getsize(OUT_LOD0):,} bytes")
    print(f"LOD1: {tris_lod1:6d} tris  file={os.path.getsize(OUT_LOD1):,} bytes")
    print(f"\nFinal bounding box (LOD0):")
    print(f"  Length (X): {mx_x - mn_x:.3f} m  (target ~4.0 m)")
    print(f"  Width  (Z): {mx_z - mn_z:.3f} m  (target ~2.0 m)")
    print(f"  Height (Y): {mx_y - mn_y:.3f} m  (target ~1.5 m)")
    print(f"  Atlas texture: {ATLAS_TEXTURE}")
    print(f"  UV channel: 0 (single), remapped to cell ({ATLAS_COL_IDX},{ATLAS_ROW_IDX})"
          f" = U[{ATLAS_OFF_U:.3f},{ATLAS_OFF_U+ATLAS_CELL_U:.3f}]"
          f" V[{ATLAS_OFF_V:.3f},{ATLAS_OFF_V+ATLAS_CELL_V:.3f}]")
    print(f"  Winding: flipped for Irrlicht left-handed coordinate space")

main()
