# Terrain Interaction

- **Non-buildable slope**: Slopes where `atan(height_delta / cell_width) > 15.0°` (exact; no approximation) are non-buildable without earthworks.
- **Earthworks cost**: `cost_per_tile = $500 × slope_severity_factor` where `slope_severity_factor = clamp((slope_degrees − 15) / 30, 0, 2)` (scales from 0 at exactly 15° to 2 at 45°+). Earthworks are applied automatically at zone placement and deducted from treasury immediately.
- **Earthworks is treasury-only through Phase 9b**: The earthworks mechanic deducts cost
  from the treasury. Through Phase 9b the terrain mesh and heightmap are not modified; the
  terrain remains visually bumpy after placement and only the financial cost is charged.
  **Phase 10b delivers terrain mesh modification**: `setTileHeight()` on `ITerrainQuery`,
  a `TerrainSystem` heightmap write path, and `rebuildTerrainChunk()` triggered on all
  affected chunks. The earthworks treasury deduction and `sfx_earthworks` audio cue remain
  unchanged; Phase 10b adds the visual terrain change on top of the existing mechanic. Do
  not implement terrain mesh modification in Phase 9b or earlier than Phase 10b.
- **Map playability guarantee**: The procedural terrain generator must guarantee **at least 20% of total map tiles are flat or sub-15° slope** to ensure viable starting conditions on all difficulty tiers. The generator re-seeds if this constraint is not met after generation. **Minimum contiguous flat area**: the generator must also guarantee at least **one contiguous region of flat-or-sub-15° tiles of minimum 50×50 tiles** (2,500 tiles) — this ensures the player always has a viable starting district large enough for roads + 2 residential zones + 1 commercial zone + 1 power plant + 1 water tower, even on mountainous maps. The generator re-seeds if this contiguous-area constraint is not met. **Re-seed attempt cap**: the generator will attempt at most **100 re-seeds** before giving up (this ceiling is a safety guard against infinite loops — correctly authored terrain noise should satisfy both constraints within a small number of attempts on any practical seed). **Exhaustion fallback**: if all 100 attempts fail to produce a valid map, the generator logs an error, issues the best-effort map from the final attempt, and the game displays a one-time non-blocking toast notification: "This map has limited flat terrain — consider a new seed if placement is difficult." The simulation proceeds normally; no crash or hard error occurs. This fallback is unreachable under normal operating conditions and exists solely as a robustness guard.

## Placement Feedback

### Zone placement feedback

When `ICitySimulation::placeZone()` succeeds, the following player-visible feedback must occur
in the same frame as the click:

1. **Zone colour overlay**: `UIManager` inserts the tile's key (`tileZ * mapTilesX + tileX`)
   into its sparse `m_overlayMap` with the zone-type ARGB colour (`kOverlayArgbResidential`,
   `kOverlayArgbCommercial`, or `kOverlayArgbIndustrial` — see
   `architecture/ui-ux/hud-layout.md` and `architecture/game-design/zoning-system.md`) and
   immediately calls `IRenderer::setZoneOverlay(mapTilesX, mapTilesZ, m_overlayMap)`. The
   semi-transparent colour quad is rendered over the tile on the same frame. **Prerequisite**:
   `UIManager::setMapDimensions(mapTilesX, mapTilesZ)` must be called from `main.cpp` after
   terrain generation completes, before the first frame of gameplay. If `setMapDimensions` has
   not been called (both dimensions are 0), the overlay update is skipped silently — the
   simulation state is still updated, but no visual confirmation appears. `main.cpp` must not
   skip this call or delay it past the first rendered frame.
2. **Build audio cue**: `CitySimulation::placeZone()` triggers `SFX_BUILD_PLACE` via
   `IAudioSystem::playPositionalSound()` at the tile's world position. If earthworks were
   required, `SFX_EARTHWORKS` is played first (before `SFX_BUILD_PLACE`).
3. **Unsaved-changes indicator**: `UIManager` sets the unsaved-changes dot visible via
   `setUnsavedChanges(true)` after each successful placement.

### Road placement feedback

When `ICitySimulation::placeRoad()` succeeds, the following player-visible feedback must occur:

