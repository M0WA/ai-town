#!/usr/bin/env python3
"""
Blender headless pipeline: Tripo3D FBX source zip -> PLY (LOD0 + LOD1 [+ LOD2])

Converts a single Tripo3D source asset to Irrlicht-compatible PLY format.  PLY
files loaded by Irrlicht use CDynamicMeshBuffer with 32-bit indices, eliminating
the 65,535 vertex-per-buffer limit that causes crashes with high-poly B3D meshes.

Supports both vehicles and buildings.

Usage:
  blender --background --python tools/convert_tripo3d_to_ply.py -- \\
    --zip <source.zip> --name <asset_name> --type <vehicle|building> \\
    [--atlas-row R] [--atlas-col C] [--footprint N] [--target-length L]

Arguments:
  --zip           Path to the Tripo3D source ZIP (required)
  --name          Output asset base name, e.g. com_high_01 (required)
  --type          Asset type: vehicle or building (required)
  --atlas-row     Row index in the texture atlas (default: 0)
  --atlas-col     Column index in the texture atlas (default: 0)
  --footprint     Tile footprint for buildings, e.g. 1/2/3 (default: 1)
  --target-length Vehicle target length in metres (default: 4.0)

Output files:
  assets/3d/vehicles/<name>_lod0.ply, <name>_lod1.ply
  assets/3d/buildings/<name>_lod0.ply, <name>_lod1.ply [, <name>_lod2.ply]
"""

import bpy
import bmesh
import math
import os
import sys
import tempfile
import zipfile

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
_TOOLS_DIR  = os.path.dirname(os.path.abspath(__file__))
_REPO_ROOT  = os.path.join(_TOOLS_DIR, "..")
_ASSETS_DIR = os.path.join(_REPO_ROOT, "assets")


# ---------------------------------------------------------------------------
# LOD targets (triangle counts)
# ---------------------------------------------------------------------------
VEHICLE_LOD1_TRIS  = 10000
BUILDING_LOD1_TRIS = 5000
BUILDING_LOD2_TRIS = 500

# Atlas constants
VEHICLE_ATLAS_GRID = 4     # 4x4 vehicle atlas
BUILDING_ATLAS_GRID = 8    # 8x8 building atlas

DEFAULT_VEHICLE_LENGTH_M = 4.0
TILE_SIZE_M = 10.0


# ===========================================================================
# Meta file helpers
# ===========================================================================

import json

def read_height_floors(asset_name, asset_type):
    """Read height_floors from <asset_name>.meta in assets/3d/<type>s/.

    Returns the floor count, or 0 if no meta file exists (conservative: no LOD2).
    """
    meta_path = os.path.join(_ASSETS_DIR, "3d", asset_type + "s", f"{asset_name}.meta")
    if not os.path.exists(meta_path):
        return 0
    try:
        with open(meta_path) as f:
            data = json.load(f)
        return int(data.get("height_floors", 0))
    except Exception as e:
        print(f"  WARNING: could not read {meta_path}: {e}")
        return 0


# ===========================================================================
# Blender helpers
# ===========================================================================

def reset_scene():
    """Clear everything."""
    bpy.ops.wm.read_factory_settings(use_empty=True)


def select_only(obj):
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj


def apply_transforms(obj):
    select_only(obj)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)


def get_bounds(obj):
    coords = [obj.matrix_world @ v.co for v in obj.data.vertices]
    xs = [c.x for c in coords]
    ys = [c.y for c in coords]
    zs = [c.z for c in coords]
    return min(xs), max(xs), min(ys), max(ys), min(zs), max(zs)


def tri_count(obj):
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bmesh.ops.triangulate(bm, faces=bm.faces)
    n = len(bm.faces)
    bm.free()
    return n


def import_fbx(path):
    before = set(bpy.context.scene.objects)
    bpy.ops.import_scene.fbx(
        filepath=path,
        use_custom_normals=False,
        ignore_leaf_bones=True,
        use_image_search=False,
    )
    return [o for o in bpy.context.scene.objects if o not in before]


def join_meshes(objects):
    meshes = [o for o in objects if o.type == 'MESH']
    if not meshes:
        return None
    bpy.ops.object.select_all(action='DESELECT')
    for o in meshes:
        o.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    if len(meshes) > 1:
        bpy.ops.object.join()
    return bpy.context.view_layer.objects.active


def remove_non_mesh(objects):
    for o in list(objects):
        if o.type != 'MESH':
            bpy.data.objects.remove(o, do_unlink=True)


