---
name: model-review
description: >
  Interactive visual review loop for AI Town 3D model assets. Use this skill whenever the user
  wants to review, inspect, check, sign off on, or validate 3D models — even if they phrase it
  as "look at the buildings", "check the assets", "validate models visually", "review the 3D
  stuff", or "are the models ok". Launches the model validator tool with all LODs visible,
  guides the user through annotating problems with the built-in paint tool, reads annotated
  screenshots, and dispatches fix requests to the 3D model and texture artist agents. Loops
  until each model is approved. Works for a single named model or the full asset library.
---

# Model Review Skill

A guided review loop for AI Town's 3D assets using the built-in model validator tool. Each model
is shown with all its LODs side by side. The user annotates issues with the paint tool and saves
screenshots. Claude reads the screenshots, understands the problems, and dispatches fixes to the
artist agents. Repeat until approved.

---

## Step 1 — Determine scope

Ask the user (or infer from context):

- **Specific model**: e.g. "review res_low_01" → use `--model res_low_01`
- **Full review**: iterate through every model in the asset library (see model list below)

If the user didn't specify, ask: "Do you want to review a specific model, or go through all models?"

---

## Step 2 — Build check

Verify `./build/aitown_model_validator` exists. If it doesn't, build it:

```bash
cmake -B build -S . -G Ninja \
  -DCMAKE_MAKE_PROGRAM=/usr/bin/ninja \
  -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_OVERLAY_PORTS=vcpkg-overlays \
  -DENABLE_COVERAGE=OFF
cmake --build build --target aitown_model_validator -- -j$(nproc)
```

---

## Step 3 — Launch the validator

Run from the repo root so screenshots land in `/home/user/repos/ai-town/`:

```bash
./build/aitown_model_validator --model <model_name>
```

The `--model` flag shows **all LODs (LOD0, LOD1, LOD2) side by side** for the named model —
this is the correct mode for review. Do not run without `--model` (category mode only shows LOD0).

For a full review, do **one model at a time** — launch, wait for the user to review and tell
you the result, then move on to the next.

---

## Step 4 — Brief the user on annotation controls

Tell the user (first time only, skip on repeat runs):

```
The validator window is open. Controls:
  Left drag       Draw annotation marks
  V               Cycle shape: DOT → CIRCLE → CROSS
  C               Cycle colour: Red → Green → Blue → Yellow → Cyan → Magenta → White → Black
  S               Save annotated screenshot (saved as annotation_N.png in repo root)
  Z               Undo last stroke
  X               Clear all marks
  Right drag      Orbit camera
  Mouse wheel     Zoom in/out

When you're done, either:
  - Say "approved" to move on
  - Save a screenshot (S), then tell me the path and describe the issue
```

---

## Step 5 — Collect feedback

Wait for the user to respond. Two outcomes:

**Approved** — note it, move to the next model (full review) or finish (single model).

**Issues found** — the user will give you one or more screenshot paths and describe the problem.
Read each screenshot image with the Read tool so you can see the annotations. Confirm your
understanding of the issue with the user before dispatching to artists.

---

## Step 6 — Dispatch to artist agents

Classify the issue type:
- **Geometry** (wrong shape, missing polygons, wrong scale, overflow outside tile boundary,
  wrong LOD polygon count, wrong axis orientation) → `graphics-artist-3d-model`
- **Texture / UV** (wrong texture, seams, UV misalignment, missing material, wrong atlas tile) →
  `graphics-artist-2d-texture`
- **Both** → dispatch to both agents in parallel

Spawn the relevant agent(s) with:
- The model name and which LOD(s) are affected
- The annotated screenshot(s) (pass file paths so the agent can read them)
- A clear plain-language description of the problem
- The relevant spec file(s) as context:
  - `architecture/asset-standards/3d-model-standards.md`
  - `architecture/asset-standards/2d-texture-standards.md`
  - `architecture/asset-standards/building-atlas-layout.md`
  - `architecture/graphics-architecture/model-validator-tool.md` (for tile boundary and scale rules)
- The asset directory: `assets/3d/`
- Instruction: update the asset file(s) in `assets/3d/` directly

Wait for the agent(s) to finish.

---

## Step 7 — Re-review

After the artist(s) have updated the asset, re-run the validator for the same model:

```bash
./build/aitown_model_validator --model <model_name>
```

Assets are loaded from disk at runtime — no rebuild is needed after an artist updates a `.b3d`
or texture file.

Go back to Step 4. Repeat until the user says "approved".

---

## Step 8 — Next model (full review only)

Once approved, move to the next model in the list. Tell the user which model you're moving to
and launch the validator for it.

When all models are done, summarise: how many were approved immediately, how many needed fixes,
and which artist agents were involved.

---

## Full Asset List (review order)

All assets live in `assets/3d/`. File naming: `<name>_lod0.b3d`, `<name>_lod1.b3d`,
`<name>_lod2.b3d` (or `<name>_billboard.dds` for Low/Med billboard LOD2).

**Residential Low** — res_low_01, res_low_02, res_low_03, res_low_04
**Residential Med** — res_med_01, res_med_02, res_med_03, res_med_04
**Residential High** — res_high_01, res_high_02, res_high_03, res_high_04
**Commercial Low** — com_low_01, com_low_02, com_low_03, com_low_04
**Commercial Med** — com_med_01, com_med_02, com_med_03, com_med_04
**Commercial High** — com_high_01, com_high_02, com_high_03, com_high_04
**Industrial Low** — ind_low_01, ind_low_02, ind_low_03, ind_low_04
**Industrial Med** — ind_med_01, ind_med_02, ind_med_03, ind_med_04
**Industrial High** — ind_high_01, ind_high_02, ind_high_03, ind_high_04
**Service** — svc_fire_station, svc_police_station, svc_power_plant, svc_water_tower
**Vehicles** — car_sedan, car_hatchback, car_suv, bus_standard, truck_cargo

Total: 45 models.

---

## Polygon budget quick reference (for briefing artists)

| Asset type | LOD0 | LOD1 | LOD2 |
|---|---|---|---|
| Small buildings (Low/Med all zones) | 1,500–3,000 tris | 200–400 tris | Billboard |
| Large buildings (High all zones) | 4,000–8,000 tris | 1,000–1,500 tris | 400–600 tris |
| Commercial High (Skyscrapers) | 7,000–10,000 tris | 1,200–2,000 tris | 500–700 tris |
| Service buildings | 2,000–4,000 tris | 200–400 tris | Billboard |
| Cars | ≤2,000 tris | ≤400 tris | Sprite |
| Bus / Truck | ≤3,000 tris | ≤500 tris | Sprite |

Tile boundary: buildings must not overflow the **10×10 m** red square shown in the validator.
Scale: all buildings are rendered at **10×10×10 m** in-game.
Axis: **Y-up, Z-forward, left-handed** (Irrlicht convention — NOT Blender's right-handed default).