1. **No colour overlay**: roads do not receive a zone overlay quad. The road tile appearance
   is conveyed entirely by the 3D road mesh placed by the renderer (Phase 9a) rather than a
   2D overlay. No `m_overlayMap` entry is written for road tiles.
2. **Build audio cue**: `CitySimulation::placeRoad()` triggers `SFX_ROAD_BUILD` via
   `IAudioSystem::playPositionalSound()`. If earthworks were required, `SFX_EARTHWORKS` is
   played first.
3. **Unsaved-changes indicator**: `UIManager` sets the unsaved-changes dot visible via
   `setUnsavedChanges(true)` after each successful road placement.

**Important**: the absence of a colour overlay for road tiles means the player has no 2D
overlay confirmation that a road was placed (unlike zone tiles). The sole visual confirmation
is the 3D road mesh. Until Phase 9a road mesh rendering is complete, road placement produces
only the audio cue and the unsaved-changes dot. This is expected behaviour for Phase 9b.

## Phase 10b: Terrain Mesh Modification on Placement

### Buildings and service buildings — full flattening

`IrrlichtRenderer` placement helpers `placeBuildingMesh` and `placeServiceBuildingMesh`
flatten all 4 corner vertices of the placed tile to the same average height before
creating the scene node. The call sequence is:

1. Read all 4 tile-corner heights:

   ```cpp
   const float h00 = m_terrain ? m_terrain->getHeightAt(tileX,     tileZ)     : 0.0f;
   const float h10 = m_terrain ? m_terrain->getHeightAt(tileX + 1, tileZ)     : 0.0f;
   const float h01 = m_terrain ? m_terrain->getHeightAt(tileX,     tileZ + 1) : 0.0f;
   const float h11 = m_terrain ? m_terrain->getHeightAt(tileX + 1, tileZ + 1) : 0.0f;
   const float targetH = (h00 + h10 + h01 + h11) * 0.25f;
   ```

2. Flatten all 4 corners to `targetH` (null-guarded):

   ```cpp
   if (m_terrain) {
       m_terrain->setTileHeight(tileX,     tileZ,     targetH);
       m_terrain->setTileHeight(tileX + 1, tileZ,     targetH);
       m_terrain->setTileHeight(tileX,     tileZ + 1, targetH);
       m_terrain->setTileHeight(tileX + 1, tileZ + 1, targetH);
   }
   ```

   Each `setTileHeight()` call writes the target height into the persistent LOD0
   heightmap at the addressed tile corner and applies neighbour blending to the 8
   surrounding tiles (cardinal neighbours lerped 50% toward `targetH`; diagonal
   neighbours lerped 25% toward `targetH`). All modified tiles' chunks are enqueued for
   rebuild. When `m_terrain` is null (headless unit tests, startup before terrain is
   wired) the flatten steps are skipped.

   **Why 4 calls?** `getHeightAt(tileX, tileZ)` returns the TOP-LEFT vertex height of
   tile `(tileX, tileZ)`. The tile quad has 4 distinct corner vertices — `(tileX, tileZ)`,
   `(tileX+1, tileZ)`, `(tileX, tileZ+1)`, `(tileX+1, tileZ+1)`. Flattening only the
   top-left corner leaves the other 3 at their original heights, producing visible
   T-junction seams at every tile edge where the flat mesh meets the un-flattened terrain
   geometry. Calling `setTileHeight` on all 4 corner coordinates ensures the full tile
   quad is planar before the mesh is placed.

3. Flush all pending chunk rebuilds synchronously so the terrain geometry reflects
   the new heightmap before the scene node is positioned:

   ```cpp
   if (m_terrain) m_terrain->flushTerrainRebuilds();
   ```

   Without this flush, `TerrainSystem::update()` processes at most 2 chunk rebuilds per
   frame. For several frames after placement the terrain geometry still shows the original
   (pre-flatten) heights, making the structure appear sunken.