# ===========================================================================
# Orientation & scaling
# ===========================================================================

def orient_zup_to_yup(obj):
    """Rotate -90 deg X to convert Tripo3D Z-up to Irrlicht Y-up."""
    obj.rotation_euler = (math.radians(-90), 0, 0)
    apply_transforms(obj)


def ensure_long_axis_z(obj):
    """If the long axis is X, rotate +90 deg Y so length is along Z."""
    xmin, xmax, _, _, zmin, zmax = get_bounds(obj)
    if (xmax - xmin) > (zmax - zmin):
        obj.rotation_euler = (0, math.radians(90), 0)
        apply_transforms(obj)


def flip_nose_forward(obj):
    """Rotate 180 deg Y so vehicle nose faces +Z."""
    obj.rotation_euler = (0, math.radians(180), 0)
    apply_transforms(obj)


def scale_vehicle(obj, target_length):
    """Scale vehicle so length along Z = target_length, centered, base at Y=0."""
    xmin, xmax, ymin, ymax, zmin, zmax = get_bounds(obj)
    raw_len = zmax - zmin
    sf = target_length / raw_len if raw_len > 1e-5 else 1.0
    obj.scale = (sf, sf, sf)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)

    xmin, xmax, ymin, ymax, zmin, zmax = get_bounds(obj)
    cx, cz = (xmin + xmax) / 2, (zmin + zmax) / 2
    obj.location = (-cx, -ymin, -cz)
    apply_transforms(obj)
    print(f"  Scaled vehicle to {target_length}m (factor {sf:.4f})")


def scale_building(obj, footprint_tiles):
    """Scale building to fit NxN tile footprint, base at Y=0."""
    apply_transforms(obj)
    xmin, xmax, ymin, ymax, zmin, zmax = get_bounds(obj)
    w = xmax - xmin
    d = zmax - zmin
    half = footprint_tiles * TILE_SIZE_M / 2.0
    sf = (half * 2.0) / max(w, d)
    cx = (xmin + xmax) / 2
    cz = (zmin + zmax) / 2
    for v in obj.data.vertices:
        v.co.x = (v.co.x - cx) * sf
        v.co.y = (v.co.y - ymin) * sf
        v.co.z = (v.co.z - cz) * sf
    print(f"  Scaled building: {w:.1f}x{d:.1f}m -> {w*sf:.1f}x{d*sf:.1f}m (factor {sf:.4f})")


# ===========================================================================
# Interior removal (vehicles)
# ===========================================================================

def remove_vehicle_interior(obj):
    """Separate by loose parts, remove tiny interior pieces, rejoin."""
    select_only(obj)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.separate(type='LOOSE')
    bpy.ops.object.mode_set(mode='OBJECT')

    parts = sorted([o for o in bpy.data.objects if o.type == 'MESH'],
                   key=lambda o: -len(o.data.polygons))
    if len(parts) <= 1:
        return parts[0] if parts else obj

    max_polys = len(parts[0].data.polygons)
    xmin, xmax, _, _, zmin, zmax = get_bounds(parts[0])
    keep = []
    for p in parts:
        n = len(p.data.polygons)
        pverts = [p.matrix_world @ v.co for v in p.data.vertices]
        px_min = min(v.x for v in pverts)
        px_max = max(v.x for v in pverts)
        pz_min = min(v.z for v in pverts)
        pz_max = max(v.z for v in pverts)
        is_enclosed_x = px_min > xmin + 0.05 and px_max < xmax - 0.05
        is_enclosed_z = pz_min > zmin + 0.05 and pz_max < zmax - 0.05
        is_tiny = n < max_polys * 0.02
        if is_tiny and is_enclosed_x and is_enclosed_z:
            print(f"    REMOVE interior: {p.name} ({n} polys)")
            bpy.data.objects.remove(p, do_unlink=True)
        else:
            keep.append(p)

    bpy.ops.object.select_all(action='DESELECT')
    for p in keep:
        p.select_set(True)
    bpy.context.view_layer.objects.active = keep[0]
    bpy.ops.object.join()
    result = bpy.context.view_layer.objects.active
    print(f"  After interior removal: {tri_count(result)} tris")
    return result


# ===========================================================================
# UV atlas remapping
# ===========================================================================

