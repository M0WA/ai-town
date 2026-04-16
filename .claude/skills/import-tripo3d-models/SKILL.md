---
name: import-tripo3d-models
description: >
  Convert Tripo3D source ZIP assets to production-ready PLY models in assets/3d/.
  Use this skill whenever the user wants to run the 3D model conversion pipeline,
  import new Tripo3D models, regenerate PLY files, or convert ZIPs from
  assets/tripo3d/ — even if they say "convert the buildings", "run the PLY
  pipeline", "import the new models", or "process the zips".
---

# Import Tripo3D Models

Drives `tools/convert_tripo3d_to_ply.py` (Blender headless) one ZIP at a time.
The user renames ZIPs upfront so the ZIP filename is the output asset name.
For each ZIP the skill asks for atlas position and footprint, then runs the conversion.

---

## Step 1 — Determine source directory

Check if the user specified a directory. Default: `assets/tripo3d/medium_poly`.

---

## Step 2 — Check Blender

```bash
blender --version
```

If Blender is not found, stop and tell the user to install it.

---

## Step 3 — Find ZIPs

```bash
find <source_dir> -name "*.zip" | sort
```

List the found ZIPs to the user (show just the filenames, not full paths).
The filename without `.zip` is the intended output asset name — confirm this with the user before continuing.

If no ZIPs are found, say so and stop.

---

## Step 4 — Collect metadata

For **each ZIP**, ask the user for:

| Field | Buildings | Vehicles |
|---|---|---|
| `type` | `building` | `vehicle` |
| `atlas-row` | row index in 8×8 atlas (0–7) | row index in 4×4 atlas (0–3) |
| `atlas-col` | col index in 8×8 atlas (0–7) | col index in 4×4 atlas (0–3) |
| `footprint` | tile footprint: 1, 2, or 3 | — |
| `target-length` | — | vehicle length in metres (default 4.0) |

Collect this for all ZIPs at once before starting any conversions, so the user
isn't interrupted mid-run. Present a summary table and ask the user to confirm
before proceeding.

**Atlas reference** (buildings, 8×8 grid):
- See `architecture/asset-standards/building-atlas-layout.md` for the full cell map.
- Vehicles use a separate 4×4 atlas (rows 0–1 in use for V1).

---

## Step 5 — Convert one ZIP at a time

For each ZIP, run:

```bash
blender --background --python tools/convert_tripo3d_to_ply.py -- \
  --zip <path/to/file.zip> \
  --name <basename_without_extension> \
  --type <vehicle|building> \
  --atlas-row <R> \
  --atlas-col <C> \
  --footprint <N>          # buildings only
  --target-length <L>      # vehicles only
```

Show the user which asset is being processed and whether it succeeded or failed
before moving to the next. Don't abort the whole run on a single failure — finish
all ZIPs and report failures at the end.

LOD2 is generated automatically by the script when `height_floors > 3` (read from
the existing `.meta` file). If the `.meta` doesn't exist yet, LOD2 is skipped and
logged; the user can create the `.meta` and re-run.

---

## Step 6 — Report results

After all ZIPs are processed, show a summary:

```
Conversion complete
  OK:   N  (list names)
  FAIL: N  (list names + error)
```

Remind the user that output files landed in:
- `assets/3d/buildings/<name>_lod0.ply`, `_lod1.ply`, `[_lod2.ply]`
- `assets/3d/vehicles/<name>_lod0.ply`, `_lod1.ply`

If any failed, suggest re-running the script manually with verbose output for
those specific ZIPs to diagnose the issue.