4. Use `targetH` directly for node Y positioning — **do NOT call `getHeightAt()` here**:

   ```cpp
   // Use targetH directly — NOT getHeightAt() after setTileHeight().
   // setTileHeight() applies neighbour blending to the 8 surrounding tiles;
   // subsequent corner calls bleed back into vertex (tileX, tileZ), leaving
   // its stored height below targetH. getHeightAt() would return that
   // blended-down value and position the node below the rendered terrain surface.
   const float postY = m_terrain ? targetH : 0.0f;
   // Roads, buildings, service buildings: postY + 0.25f
   // (25 cm offset covers tile-edge height interpolation bleed-back after neighbour
   //  blending; polygon offset on the material is the primary Z-fighting defence —
   //  see step 5 below; canonical constant: kRoadSurfaceYBias in
   //  src/rendering/render_constants.h)
   node->setPosition(irr::core::vector3df(
       static_cast<float>(tileX) * kTileSize + kTileSize * 0.5f,
       postY + 0.25f,
       static_cast<float>(tileZ) * kTileSize + kTileSize * 0.5f));
   ```

   **Why `targetH` and not `getHeightAt()`?** Each `setTileHeight(cornerX, cornerZ, targetH)`
   call applies neighbour blending to the 8 surrounding tiles. When the 4 corner calls are
   issued in sequence, each subsequent call treats the previously-set corner as a neighbour
   and blends it 50% back toward its original value. This leaves vertex `(tileX, tileZ)`
   at a height lower than `targetH` by the time all 4 calls complete. Calling
   `getHeightAt(tileX, tileZ)` at that point returns the blended-down value, not `targetH`,
   and positions the node below the rendered terrain surface — exactly the sinking bug that
   `flushTerrainRebuilds()` alone cannot fix. Using `targetH` directly bypasses the bleed-back
   entirely, since that is the exact height to which all 4 terrain vertices were intended to
   be set. When `m_terrain` is null, `postY` falls back to `0.0f`.

   **Why a pure Y offset is insufficient**: With `nearClip = 0.1f` and `farClip = 3000.0f`,
   24-bit depth buffer precision degrades as Z² — approximately
   `ΔZ ≈ 2 × Z² × (far − near) / (near × far × 2²⁴)`. At 400 m this yields ~7 cm; at
   500 m ~11 cm. A fixed Y offset that is sufficient at 300 m is swallowed by depth
   imprecision at 400–500 m, causing intermittent Z-fighting that is impossible to cure
   with a larger constant offset alone (making the offset large enough to prevent all
   Z-fighting at 500 m would visually float the mesh above the terrain at close range).
   Polygon offset (step 5) is the correct, distance-independent solution.

5. **Apply polygon offset to every material slot** on the road/building scene node:

   ```cpp
   mat.PolygonOffsetDirection = irr::video::EPO_FRONT;  // push toward camera
   mat.PolygonOffsetFactor    = 1;                       // combined slope+constant factor (0=off, 1–7)
   ```

   `EPO_FRONT` causes Irrlicht to pass a negative factor to `glPolygonOffset`, which
   subtracts a small bias from the fragment depth before the depth test. This shifts the
   rendered surface "closer" to the camera in clip space without changing the visual
   geometry. The effect is distance-independent because the GPU applies the offset in
   window-space depth units (not world-space metres). Roads, zone buildings, and service
   buildings all use these same settings.

   Note: this Irrlicht version (1.8.x, vcpkg port) exposes only `PolygonOffsetFactor`
   (a 3-bit field, range 0–7) and `PolygonOffsetDirection`. There is no separate
   `PolygonOffsetUnits` field; the single factor controls the combined depth bias.

This pattern guarantees the placed structure is always visually flush with the terrain
surface. Neighbour blending prevents hard seams at tile boundaries. The critical invariant
is that `postY` must be `targetH` — not `getHeightAt(tileX, tileZ)` after the four corner
writes — because the blending walk-back from subsequent corners corrupts the top-left
vertex height. The earthworks treasury deduction and `sfx_earthworks` audio cue
(introduced in earlier phases) are unchanged; Phase 10b adds only the visual terrain
change.

**Z-fighting defence summary**: the placement helpers use two complementary mechanisms:

- A **25 cm Y offset** (`postY + 0.25f`) to handle the residual case where tile-edge
  terrain vertices are fractionally above `targetH` after neighbour blending completes.