def rewrap_and_bake_to_atlas(obj, atlas_grid, row, col, basecolor_jpg, atlas_png, cell_px=256):
    """Re-unwrap the mesh with Smart UV Project, bake the original texture onto the
    new UV layout, write the baked cell into the atlas PNG, and update the object's
    active UV map to the new atlas coordinates.

    This replaces the old remap_uvs_to_atlas + bake_basecolor_to_atlas pair.  The
    old approach suffered from visible UV-island seams because it kept the Tripo3D
    UV layout (many tiny isolated islands) and pasted the basecolor image without
    re-baking.  Smart UV Project produces a continuous, low-seam unwrap; baking
    onto it fills every texel correctly.

    Requires Blender's Cycles or EEVEE render engine (for texture baking).
    Falls back to the old paste-and-remap approach when baking is unavailable.
    """
    import os

    cell_u = 1.0 / atlas_grid
    cell_v = 1.0 / atlas_grid
    off_u  = col * cell_u
    off_v  = row * cell_v

    # --- Step 1: create a new UV map "atlas_uv" with Smart UV Project ---
    select_only(obj)
    # Remove any pre-existing atlas_uv layer so we start clean
    existing = [l for l in obj.data.uv_layers if l.name == "atlas_uv"]
    for l in existing:
        obj.data.uv_layers.remove(l)
    atlas_layer = obj.data.uv_layers.new(name="atlas_uv")
    obj.data.uv_layers.active = atlas_layer

    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(
        angle_limit=math.radians(66),
        island_margin=0.02,
        scale_to_bounds=True,
    )
    bpy.ops.object.mode_set(mode='OBJECT')
    print(f"  Smart UV Project done (new UV map 'atlas_uv')")

    # --- Step 2: bake original texture onto the new atlas_uv layout ---
    baked_ok = False
    if basecolor_jpg and os.path.exists(basecolor_jpg):
        try:
            # Set up Cycles bake
            bpy.context.scene.render.engine = 'CYCLES'
            bpy.context.scene.cycles.samples = 1

            # Create a bake target image
            bake_img = bpy.data.images.new(
                name="bake_target", width=cell_px, height=cell_px, alpha=False)
            bake_img.file_format = 'PNG'

            # Assign the original Tripo3D texture as the source material
            mat_name = f"{obj.name}_bake_mat"
            mat = bpy.data.materials.get(mat_name)
            if mat is None:
                mat = bpy.data.materials.new(name=mat_name)
                mat.use_nodes = True
            obj.data.materials.clear()
            obj.data.materials.append(mat)

            tree = mat.node_tree
            tree.nodes.clear()

            # Source texture node (original Tripo3D basecolor)
            src_tex = tree.nodes.new('ShaderNodeTexImage')
            src_tex.image = bpy.data.images.load(basecolor_jpg)
            # The source UV is the original Tripo3D UV — use the first non-atlas_uv layer
            orig_uv_name = next(
                (l.name for l in obj.data.uv_layers if l.name != "atlas_uv"), None)
            if orig_uv_name:
                src_uv_node = tree.nodes.new('ShaderNodeUVMap')
                src_uv_node.uv_map = orig_uv_name
                tree.links.new(src_uv_node.outputs['UV'], src_tex.inputs['Vector'])

            diffuse = tree.nodes.new('ShaderNodeBsdfDiffuse')
            tree.links.new(src_tex.outputs['Color'], diffuse.inputs['Color'])
            out = tree.nodes.new('ShaderNodeOutputMaterial')
            tree.links.new(diffuse.outputs['BSDF'], out.inputs['Surface'])

            # Target texture node — MUST be selected for Cycles bake target
            tgt_tex = tree.nodes.new('ShaderNodeTexImage')
            tgt_tex.image = bake_img
            tgt_uv_node = tree.nodes.new('ShaderNodeUVMap')
            tgt_uv_node.uv_map = "atlas_uv"
            tree.links.new(tgt_uv_node.outputs['UV'], tgt_tex.inputs['Vector'])
            tree.nodes.active = tgt_tex  # must be active for bake target

            # Switch active UV to atlas_uv for the bake destination
            obj.data.uv_layers.active = atlas_layer

            bpy.ops.object.bake(
                type='DIFFUSE',
                pass_filter={'COLOR'},
                use_selected_to_active=False,
                margin=2,
            )

            # Paste baked cell into atlas using bpy.data.images + numpy (no Pillow needed).
            # Blender stores pixel arrays bottom-up (Y=0 at bottom), so we flip the row
            # index when computing the paste position so the PNG on disk stays top-down.
            import numpy as np
            try:
                atlas_img = bpy.data.images.load(atlas_png, check_existing=False)
                atlas_w, atlas_h = atlas_img.size
                atlas_px = np.array(atlas_img.pixels[:]).reshape(atlas_h, atlas_w, 4)

                baked_px = np.array(bake_img.pixels[:]).reshape(cell_px, cell_px, 4)

                cell_x = col * cell_px
                cell_y = (atlas_grid - 1 - row) * cell_px  # flip row: Blender Y=0 at bottom
                atlas_px[cell_y:cell_y + cell_px, cell_x:cell_x + cell_px] = baked_px

                atlas_img.pixels = atlas_px.flatten().tolist()
                atlas_img.filepath_raw = atlas_png
                atlas_img.file_format = 'PNG'
                atlas_img.save()
                bpy.data.images.remove(atlas_img)
                print(f"  Baked texture pasted into atlas cell ({row},{col})")
                baked_ok = True
            except Exception as paste_err:
                print(f"  WARNING: Atlas paste failed ({paste_err})")

            # Cleanup
            bpy.data.images.remove(bake_img)
            bpy.data.images.remove(src_tex.image)

        except Exception as e:
            print(f"  WARNING: Cycles bake failed ({e}) — falling back to paste method")

    if not baked_ok and basecolor_jpg:
        # Fallback: paste basecolor directly (old behaviour)
        bake_basecolor_to_atlas(basecolor_jpg, row, col)

    # --- Step 3: remap atlas_uv [0,1]x[0,1] -> atlas cell, with V-flip ---
    # After Smart UV Project, UVs span approximately [0,1]x[0,1].
    # V-flip because Blender UV V=0 is at bottom but atlas PNG Y=0 is at top.
    # Re-fetch atlas_layer: the original reference can become stale after
    # Blender's edit-mode/object-mode round trips during baking and UV unwrap.
    atlas_layer = obj.data.uv_layers.get("atlas_uv")
    if atlas_layer is None:
        print("  WARNING: atlas_uv layer not found — UV remap skipped")
        return
    obj.data.uv_layers.active = atlas_layer
    uvs = atlas_layer.data
    for i in range(len(uvs)):
        u = uvs[i].uv.x
        v = uvs[i].uv.y
        uvs[i].uv.x = off_u + u * cell_u
        uvs[i].uv.y = off_v + (1.0 - v) * cell_v   # V-flip

    # Verify the remap was applied (sample first loop)
    actual_u = uvs[0].uv.x
    actual_v = uvs[0].uv.y
    print(f"  UV remap -> atlas cell ({row},{col})"
          f" U[{off_u:.3f},{off_u+cell_u:.3f}] V[{off_v:.3f},{off_v+cell_v:.3f}]"
          f"  (sample: u={actual_u:.3f} v={actual_v:.3f})")


