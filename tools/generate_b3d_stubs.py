#!/usr/bin/env python3
"""
generate_b3d_stubs.py — Phase 9 placeholder B3D stub generator for AI Town.

Generates minimal valid B3D placeholder files for all V1 building and vehicle assets.

A minimal B3D file consists of exactly 12 bytes:
  - Bytes 0-3:  "BB3D"  (chunk ID, ASCII)
  - Bytes 4-7:  4       (chunk size, little-endian int32 — payload is 4 bytes: just the version)
  - Bytes 8-11: 2       (version, little-endian int32 — B3D format version 2)

This header passes export validation check #4 (non-empty file check). It is a deliberately
minimal stub — no geometry, no textures, no animation. Full geometry is authored in Phase 9.

Usage (run from workspace root):
    python tools/generate_b3d_stubs.py

Output:
    assets/3d/buildings/<name>.b3d
    assets/3d/vehicles/<name>.b3d

Naming convention (per architecture/asset-standards/3d-model-standards.md):
    Buildings: <zone>_<tier>_<variant>_lod<N>.b3d
      e.g. res_low_01_lod0.b3d
    Vehicles:  <type>_lod<N>.b3d
      e.g. car_sedan_lod0.b3d

LOD2 rules (per 3d-model-standards.md LOD Requirements table):
  - Small buildings (height_floors <= 3): LOD2 is billboard only — NO _lod2.b3d generated.
    Low tier (height_floors=2) and Med tier (height_floors=3) both use billboard LOD2.
  - Large buildings (height_floors >= 4): LOD2 is geometry shell — _lod2.b3d IS generated.
    High tier (height_floors=6) uses _lod2.b3d geometry shell.
  - LOD2 MUST NOT co-exist with a _billboard.dds atlas for the same asset (project rule).
"""

import os
import struct

# Workspace root is the directory this script is run from (or two levels up from tools/).
WORKSPACE_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILDINGS_DIR = os.path.join(WORKSPACE_ROOT, "assets", "3d", "buildings")
VEHICLES_DIR = os.path.join(WORKSPACE_ROOT, "assets", "3d", "vehicles")

# Minimal valid B3D header: "BB3D" + chunk_size=4 + version=2
B3D_STUB = b"BB3D" + struct.pack("<i", 4) + struct.pack("<i", 2)


def write_b3d(path: str) -> None:
    """Write a minimal 12-byte B3D stub to path. Creates parent dirs if needed."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(B3D_STUB)


def generate_building_stubs() -> list[str]:
    """
    Generate B3D stubs for all V1 building assets.

    V1 building set — 18 base names (2 variants x 3 zones x 3 tiers):
      Zones:    res (Residential), com (Commercial), ind (Industrial)
      Tiers:    low (height_floors=2), med (height_floors=3), high (height_floors=6)
      Variants: _01, _02

    LOD files per tier:
      low/med (height_floors <= 3): _lod0.b3d, _lod1.b3d  (LOD2 = billboard only, no .b3d)
      high    (height_floors >= 4): _lod0.b3d, _lod1.b3d, _lod2.b3d (geometry shell)
    """
    zones = ["res", "com", "ind"]
    tiers = ["low", "med", "high"]
    variants = ["01", "02"]

    generated = []

    for zone in zones:
        for tier in tiers:
            # Determine which LOD files to generate
            if tier == "high":
                # height_floors=6 >= 4 -> geometry shell at LOD2
                lod_levels = [0, 1, 2]
            else:
                # height_floors=2 (low) or 3 (med) — billboard LOD2, no _lod2.b3d
                lod_levels = [0, 1]

            for variant in variants:
                base_name = f"{zone}_{tier}_{variant}"
                for lod in lod_levels:
                    filename = f"{base_name}_lod{lod}.b3d"
                    path = os.path.join(BUILDINGS_DIR, filename)
                    write_b3d(path)
                    generated.append(path)

    return generated


def generate_vehicle_stubs() -> list[str]:
    """
    Generate B3D stubs for all V1 vehicle assets.

    V1 vehicle types (5 total):
      car_sedan, car_hatchback, car_suv, bus_standard, truck_cargo

    LOD files per vehicle:
      LOD0, LOD1  (LOD2 = point/sprite, no .b3d)
    """
    vehicle_types = [
        "car_sedan",
        "car_hatchback",
        "car_suv",
        "bus_standard",
        "truck_cargo",
    ]
    lod_levels = [0, 1]  # LOD2 is point/sprite — no .b3d file

    generated = []

    for vehicle in vehicle_types:
        for lod in lod_levels:
            filename = f"{vehicle}_lod{lod}.b3d"
            path = os.path.join(VEHICLES_DIR, filename)
            write_b3d(path)
            generated.append(path)

    return generated


def main() -> None:
    print("generate_b3d_stubs.py — AI Town Phase 9 B3D stub generator")
    print(f"Workspace root: {WORKSPACE_ROOT}")
    print()

    print("Generating building B3D stubs...")
    building_files = generate_building_stubs()
    for p in building_files:
        rel = os.path.relpath(p, WORKSPACE_ROOT)
        size = os.path.getsize(p)
        print(f"  WROTE  {rel}  ({size} bytes)")

    print()
    print("Generating vehicle B3D stubs...")
    vehicle_files = generate_vehicle_stubs()
    for p in vehicle_files:
        rel = os.path.relpath(p, WORKSPACE_ROOT)
        size = os.path.getsize(p)
        print(f"  WROTE  {rel}  ({size} bytes)")

    total = len(building_files) + len(vehicle_files)
    print()
    print(f"Done. {len(building_files)} building files + {len(vehicle_files)} vehicle files = {total} total B3D stubs.")

    # Verify all stubs are exactly 12 bytes and start with "BB3D"
    print()
    print("Verifying stubs...")
    errors = 0
    for p in building_files + vehicle_files:
        with open(p, "rb") as f:
            data = f.read()
        if len(data) != 12 or data[:4] != b"BB3D":
            print(f"  ERROR: {p} — unexpected content (len={len(data)}, magic={data[:4]!r})")
            errors += 1
    if errors == 0:
        print(f"  All {total} stubs verified: 12 bytes, BB3D magic. OK.")
    else:
        print(f"  {errors} verification error(s) found.")


if __name__ == "__main__":
    main()