- **Polygon offset** (`EPO_FRONT`, factor=1, units=4) applied to every material slot,
  which provides a distance-independent depth bias that prevents Z-fighting at all camera
  distances (pure Y offsets fail beyond ~400 m where depth precision degrades below 10 cm
  for a 24-bit buffer with near=0.1, far=3000).

### Multi-tile footprint extension

For buildings with an N×N footprint (N > 1 — Medium density: N=2, High density: N=3,
Service buildings: N=2), `setTileHeight()` must be called for **all `(N+1) × (N+1)`
corner vertices** spanning the full footprint, not only the 4 corners of the 1×1 origin
tile. Flattening only the origin tile leaves the remaining footprint tiles at their
original terrain heights, causing the building's ground plate to intersect raised terrain
edges.

The target height `targetH` for the entire footprint is computed from the 4 **outermost**
corners of the full footprint:

```cpp
const float h_NW = m_terrain->getHeightAt(footX,       footZ);
const float h_NE = m_terrain->getHeightAt(footX + N,   footZ);
const float h_SW = m_terrain->getHeightAt(footX,       footZ + N);
const float h_SE = m_terrain->getHeightAt(footX + N,   footZ + N);
const float targetH = (h_NW + h_NE + h_SW + h_SE) * 0.25f;
```

Then call `setTileHeight(cx, cz, targetH)` for every corner vertex
`cx ∈ [footX, footX+N]`, `cz ∈ [footZ, footZ+N]`:

```cpp
if (m_terrain) {
    for (int cz = footZ; cz <= footZ + N; ++cz)
        for (int cx = footX; cx <= footX + N; ++cx)
            m_terrain->setTileHeight(cx, cz, targetH);
    m_terrain->flushTerrainRebuilds();  // single flush after all writes
}
```

A Low-density building (N=1) produces the same 4 calls as the original spec above —
the loop collapses to 4 iterations and behaviour is unchanged for Low density.
`flushTerrainRebuilds()` is called **once** after the full loop, not once per tile.

The scene node Y position uses `targetH` directly (not `getHeightAt()` after
`setTileHeight()` calls) per the bleed-back rule above — this applies to the full
footprint: use the single `targetH` value computed from the outermost corners.

For the full `setTileHeight()` implementation spec (blending formula, chunk rebuild
enqueue, bounds clamping, rebuild budget interaction) see
`architecture/graphics-architecture/procedural-terrain.md` — `setTileHeight()` Write
Path and Neighbour Blending (Phase 10b).

### Roads — sloped terrain-conforming mesh

Roads use a different placement strategy from buildings. Rather than always flattening
to a single average height, roads follow the terrain up to a maximum 15° incline. Full
flattening is only applied when the natural terrain slope exceeds this limit.

#### Slope calculation

The tile slope is derived from the maximum height difference across the tile diagonal
divided by the diagonal cell distance:

```text
maxDelta = max(|h11 - h00|, |h10 - h01|)
slopeDeg = atan(maxDelta / (kTileSize * sqrt(2))) * (180 / PI)
```

`kTileSize` is the world-space side length of one tile (in metres). The two diagonal
differences are checked because a tile is a quad with two triangles — either diagonal
can be the steepest cross-section. The largest is the conservative bound.

#### Conditional flattening

- If `slopeDeg <= 15.0°`: **no flattening**. The 4 corner heights `h00`, `h10`, `h01`,
  `h11` are used as-is. `setTileHeight()` is not called. The road mesh is built directly
  on the natural terrain heights.
- If `slopeDeg > 15.0°`: **minimum-adjustment flattening**. The goal is to find the
  smallest vertical shift applied uniformly to all 4 corners that brings the slope to
  exactly 15°. This is achieved by computing the average height and then clamping the
  per-corner deviation so that no diagonal exceeds the 15° limit:

  ```text
  avgH     = (h00 + h10 + h01 + h11) * 0.25f
  maxAllowed = kTileSize * sqrt(2) * tan(15° in radians)
  ```

  Each corner height is clamped to `[avgH - maxAllowed/2, avgH + maxAllowed/2]`. This
  preserves the average elevation (no net vertical shift of the tile) while guaranteeing
  that neither diagonal exceeds 15°. The clamped corner heights `c00`, `c10`, `c01`,
  `c11` are then written back with `setTileHeight()`:

  ```cpp
  if (m_terrain) {
      m_terrain->setTileHeight(tileX,     tileZ,     c00);
      m_terrain->setTileHeight(tileX + 1, tileZ,     c10);
      m_terrain->setTileHeight(tileX,     tileZ + 1, c01);
      m_terrain->setTileHeight(tileX + 1, tileZ + 1, c11);
  }
  ```

  The same `flushTerrainRebuilds()` call (step 3 of the building path) must follow.