def remap_uvs_to_atlas(obj, atlas_grid, row, col):
    """Remap the active UV map into an atlas cell.

    Maps UV [0,1]x[0,1] -> atlas cell with V-flip (Blender V=0-at-bottom ->
    Irrlicht/DirectX V=0-at-top convention).  Does NOT normalise the bounding
    box — the UV range is assumed to already span [0,1] (as produced by
    Smart UV Project or equivalent).  This keeps UV coordinates consistent
    with a directly-pasted basecolor texture in the atlas cell.
    """
    uv_layer = obj.data.uv_layers.active
    if not uv_layer:
        print("  WARNING: no UV layer — skipping atlas remap")
        return

    cell_u = 1.0 / atlas_grid
    cell_v = 1.0 / atlas_grid
    off_u = col * cell_u
    off_v = row * cell_v

    uvs = uv_layer.data
    for i in range(len(uvs)):
        u = uvs[i].uv.x
        v = uvs[i].uv.y
        uvs[i].uv.x = off_u + u * cell_u
        uvs[i].uv.y = off_v + (1.0 - v) * cell_v   # V-flip: Blender -> Irrlicht

    print(f"  UV remap -> atlas cell ({row},{col})"
          f" U[{off_u:.3f},{off_u+cell_u:.3f}] V[{off_v:.3f},{off_v+cell_v:.3f}]")


# ===========================================================================
# Decimation
# ===========================================================================

