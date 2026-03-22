## Phase 11j: Upgrade Unzoning Fix & Ground Plate Terrain Clearance

**Status: DONE**

### Goal

Three targeted runtime bug fixes identified during Phase 11h visual QA:

1. **Upgrade unzoning** — When a density upgrade demolishes a lower-density neighbour
   whose footprint extends outside the new high-density footprint, the outer tiles were
   incorrectly marked `isZoned = false` and never re-marked.  The player's zone
   designation was permanently erased for those tiles.

2. **Outer tile invisible after upgrade** — Outer tiles kept as `isZoned = true`
   had no building mesh placed.  They appeared as bare terrain in the query panel and
   had no zone-colour indicator (green / yellow / blue), making it impossible for the
   player to see which zone type they held.

3. **Ground plate terrain intersection** — Zone building nodes were placed at
   `postY + 0.01f` (1 cm above the flattened terrain surface).  The ground quad in
   each B3D model sits at `y = 0.01` in model space, giving a total world clearance
   of only 2 cm.  Terrain vertex blending at footprint edges caused Z-fighting and
   visible intersection artefacts.  Service buildings already used `postY + 0.10f`
   (10 cm) and were unaffected.

---

### Deliverables

#### 1. Simulation fix — upgrade unzoning (`src/simulation/CitySimulation.cpp`)

- [x] In `doDensityUnlockTick`, the demolition loop that clears a demolished
  neighbour's `oldN × oldN` footprint now checks each tile against the new
  `tx … tx+newN-1`, `tz … tz+newN-1` upgrade footprint:
  - **Inside new footprint**: `isZoned = false` (re-marking loop below will restore
    `true`; setting `false` prevents stale visible state during the same tick).
  - **Outside new footprint**: `isZoned = true`, `density = DensityTier::Low`,
    `footprintOriginX = -1`, `footprintOriginZ = -1`, `population = 0`,
    `isAbandoned = false`.  Zone type is preserved so the player's designation
    survives.
- [x] No change to the re-marking loop (`tx … tx+newN-1` range) — it already sets
  `isZoned = true` for all footprint tiles and will correctly overwrite any tile that
  falls inside the new footprint.

#### 2. Simulation fix — outer tile building mesh (`src/simulation/CitySimulation.cpp`)

- [x] After the demolition loop in `doDensityUnlockTick`, outer tiles (tiles of a
  demolished neighbour that fall outside the new upgrade footprint) are collected in
  a `std::vector<OuterTile>` during the tile-clear pass.
- [x] After all `toDemo` entries are processed, each outer tile receives a fresh
  `DensityTier::Low` building mesh via `m_renderer->placeBuildingMesh()` using the
  tile's preserved zone type and the existing round-robin variant counter.  This
  gives the tile the same visual appearance as a newly placed Low-density zone —
  the zone-colour indicator (green / yellow / blue) becomes visible to the player
  and the query panel reports it correctly.

#### 3. Renderer fix — ground plate clearance (`src/rendering/IrrlichtRenderer.cpp`)

- [x] Zone building node Y position changed from `postY + 0.01f` to `postY + 0.05f`
  (5 cm clearance).  This matches the half-way point between the old 1 cm and the
  service building 10 cm, providing enough separation to eliminate Z-fighting at all
  density tiers without lifting small Low-density buildings noticeably off the ground.

---

### Exit Criteria

- [x] Full unit test suite passes: `100% tests passed, 0 tests failed out of 1169`.
- [x] No regressions in simulation, terrain, UI, or audio tests.

---

### Sign-offs

| Role | Area | Status |
|---|---|---|
| `graphics-dev-irrlicht` | Ground plate Y offset | ✅ |
| `gamedesign-lookandfeel` | Upgrade unzoning logic | ✅ |
| `test-dev-cpp` | Test suite green | ✅ |
