# Terrain Interaction

- **Non-buildable slope**: Slopes where `atan(height_delta / cell_width) > 15.0°` (exact; no approximation) are non-buildable without earthworks.
- **Earthworks cost**: `cost_per_tile = $500 × slope_severity_factor` where `slope_severity_factor = clamp((slope_degrees − 15) / 30, 0, 2)` (scales from 0 at exactly 15° to 2 at 45°+). Earthworks are applied automatically at zone placement and deducted from treasury immediately.
- **Earthworks is treasury-only in V1**: The earthworks mechanic deducts cost from the treasury. It does **NOT** modify the terrain mesh or heightmap. The terrain remains visually bumpy after placement; only the financial cost is charged. This is intentional V1 scope. A future phase that adds terrain mesh modification would require: (1) a `setTileHeight()` method on `ITerrainQuery`, (2) a `TerrainSystem` heightmap write path, and (3) `rebuildTerrainChunk()` triggered on the affected chunk. Do not implement terrain mesh modification in Phase 9b or earlier.
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