def decimate_per_part(obj, target_tris):
    """Decimate via a single global DECIMATE COLLAPSE modifier.

    The old per-loose-part approach was broken for Tripo3D meshes: they
    contain thousands of tiny disconnected pieces (window frames, glass,
    etc.) each with only a handful of triangles.  The max(4,...) floor
    meant every micro-part kept 4 faces → total barely changed.

    A single global modifier is simpler and reliably hits the target.
    """
    work = obj.copy()
    work.data = obj.data.copy()
    bpy.context.collection.objects.link(work)
    select_only(work)

    # Triangulate so polygon count == triangle count.
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.quads_convert_to_tris()
    bpy.ops.object.mode_set(mode='OBJECT')

    n_total = len(work.data.polygons)
    print(f"  Decimating: {n_total} tris -> target {target_tris}")

    if n_total > target_tris:
        ratio = max(0.001, target_tris / n_total)
        mod = work.modifiers.new("Dec", 'DECIMATE')
        mod.decimate_type = 'COLLAPSE'
        mod.ratio = ratio
        bpy.ops.object.modifier_apply(modifier="Dec")

    n_result = len(work.data.polygons)
    print(f"  Decimated to {n_result} tris")
    return work, n_result


def decimate_voxel_then_collapse(obj, target_tris):
    """Voxel remesh + DECIMATE COLLAPSE for very low poly LOD2."""
    work = obj.copy()
    work.data = obj.data.copy()
    bpy.context.collection.objects.link(work)
    select_only(work)

    work.data.calc_loop_triangles()
    current = len(work.data.loop_triangles)

    mod_r = work.modifiers.new(name="REMESH", type='REMESH')
    mod_r.mode = 'VOXEL'
    mod_r.voxel_size = 1.5
    bpy.ops.object.modifier_apply(modifier="REMESH")
    work.data.calc_loop_triangles()
    after_remesh = len(work.data.loop_triangles)
    print(f"  Voxel remesh: {current} -> {after_remesh} tris")

    if after_remesh > target_tris:
        ratio = target_tris / after_remesh
        mod = work.modifiers.new(name="DECIMATE", type='DECIMATE')
        mod.ratio = max(ratio, 0.001)
        bpy.ops.object.modifier_apply(modifier="DECIMATE")
        work.data.calc_loop_triangles()
        result_tris = len(work.data.loop_triangles)
        print(f"  Collapsed: {after_remesh} -> {result_tris} tris")
    else:
        result_tris = after_remesh

    return work, result_tris


# ===========================================================================
# PLY export
# ===========================================================================

def triangulate_obj(obj):
    """Ensure mesh is fully triangulated."""
    select_only(obj)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.quads_convert_to_tris()
    bpy.ops.object.mode_set(mode='OBJECT')


def _fix_ply_for_irrlicht(filepath):
    """Post-process a PLY binary file for Irrlicht's CPLYMeshFileLoader:

    UV property names: Blender 4.x exports 's'/'t'; Irrlicht only reads 'u'/'v'.
    Rename 'property float s' -> 'property float u' and 't' -> 'v' in header.

    No axis swap is needed.  The PLY exporter is called with forward_axis='Z',
    up_axis='Y', which maps Blender Y (height after orient_zup_to_yup) into PLY z
    and Blender Z (depth) into PLY y — exactly matching Irrlicht's convention:
    CPLYMeshFileLoader reads PLY z -> Pos.Y (up) and PLY y -> Pos.Z (depth).
    """
    with open(filepath, "rb") as f:
        raw = f.read()

    eoh_tag = b"end_header\n"
    eoh = raw.find(eoh_tag)
    assert eoh != -1, f"_fix_ply_for_irrlicht: no end_header in {filepath}"
    header = bytearray(raw[:eoh])

    # Rename UV property names s -> u, t -> v
    header = header.replace(b"property float s\n", b"property float u\n")
    header = header.replace(b"property float t\n", b"property float v\n")

    # Fix face index type: Blender 4.x exports "uint" for face vertex indices.
    # Irrlicht's unpatched CPLYMeshFileLoader maps "uint" -> EPLYPT_INT16 (2 bytes).
    # Replace with "int32" which maps correctly to EPLYPT_INT32 (4 bytes).
    # Binary data is unchanged — both are 4-byte little-endian integers.
    header = header.replace(
        b"property list uchar uint vertex_indices",
        b"property list uchar int32 vertex_indices",
    )

    # Parse vertex count for the log message
    n_verts = None
    props = []
    in_vertex = False
    for line in header.split(b"\n"):
        if line.startswith(b"element vertex "):
            in_vertex = True
            n_verts = int(line.split()[-1])
        elif line.startswith(b"element ") and not line.startswith(b"element vertex"):
            in_vertex = False
        elif in_vertex and line.startswith(b"property float "):
            props.append(line.split()[-1].decode())

    rest = raw[eoh + len(eoh_tag):]

    with open(filepath, "wb") as f:
        f.write(bytes(header))
        f.write(eoh_tag)
        f.write(rest)

    print(f"  Irrlicht PLY fix: s/t->u/v renamed "
          f"({n_verts} verts, props={props})")