#### Terrain-conforming road mesh

The road mesh is built as a per-tile quad with vertex Y positions taken from the 4
actual corner heights after the conditional-flatten step above. The mesh is not a
flat quad at a single Y; it follows the terrain surface. Vertices are positioned at:

```text
V00 = (tileX * kTileSize,       c00 + 0.25f, tileZ * kTileSize)
V10 = ((tileX+1) * kTileSize,   c10 + 0.25f, tileZ * kTileSize)
V01 = (tileX * kTileSize,       c01 + 0.25f, (tileZ+1) * kTileSize)
V11 = ((tileX+1) * kTileSize,   c11 + 0.25f, (tileZ+1) * kTileSize)
```

The 0.25f Y offset (`kRoadSurfaceYBias` in `src/rendering/render_constants.h`) is
applied per-vertex (not to the scene node position), for the same Z-fighting reason
as buildings. The scene node origin is placed at the tile centre
at the average of the 4 corner heights; vertices carry the per-corner offsets relative
to that origin.

Polygon offset (`EPO_FRONT`, factor=1) is applied to every material slot on the road
mesh node, identical to buildings.

#### Neighbor edge matching

When placing a road tile at `(tileX, tileZ)`, the 4 cardinal neighbors are checked
immediately after the conditional-flatten step:

- **North**: `(tileX, tileZ - 1)`
- **South**: `(tileX, tileZ + 1)`
- **East**: `(tileX + 1, tileZ)`
- **West**: `(tileX - 1, tileZ)`

Shared-edge continuity is guaranteed by the fact that both tiles reference the same
`m_generatedHeightmap` vertices. For example, the shared edge between this tile and
its North neighbor consists of vertices `(tileX, tileZ)` and `(tileX+1, tileZ)`. When
`setTileHeight(tileX, tileZ, c00)` was called for this tile, those heightmap entries
were updated. The North neighbor's South edge reads the same heightmap entries, so the
geometry already matches at the shared edge — no separate coordinate negotiation is
needed.

However, the neighbor's **rendered road mesh** is now stale: it was built using the
old pre-flatten heights. Any cardinal neighbor that already has a road tile must have
its road mesh rebuilt to reflect the freshly-written heightmap values. The sequence is:

1. Flush terrain rebuilds for the current tile (`flushTerrainRebuilds()`).
2. For each cardinal direction `d` in `{N, S, E, W}`:
   - If `ICitySimulation::hasTile(nx, nz)` returns a road tile at `(nx, nz)`:
     - Call `IrrlichtRenderer::removeRoadMesh(nx, nz)` to remove the stale mesh.
     - Call `IrrlichtRenderer::placeRoadMesh(nx, nz)` to rebuild it from current
       heightmap values. This recursive call will itself read the updated corner heights,
       evaluate the slope, and reconstruct the terrain-conforming quad.

The recursive `placeRoadMesh` calls on neighbors do not themselves trigger further
neighbor propagation — propagation is one level deep (only immediate cardinal neighbors
of the originally-placed tile are rebuilt). This prevents unbounded cascading rebuilds
across a large road network. In practice, the 4-call limit ensures at most 4 extra mesh
rebuilds per road placement, which is acceptable.

**Note on corner ownership and blending**: `setTileHeight()` applies neighbour blending
to the 8 surrounding tiles of each written corner. This means that writing `c00` at
`(tileX, tileZ)` also partially adjusts the surrounding terrain. The cardinal road
neighbors are among those 8 surrounding tiles. Their terrain vertices are therefore
already partially updated by the blending walk; the `removeRoadMesh` + `placeRoadMesh`
rebuild on each road neighbor is still required to re-emit mesh geometry that matches
the final heightmap state.
