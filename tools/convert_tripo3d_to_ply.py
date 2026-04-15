#!/usr/bin/env python3
"""
Blender headless pipeline: Tripo3D FBX source zips -> PLY (LOD0 + LOD1 [+ LOD2])

Converts Tripo3D source assets to Irrlicht-compatible PLY format.  PLY files
loaded by Irrlicht use CDynamicMeshBuffer with 32-bit indices, eliminating the
65,535 vertex-per-buffer limit that causes crashes with high-poly B3D meshes.

Supports both vehicles and buildings.  The asset type is inferred from the
output directory or can be set explicitly.

Usage (single asset):
  blender --background --python tools/convert_tripo3d_to_ply.py -- \\
    --zip <source.zip> --name <asset_name> --type <vehicle|building> \\
    [--atlas-row R] [--atlas-col C] [--footprint N] [--target-length L]

Usage (batch — all assets from manifest):
  blender --background --python tools/convert_tripo3d_to_ply.py -- --batch

The batch manifest is embedded in this file (MANIFEST dict).  Edit it to add
or change source-zip-to-asset-name mappings.

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
# Manifest: maps Tripo3D source zips to output asset names.
#
# Format:
#   "tripo3d/<subdir>/<zip>": {
#       "name":      "<output asset base name>",
#       "type":      "vehicle" | "building",
#       "atlas_row": <int>,   # from .meta file
#       "atlas_col": <int>,   # from .meta file
#       "footprint": <int>,   # tiles (buildings only; default 3 for com_high, 2 for com_med, 1 for small)
#       "target_len": <float> # metres (vehicles only; default 4.0)
#   }
# ---------------------------------------------------------------------------
MANIFEST = {
    # --- Vehicles ---
    "vehicles/sedan_1.zip":     {"name": "car_sedan",     "type": "vehicle", "atlas_row": 0, "atlas_col": 0},
    "vehicles/hatchback_1.zip": {"name": "car_hatchback", "type": "vehicle", "atlas_row": 0, "atlas_col": 1},
    "vehicles/suv_1.zip":       {"name": "car_suv",       "type": "vehicle", "atlas_row": 0, "atlas_col": 2},
    "vehicles/bus_1.zip":       {"name": "bus_standard",  "type": "vehicle", "atlas_row": 1, "atlas_col": 0},
    "vehicles/truck_1.zip":     {"name": "truck_cargo",   "type": "vehicle", "atlas_row": 1, "atlas_col": 1},
    "vehicles/firetruck_1.zip": {"name": "firetruck",     "type": "vehicle", "atlas_row": 1, "atlas_col": 2},

    # --- Commercial High (3x3 tiles) ---
    "medium_poly/buildings/com_high_1.zip": {"name": "com_high_01", "type": "building", "atlas_row": 2, "atlas_col": 4, "footprint": 3},
    "medium_poly/buildings/com_high_2.zip": {"name": "com_high_02", "type": "building", "atlas_row": 2, "atlas_col": 5, "footprint": 3},
    "medium_poly/buildings/com_high_3.zip": {"name": "com_high_03", "type": "building", "atlas_row": 2, "atlas_col": 6, "footprint": 3},
    "medium_poly/buildings/com_high_4.zip": {"name": "com_high_04", "type": "building", "atlas_row": 2, "atlas_col": 7, "footprint": 3},
    "medium_poly/buildings/com_high_5.zip": {"name": "com_high_05", "type": "building", "atlas_row": 3, "atlas_col": 4, "footprint": 3},

    # --- Commercial Medium (2x2 tiles) ---
    "medium_poly/buildings/com_med_1.zip": {"name": "com_med_01", "type": "building", "atlas_row": 2, "atlas_col": 0, "footprint": 2},
    "medium_poly/buildings/com_med_2.zip": {"name": "com_med_02", "type": "building", "atlas_row": 2, "atlas_col": 1, "footprint": 2},
    "medium_poly/buildings/com_med_3.zip": {"name": "com_med_03", "type": "building", "atlas_row": 2, "atlas_col": 2, "footprint": 2},
    "medium_poly/buildings/com_med_4.zip": {"name": "com_med_04", "type": "building", "atlas_row": 2, "atlas_col": 3, "footprint": 2},

    # --- Commercial Low (1x1 tile) ---
    "medium_poly/buildings/com_low_1.zip": {"name": "com_low_01", "type": "building", "atlas_row": 1, "atlas_col": 4, "footprint": 1},
    "medium_poly/buildings/com_low_2.zip": {"name": "com_low_02", "type": "building", "atlas_row": 1, "atlas_col": 5, "footprint": 1},
    "medium_poly/buildings/com_low_3.zip": {"name": "com_low_03", "type": "building", "atlas_row": 1, "atlas_col": 6, "footprint": 1},
    "medium_poly/buildings/com_low_4.zip": {"name": "com_low_04", "type": "building", "atlas_row": 1, "atlas_col": 7, "footprint": 1},

    # --- Residential High (3x3 tiles) ---
    "medium_poly/buildings/res_high_1.zip": {"name": "res_high_01", "type": "building", "atlas_row": 1, "atlas_col": 0, "footprint": 3},
    "medium_poly/buildings/res_high_2.zip": {"name": "res_high_02", "type": "building", "atlas_row": 1, "atlas_col": 1, "footprint": 3},
    "medium_poly/buildings/res_high_3.zip": {"name": "res_high_03", "type": "building", "atlas_row": 1, "atlas_col": 2, "footprint": 3},

    # --- Residential Medium (2x2 tiles) ---
    "medium_poly/buildings/res_med_1.zip":  {"name": "res_med_01", "type": "building", "atlas_row": 0, "atlas_col": 4, "footprint": 2},
    "medium_poly/buildings/res_med_2.zip":  {"name": "res_med_02", "type": "building", "atlas_row": 0, "atlas_col": 5, "footprint": 2},
    "medium_poly/buildings/res_med_03.zip": {"name": "res_med_03", "type": "building", "atlas_row": 0, "atlas_col": 6, "footprint": 2},
    "medium_poly/buildings/res_med_4.zip":  {"name": "res_med_04", "type": "building", "atlas_row": 0, "atlas_col": 7, "footprint": 2},
    "medium_poly/buildings/res_med_5.zip":  {"name": "res_med_05", "type": "building", "atlas_row": 5, "atlas_col": 0, "footprint": 2},
    "medium_poly/buildings/res_med_6.zip":  {"name": "res_med_06", "type": "building", "atlas_row": 5, "atlas_col": 1, "footprint": 2},

    # --- Residential Low (1x1 tile) ---
    "medium_poly/buildings/res_low_1.zip": {"name": "res_low_01", "type": "building", "atlas_row": 0, "atlas_col": 0, "footprint": 1},
    "medium_poly/buildings/res_low_2.zip": {"name": "res_low_02", "type": "building", "atlas_row": 0, "atlas_col": 1, "footprint": 1},
    "medium_poly/buildings/res_low_3.zip": {"name": "res_low_03", "type": "building", "atlas_row": 0, "atlas_col": 2, "footprint": 1},
    "medium_poly/buildings/res_low_4.zip": {"name": "res_low_04", "type": "building", "atlas_row": 0, "atlas_col": 3, "footprint": 1},

    # --- Service buildings (1x1 tile) ---
    # Primary replacements for existing svc_* B3D assets (keep same atlas cell)
    "medium_poly/buildings/svc_fire_medium_1.zip":   {"name": "svc_fire_station",  "type": "building", "atlas_row": 4, "atlas_col": 4, "footprint": 1},
    "medium_poly/buildings/svc_police_medium_1.zip": {"name": "svc_police_station", "type": "building", "atlas_row": 4, "atlas_col": 5, "footprint": 1},
    "medium_poly/buildings/svc_power_coal_medium_1.zip": {"name": "svc_power_plant", "type": "building", "atlas_row": 4, "atlas_col": 6, "footprint": 1},
    "medium_poly/buildings/svc_water_low_1.zip":     {"name": "svc_water_tower",   "type": "building", "atlas_row": 4, "atlas_col": 7, "footprint": 1},
    # Additional svc variants (new assets, row 5)
    "medium_poly/buildings/svc_fire_high_1.zip":          {"name": "svc_fire_high_01",          "type": "building", "atlas_row": 5, "atlas_col": 0, "footprint": 1},
    "medium_poly/buildings/svc_power_coal_medium_2.zip":  {"name": "svc_power_coal_medium_02",  "type": "building", "atlas_row": 5, "atlas_col": 1, "footprint": 1},
    "medium_poly/buildings/svc_power_nuclear_medium_1.zip": {"name": "svc_power_nuclear_medium_01", "type": "building", "atlas_row": 5, "atlas_col": 2, "footprint": 1},
    "medium_poly/buildings/svc_power_nuclear_medium_2.zip": {"name": "svc_power_nuclear_medium_02", "type": "building", "atlas_row": 5, "atlas_col": 3, "footprint": 1},
    "medium_poly/buildings/svc_power_nuclear_medium_3.zip": {"name": "svc_power_nuclear_medium_03", "type": "building", "atlas_row": 5, "atlas_col": 4, "footprint": 1},
    "medium_poly/buildings/svc_school_medium_1.zip":  {"name": "svc_school_medium_01",  "type": "building", "atlas_row": 5, "atlas_col": 5, "footprint": 1},
    "medium_poly/buildings/svc_water_low_2.zip":      {"name": "svc_water_low_02",      "type": "building", "atlas_row": 5, "atlas_col": 6, "footprint": 1},
}

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

def remap_uvs_to_atlas(obj, atlas_grid, row, col):
    """Normalize UVs to [0,1], V-flip, remap into atlas cell."""
    uv_layer = obj.data.uv_layers.active
    if not uv_layer:
        print("  WARNING: no UV layer — skipping atlas remap")
        return

    cell_u = 1.0 / atlas_grid
    cell_v = 1.0 / atlas_grid
    off_u = col * cell_u
    off_v = row * cell_v

    uvs = uv_layer.data
    us = [uvs[i].uv.x for i in range(len(uvs))]
    vs = [uvs[i].uv.y for i in range(len(uvs))]
    min_u, max_u = min(us), max(us)
    min_v, max_v = min(vs), max(vs)
    ru = (max_u - min_u) or 1.0
    rv = (max_v - min_v) or 1.0

    for i in range(len(uvs)):
        norm_u = (uvs[i].uv.x - min_u) / ru
        norm_v = (uvs[i].uv.y - min_v) / rv
        norm_v = 1.0 - norm_v  # V-flip: Blender -> OpenGL
        uvs[i].uv.x = off_u + norm_u * cell_u
        uvs[i].uv.y = off_v + norm_v * cell_v

    print(f"  UV remap -> atlas cell ({row},{col})"
          f" U[{off_u:.3f},{off_u+cell_u:.3f}] V[{off_v:.3f},{off_v+cell_v:.3f}]")


# ===========================================================================
# Decimation
# ===========================================================================

def decimate_per_part(obj, target_tris):
    """Decimate via DECIMATE COLLAPSE per loose part, then rejoin."""
    before_names = {o.name for o in bpy.data.objects}

    work = obj.copy()
    work.data = obj.data.copy()
    bpy.context.collection.objects.link(work)
    select_only(work)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.quads_convert_to_tris()
    bpy.ops.mesh.separate(type='LOOSE')
    bpy.ops.object.mode_set(mode='OBJECT')

    parts = [o for o in bpy.data.objects
             if o.type == 'MESH' and o.name not in before_names]
    n_total = sum(len(p.data.polygons) for p in parts)
    print(f"  Decimating: {len(parts)} parts, {n_total} tris -> target {target_tris}")

    for p in parts:
        n_part = len(p.data.polygons)
        part_target = max(4, int(target_tris * n_part / n_total))
        ratio = min(1.0, part_target / max(n_part, 1))
        select_only(p)
        mod = p.modifiers.new("Dec", 'DECIMATE')
        mod.decimate_type = 'COLLAPSE'
        mod.ratio = ratio
        bpy.ops.object.modifier_apply(modifier="Dec")

    bpy.ops.object.select_all(action='DESELECT')
    for p in parts:
        p.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()
    result = bpy.context.active_object
    n_result = len(result.data.polygons)
    print(f"  Decimated to {n_result} tris")
    return result, n_result


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

    # Blender 4.x PLY exporter (bpy.ops.export_mesh.ply was removed in 4.0)
    bpy.ops.wm.ply_export(
        filepath=filepath,
        ascii_format=False,
        export_normals=True,
        export_uv=True,
        export_colors='NONE',
        global_scale=1.0,
        forward_axis='NEGATIVE_Z',
        up_axis='Y',
    )

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

    # UV remap
    remap_uvs_to_atlas(obj, VEHICLE_ATLAS_GRID, atlas_row, atlas_col)

    # Smooth shading
    select_only(obj)
    bpy.ops.object.shade_smooth()

    # LOD0: full fidelity
    print("\n  [LOD0] Full fidelity")
    lod0 = duplicate_obj(obj, f"{asset_name}_lod0")
    export_ply(lod0, out_lod0)

    # LOD1: decimated
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

    # UV remap
    remap_uvs_to_atlas(obj, BUILDING_ATLAS_GRID, atlas_row, atlas_col)

    # Bake basecolor to atlas if available
    if basecolor_jpg:
        bake_basecolor_to_atlas(basecolor_jpg, atlas_row, atlas_col)

    # LOD0: full fidelity
    print("\n  [LOD0] Full fidelity")
    lod0 = duplicate_obj(obj, f"{asset_name}_lod0")
    export_ply(lod0, out_lod0)

    # LOD1: decimated
    print(f"\n  [LOD1] Decimate to ~{BUILDING_LOD1_TRIS} tris")
    lod1, _ = decimate_per_part(obj, BUILDING_LOD1_TRIS)
    remap_uvs_to_atlas(lod1, BUILDING_ATLAS_GRID, atlas_row, atlas_col)
    export_ply(lod1, out_lod1)

    # LOD2: voxel remesh + collapse (only for tall buildings, height_floors > 3)
    if generate_lod2:
        print(f"\n  [LOD2] Voxel remesh + decimate to ~{BUILDING_LOD2_TRIS} tris")
        lod2, _ = decimate_voxel_then_collapse(obj, BUILDING_LOD2_TRIS)
        # Smart UV unwrap after voxel remesh (original UVs are destroyed)
        select_only(lod2)
        bpy.ops.object.mode_set(mode='EDIT')
        bpy.ops.mesh.select_all(action='SELECT')
        bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=0.01)
        bpy.ops.object.mode_set(mode='OBJECT')
        remap_uvs_to_atlas(lod2, BUILDING_ATLAS_GRID, atlas_row, atlas_col)
        export_ply(lod2, out_lod2)
    else:
        print(f"\n  [LOD2] Skipped (height_floors <= 3)")

    return True


def bake_basecolor_to_atlas(basecolor_path, atlas_row, atlas_col):
    """Paste basecolor into the building atlas PNG at the given cell."""
    atlas_png = os.path.join(_ASSETS_DIR, "textures", "buildings", "buildings_atlas_d.png")
    if not os.path.exists(atlas_png):
        print(f"  WARNING: Atlas not found at {atlas_png} — skipping bake")
        return

    try:
        # Try Pillow from system or Blender's bundled Python
        for extra in ['/home/node/.local/lib/python3.11/site-packages',
                      '/usr/lib/python3/dist-packages']:
            if extra not in sys.path:
                sys.path.insert(0, extra)
        from PIL import Image
    except ImportError:
        print("  WARNING: Pillow not available — skipping atlas bake")
        return

    cell_px = 2048 // BUILDING_ATLAS_GRID  # 256
    atlas = Image.open(atlas_png).convert("RGB")
    bc = Image.open(basecolor_path).convert("RGB").resize((cell_px, cell_px), Image.LANCZOS)
    x0 = atlas_col * cell_px
    y0 = atlas_row * cell_px
    atlas.paste(bc, (x0, y0, x0 + cell_px, y0 + cell_px))
    atlas.save(atlas_png)
    print(f"  Baked basecolor into atlas cell ({atlas_row},{atlas_col})")


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


def convert_batch():
    """Convert all assets listed in MANIFEST."""
    tripo_dir = os.path.join(_ASSETS_DIR, "tripo3d")
    results = {"ok": [], "fail": [], "skip": []}

    for rel_zip, cfg in sorted(MANIFEST.items()):
        zip_path = os.path.join(tripo_dir, rel_zip)
        if not os.path.exists(zip_path):
            print(f"SKIP: {zip_path} not found")
            results["skip"].append(rel_zip)
            continue

        try:
            # Determine LOD2: generate only for buildings with height_floors > 3.
            # Read from existing .meta file; default False (no LOD2) when absent.
            lod2 = False
            if cfg["type"] == "building":
                floors = read_height_floors(cfg["name"], cfg["type"])
                lod2 = floors > 3
                if floors == 0:
                    print(f"  NOTE: no meta for {cfg['name']} — LOD2 skipped")

            ok = convert_single(
                zip_path,
                cfg["name"],
                cfg["type"],
                cfg.get("atlas_row", 0),
                cfg.get("atlas_col", 0),
                cfg.get("footprint", 1),
                cfg.get("target_len", DEFAULT_VEHICLE_LENGTH_M),
                generate_lod2=lod2,
            )
            if ok:
                results["ok"].append(cfg["name"])
            else:
                results["fail"].append(cfg["name"])
        except Exception as e:
            print(f"FAIL: {cfg['name']}: {e}")
            results["fail"].append(cfg["name"])

    print(f"\n{'='*60}")
    print(f"BATCH COMPLETE")
    print(f"  OK:   {len(results['ok'])}")
    print(f"  FAIL: {len(results['fail'])}")
    print(f"  SKIP: {len(results['skip'])}")
    if results["fail"]:
        print(f"  Failed: {results['fail']}")
    if results["skip"]:
        print(f"  Skipped: {results['skip']}")
    print(f"{'='*60}")


# ===========================================================================
# CLI argument parsing
# ===========================================================================

def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []

    if not argv:
        print(__doc__)
        sys.exit(1)

    if argv[0] == "--batch":
        convert_batch()
        return

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
    atype = args.get("type", "vehicle")

    if not zip_path or not name:
        print("ERROR: --zip and --name are required for single-asset mode")
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