def export_ply(obj, filepath):
    """Export a single object as PLY with normals and UVs."""
    triangulate_obj(obj)

    # Recalculate normals outward
    select_only(obj)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.normals_make_consistent(inside=False)
    bpy.ops.object.mode_set(mode='OBJECT')

    # Deselect all, select only the target
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj

    os.makedirs(os.path.dirname(filepath), exist_ok=True)

    # Blender 4.x PLY exporter (bpy.ops.export_mesh.ply was removed in 4.0).
    # forward_axis='Z', up_axis='Y': maps Blender Y (height after orient_zup_to_yup)
    # directly to PLY z, and Blender Z (depth) to PLY y.  Irrlicht's CPLYMeshFileLoader
    # reads PLY z -> Pos.Y (height) and PLY y -> Pos.Z (depth), so no axis swap is
    # needed in the post-processor — only the UV property rename (s/t -> u/v).
    bpy.ops.wm.ply_export(
        filepath=filepath,
        ascii_format=False,
        export_normals=True,
        export_uv=True,
        export_colors='NONE',
        global_scale=1.0,
        forward_axis='Z',
        up_axis='Y',
        export_selected_objects=True,   # only the active/selected object, not entire scene
    )

    # Post-process for Irrlicht compatibility: rename UV props s/t -> u/v.
    # No axis swap needed — the forward_axis='Z' export already maps Blender Y
    # (height) into PLY z and Blender Z (depth) into PLY y, matching Irrlicht's
    # CPLYMeshFileLoader convention exactly.
    _fix_ply_for_irrlicht(filepath)

    sz = os.path.getsize(filepath)
    n = len(obj.data.polygons)
    print(f"  Exported: {filepath}  ({sz:,} bytes, {n} polys)")
    return sz


# ===========================================================================
# Duplicate helper
# ===========================================================================

def duplicate_obj(src, name):
    new_mesh = src.data.copy()
    new_obj = src.copy()
    new_obj.data = new_mesh
    new_obj.name = name
    bpy.context.scene.collection.objects.link(new_obj)
    return new_obj


# ===========================================================================
# Pipeline: extract FBX from zip
# ===========================================================================

def extract_fbx_from_zip(zip_path, tmp_dir):
    """Extract the .fbx file from a Tripo3D zip. Returns the FBX path."""
    with zipfile.ZipFile(zip_path, 'r') as zf:
        fbx_names = [n for n in zf.namelist() if n.lower().endswith('.fbx')]
        if not fbx_names:
            raise RuntimeError(f"No .fbx file found in {zip_path}")
        zf.extractall(tmp_dir)
    return os.path.join(tmp_dir, fbx_names[0])


def find_basecolor_jpg(tmp_dir):
    """Find the basecolor JPEG in extracted Tripo3D files."""
    for root, _, files in os.walk(tmp_dir):
        for f in files:
            if 'basecolor' in f.lower() and f.lower().endswith(('.jpg', '.jpeg')):
                return os.path.join(root, f)
    return None


# ===========================================================================
# Main conversion pipelines
# ===========================================================================

