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
Only ZIPs with a corresponding `.meta` file in `assets/3d/` are processed —
the `.meta` is the authoritative source for type and atlas position.

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

## Step 3 — Find and filter ZIPs

Find all ZIPs in the source directory:

```bash
find <source_dir> -name "*.zip" | sort
```

For each ZIP, the asset name is the filename without `.zip`. Check whether a
`.meta` file exists for it:

- Buildings: `assets/3d/buildings/<name>.meta`
- Vehicles: `assets/3d/vehicles/<name>.meta`

(Check both locations — the category is determined by which one exists.)

**Split into two groups:**
- **Ready**: ZIP has a `.meta` → will be converted
- **Skipped**: ZIP has no `.meta` → skip silently for now, report at the end

If the ready list is empty, tell the user and stop.

---

## Step 4 — Read metadata and collect missing fields

For each ZIP in the ready list, read its `.meta` file to get:
- `category` → `--type` (`building` or `vehicle`)
- `atlas_cell.row` → `--atlas-row`
- `atlas_cell.col` → `--atlas-col`

The only field not in `.meta` is the scaling parameter:
- **Buildings**: `footprint` — tile footprint (1, 2, or 3). Derivable from the
  asset name tier: `_low_` → 1, `_med_` → 2, `_high_` → 3. Propose these
  defaults and ask the user to confirm or correct before proceeding.
- **Vehicles**: `target-length` in metres (default 4.0). Use the default unless
  the user has specified otherwise.

Present a summary table of all ready assets with the proposed parameters and
ask the user to confirm before running anything.

---

## Step 5 — Convert one ZIP at a time

For each confirmed asset, run:

```bash
blender --background --python tools/convert_tripo3d_to_ply.py -- \
  --zip <path/to/file.zip> \
  --name <basename_without_extension> \
  --type <vehicle|building> \
  --atlas-row <R> \
  --atlas-col <C> \
  --footprint <N>           # buildings only
  --target-length <L>       # vehicles only
```

Show the user which asset is being processed and whether it succeeded or failed
before moving to the next. Don't abort the whole run on a single failure — finish
all ZIPs and report failures at the end.

LOD2 is generated automatically by the script when `height_floors > 3` (read
from the `.meta` file).

---

## Step 6 — Rebuild the atlas

After all conversions finish (regardless of individual failures), regenerate the
building texture atlas so the new asset textures are compiled into the DDS files:

```bash
python3 tools/generate_atlas_dds.py
```

This rewrites:
- `assets/textures/buildings/buildings_atlas_d.dds` (4096×4096, 5 mip levels)
- `assets/textures/buildings/buildings_atlas_d_2k.dds` (2048×2048, 4 mip levels)
- `assets/textures/buildings/buildings_atlas_d.png` (source PNG)

If the atlas script fails, report the error but still show the conversion results —
the PLY files are valid even without the updated atlas.

---

## Step 7 — Report results

After all ZIPs are processed, show a summary:

```
Conversion complete
  OK:      N  (list names)
  FAIL:    N  (list names + error)
  Skipped: N  (list names — no .meta found)
```

Output files land in:
- `assets/3d/buildings/<name>_lod0.ply`, `_lod1.ply`, `[_lod2.ply]`
- `assets/3d/vehicles/<name>_lod0.ply`, `_lod1.ply`

If any failed, suggest re-running the script manually for those specific ZIPs
to see the full Blender output. If any were skipped, remind the user that a
`.meta` file must exist in `assets/3d/` before a ZIP can be converted.