def convert_vehicle(fbx_path, asset_name, atlas_row, atlas_col, target_length):
    """Full vehicle conversion pipeline: FBX -> PLY (LOD0 + LOD1)."""
    out_dir = os.path.join(_ASSETS_DIR, "3d", "vehicles")
    out_lod0 = os.path.join(out_dir, f"{asset_name}_lod0.ply")
    out_lod1 = os.path.join(out_dir, f"{asset_name}_lod1.ply")

    print(f"\n{'='*60}")
    print(f"Vehicle: {asset_name}")
    print(f"  FBX: {fbx_path}")
    print(f"  -> {out_lod0}")
    print(f"  -> {out_lod1}")
    print(f"{'='*60}")

    reset_scene()

    # Import
    imported = import_fbx(fbx_path)
    remove_non_mesh(imported)
    obj = join_meshes([o for o in bpy.context.scene.objects if o.type == 'MESH'])
    if not obj:
        print("  ERROR: No mesh objects in FBX")
        return False
    apply_transforms(obj)
    print(f"  Imported: {tri_count(obj)} tris")

    # Orient: Z-up -> Y-up, long axis along Z, nose forward
    orient_zup_to_yup(obj)
    ensure_long_axis_z(obj)
    flip_nose_forward(obj)

    # Scale
    scale_vehicle(obj, target_length)

    # Remove interior
    obj = remove_vehicle_interior(obj)

    # Smooth shading (no UV dependency)
    select_only(obj)
    bpy.ops.object.shade_smooth()

    # LOD0: duplicate from original obj (original UV), then remap to atlas.
    # Do NOT modify obj's UV — each LOD gets remapped independently.
    print("\n  [LOD0] Full fidelity")
    lod0 = duplicate_obj(obj, f"{asset_name}_lod0")
    remap_uvs_to_atlas(lod0, VEHICLE_ATLAS_GRID, atlas_row, atlas_col)
    export_ply(lod0, out_lod0)

    # LOD1: decimate from original obj (original UV), then remap independently.
    print(f"\n  [LOD1] Decimate to ~{VEHICLE_LOD1_TRIS} tris")
    lod1, _ = decimate_per_part(obj, VEHICLE_LOD1_TRIS)
    remap_uvs_to_atlas(lod1, VEHICLE_ATLAS_GRID, atlas_row, atlas_col)
    export_ply(lod1, out_lod1)

    return True


def convert_building(fbx_path, asset_name, atlas_row, atlas_col, footprint_tiles,
                     basecolor_jpg=None, generate_lod2=True):
    """Full building conversion pipeline: FBX -> PLY (LOD0 + LOD1 [+ LOD2]).

    LOD2 is skipped when generate_lod2=False (buildings with height_floors <= 3).
    """
    out_dir = os.path.join(_ASSETS_DIR, "3d", "buildings")
    out_lod0 = os.path.join(out_dir, f"{asset_name}_lod0.ply")
    out_lod1 = os.path.join(out_dir, f"{asset_name}_lod1.ply")
    out_lod2 = os.path.join(out_dir, f"{asset_name}_lod2.ply")

    print(f"\n{'='*60}")
    print(f"Building: {asset_name} ({footprint_tiles}x{footprint_tiles} tiles, "
          f"lod2={'yes' if generate_lod2 else 'no'})")
    print(f"  FBX: {fbx_path}")
    print(f"  -> {out_lod0}")
    print(f"  -> {out_lod1}")
    if generate_lod2:
        print(f"  -> {out_lod2}")
    print(f"{'='*60}")

    reset_scene()

    # Import
    imported = import_fbx(fbx_path)
    remove_non_mesh(imported)
    obj = join_meshes([o for o in bpy.context.scene.objects if o.type == 'MESH'])
    if not obj:
        print("  ERROR: No mesh objects in FBX")
        return False
    apply_transforms(obj)
    print(f"  Imported: {tri_count(obj)} tris")

    # Orient
    orient_zup_to_yup(obj)

    # Scale to footprint
    scale_building(obj, footprint_tiles)

    # Paste the Tripo3D basecolor into the atlas cell once (shared by all LODs).
    # remap_uvs_to_atlas below maps the original Tripo3D UV [0,1] -> atlas cell,
    # preserving the UV-to-texture relationship from the source asset.
    if basecolor_jpg and os.path.exists(basecolor_jpg):
        bake_basecolor_to_atlas(basecolor_jpg, atlas_row, atlas_col)

    # LOD0: duplicate from original obj (with original Tripo3D UV), then remap.
    # This preserves the correct UV-to-texture mapping — do NOT modify obj's UV.
    print("\n  [LOD0] Full fidelity")
    lod0 = duplicate_obj(obj, f"{asset_name}_lod0")
    remap_uvs_to_atlas(lod0, BUILDING_ATLAS_GRID, atlas_row, atlas_col)
    export_ply(lod0, out_lod0)

    # LOD1: decimate from original obj (original UV), then remap independently.
    print(f"\n  [LOD1] Decimate to ~{BUILDING_LOD1_TRIS} tris")
    lod1, _ = decimate_per_part(obj, BUILDING_LOD1_TRIS)
    remap_uvs_to_atlas(lod1, BUILDING_ATLAS_GRID, atlas_row, atlas_col)
    export_ply(lod1, out_lod1)

    # LOD2: voxel remesh + collapse (only for tall buildings, height_floors > 3)
    if generate_lod2:
        print(f"\n  [LOD2] Voxel remesh + decimate to ~{BUILDING_LOD2_TRIS} tris")
        lod2, _ = decimate_voxel_then_collapse(obj, BUILDING_LOD2_TRIS)
        remap_uvs_to_atlas(lod2, BUILDING_ATLAS_GRID, atlas_row, atlas_col)
        export_ply(lod2, out_lod2)
    else:
        print(f"\n  [LOD2] Skipped (height_floors <= 3)")

    return True


def bake_basecolor_to_atlas(basecolor_path, atlas_row, atlas_col):
    """Paste basecolor into the building atlas PNG at the given cell.

    Uses bpy.data.images + numpy — no Pillow required.
    Blender pixel arrays are bottom-up so the row index is flipped.
    """
    atlas_png = os.path.join(_ASSETS_DIR, "textures", "buildings", "buildings_atlas_d.png")
    if not os.path.exists(atlas_png):
        print(f"  WARNING: Atlas not found at {atlas_png} — skipping bake")
        return

    import numpy as np
    cell_px = 4096 // BUILDING_ATLAS_GRID  # 512 (spec: buildings atlas is 4096×4096)

    try:
        atlas_img = bpy.data.images.load(atlas_png, check_existing=False)
        atlas_w, atlas_h = atlas_img.size
        atlas_px = np.array(atlas_img.pixels[:]).reshape(atlas_h, atlas_w, 4)

        bc_img = bpy.data.images.load(basecolor_path, check_existing=False)
        # Scale to cell_px x cell_px if needed
        if bc_img.size[0] != cell_px or bc_img.size[1] != cell_px:
            bc_img.scale(cell_px, cell_px)
        bc_px = np.array(bc_img.pixels[:]).reshape(cell_px, cell_px, 4)
        bpy.data.images.remove(bc_img)

        cell_x = atlas_col * cell_px
        cell_y = (BUILDING_ATLAS_GRID - 1 - atlas_row) * cell_px  # flip: Blender Y=0 at bottom
        atlas_px[cell_y:cell_y + cell_px, cell_x:cell_x + cell_px] = bc_px

        atlas_img.pixels = atlas_px.flatten().tolist()
        atlas_img.filepath_raw = atlas_png
        atlas_img.file_format = 'PNG'
        atlas_img.save()
        bpy.data.images.remove(atlas_img)
        print(f"  Baked basecolor into atlas cell ({atlas_row},{atlas_col})")
    except Exception as e:
        print(f"  WARNING: bake_basecolor_to_atlas failed ({e})")


# ===========================================================================
# Entry points
# ===========================================================================

def convert_single(zip_path, asset_name, asset_type, atlas_row, atlas_col,
                   footprint=1, target_length=DEFAULT_VEHICLE_LENGTH_M,
                   generate_lod2=True):
    """Convert a single Tripo3D zip to PLY."""
    with tempfile.TemporaryDirectory() as tmp_dir:
        print(f"Extracting {zip_path}...")
        fbx_path = extract_fbx_from_zip(zip_path, tmp_dir)
        basecolor = find_basecolor_jpg(tmp_dir)

        if asset_type == "vehicle":
            return convert_vehicle(fbx_path, asset_name, atlas_row, atlas_col, target_length)
        else:
            return convert_building(fbx_path, asset_name, atlas_row, atlas_col, footprint,
                                    basecolor, generate_lod2=generate_lod2)


# ===========================================================================
# CLI argument parsing
# ===========================================================================

def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []

    if not argv:
        print(__doc__)
        sys.exit(1)

    # Parse named arguments
    args = {}
    i = 0
    while i < len(argv):
        if argv[i].startswith("--"):
            key = argv[i][2:]
            if i + 1 < len(argv) and not argv[i + 1].startswith("--"):
                args[key] = argv[i + 1]
                i += 2
            else:
                args[key] = True
                i += 1
        else:
            i += 1

    zip_path = args.get("zip")
    name = args.get("name")
    atype = args.get("type")

    if not zip_path or not name or not atype:
        print("ERROR: --zip, --name, and --type are required")
        print(__doc__)
        sys.exit(1)

    convert_single(
        zip_path,
        name,
        atype,
        int(args.get("atlas-row", 0)),
        int(args.get("atlas-col", 0)),
        int(args.get("footprint", 1)),
        float(args.get("target-length", DEFAULT_VEHICLE_LENGTH_M)),
    )


if __name__ == "__main__":
    main()
