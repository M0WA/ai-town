## Phase 11o: Source Code Clean-Up and C++ Best Practices

**Status: OPEN**

### Goal

Apply improvements identified in the tech-squad code review of `src/` (2026-03-30).
The review covered rendering, audio, simulation, UI, and the build/CI subsystems,
producing structured proposals across five domains (rendering, audio, simulation, UI, build/CI),
with additional improvements identified in a subsequent deep-dive review of `src/`, `tests/`,
and CI workflows. A third pass focused specifically on splitting large functions (>60 lines) and converting multi-branch if/else-if chains to `switch` statements or constexpr lookup tables. This phase implements the
HIGH-priority proposals in full and the MEDIUM-priority proposals as secondary
deliverables. LOW-priority proposals are listed as non-blocking stretch goals.

Every deliverable in this phase is a **source-only or build-system edit** — no
architecture spec files are touched (downstream spec updates, if needed, are
captured as open items in the exit criteria).

**Traceability**: each item references its proposal ID (e.g. `REN-1`).

---

### Deliverables

---

#### 1. Rendering clean-up (Group A)

**Files:** `src/rendering/IrrlichtRenderer.cpp`, `src/rendering/IrrlichtRenderer.h`,
`src/rendering/TextureCache.cpp`, `src/rendering/TextureCache.h`,
`src/rendering/SceneEntityManager.cpp`, `src/rendering/render_constants.h`,
`src/rendering/LODNode.h`, `src/rendering/RenderSystem.h`,
`src/terrain/TerrainSystem.cpp`, `src/terrain/TerrainSystem.h`

##### HIGH — correctness / significant duplication

- [ ] **A-1** _(REN-4 + REN-10)_ The `m_driver->setMaterial(SMaterial{})` flush call is
  **missing** from `destroyTileNode()`, `destroyVehicleNode()`, and `rebuildTerrainChunk()`
  and must be **added** immediately after the texture-slot-clearing loop (labelled "Step 2"
  in each function). Do not treat this as a verification task — the calls do not exist yet.
  The same call is already present in `clearCity()` and `despawnVehicleAgent()`; these
  three functions silently omit it, violating the eviction contract in
  `scene-graph-ownership.md`. _(REN-4, REN-10)_

- [ ] **A-2** _(REN-1 + REN-3)_ Extract a private helper template
  `evictLODNodeRegistry<KeyT>(std::unordered_map<KeyT, LODNode*>& registry)` that
  encapsulates the material-slot-clear / `setMaterial(SMaterial{})` / `node->remove()` /
  `delete` / `clear()` sequence. Replace the seven copy-pasted eviction loops in the
  destructor, `clearCity()`, `destroyTileNode()`, `destroyVehicleNode()`, and
  `despawnVehicleAgent()` with calls to this helper. _(REN-1, REN-3)_

- [ ] **A-3** _(REN-5)_ Promote the `extractBasename` lambda in `TextureCache::loadSRGB()`
  to a file-scope static helper function. Add a companion `extractDirectory()` helper.
  Replace the duplicated inline path-splitting block in the atlas-redirect branch with
  calls to these helpers. _(REN-5)_

- [ ] **A-4** _(REN-2)_ Extract `flattenFootprint(tileX, tileZ, footprintN, outTargetH)` and
  `applyBuildingMaterialDefaults(node, prefix)` private helpers to de-duplicate the
  ~80 shared lines between `placeBuildingMesh()` and `placeServiceBuildingMesh()`. _(REN-2)_

- [ ] **A-5** _(REN-6)_ Extract `appendTerrainQuad(buf, tx, tz, colour, yOffset)` and
  paired `openOverlayBuffer` / `closeOverlayBuffer` helpers to remove the four independent
  re-implementations of the SMesh quad-building pattern in `setZoneOverlay()`,
  `setTilePlacementPreview()` (×2), and `showServiceCoverageOverlay()`. _(REN-6)_

- [ ] **A-6** _(REN-9)_ Add a file-scope `static irr::video::SColor argbToSColor(uint32_t argb)`
  helper in `IrrlichtRenderer.cpp` and replace the four separate ARGB bit-shift/mask blocks
  with calls to it. _(REN-9)_

- [ ] **A-21** _(REN-26)_ Remove `using namespace irr;` from `SceneEntityManager.h` (file scope
  in a header); replace all unqualified `irr` names in the header with fully-qualified names
  (e.g. `irr::scene::ISceneNode*`). Every TU that includes `SceneEntityManager.h` silently
  imports the entire `irr` namespace, creating name-collision risk. _(REN-26)_

- [ ] **A-22** _(REN-27)_ Extract `void logWarning(const std::string& msg)` and
  `void logError(const std::string& msg)` private helpers in `IrrlichtRenderer` to
  eliminate the 15+ copies of the two-branch null-guard + `fprintf` fallback pattern
  (`if (m_logger) { m_logger->log(...); } else { fprintf(stderr, ...); }`). _(REN-27)_

- [ ] **A-33** _(REN-36)_ Split `IrrlichtRenderer::placeRoad()` (~170 lines) into three
  private helpers: `flattenRoadTerrain(tileX, tileZ)`, `rebuildRoadNeighbors(tileX, tileZ)`,
  and `buildRoadSceneNode(tileX, tileZ)`, so each method has a single responsibility. _(REN-36)_

##### MEDIUM — best practices / simplification

- [ ] **A-7** _(REN-12)_ Replace the nested ternary `(footprintSize < 1) ? 1 : (footprintSize > 3) ? 3 : footprintSize`
  with `std::clamp(footprintSize, 1, 3)` (C++17). _(REN-12)_

- [ ] **A-8** _(REN-13)_ Replace the two `while`-loop heading normalisations in
  `moveVehicleAgent()` with `std::fmod(headingDeg, 360.0f); if (h < 0.0f) h += 360.0f;`. _(REN-13)_

- [ ] **A-9** _(REN-14)_ Replace the per-tile `std::sqrt` call in `showServiceCoverageOverlay()`
  with a squared-distance comparison; hoist `const float radiusSq = radiusM * radiusM`
  before the loop. _(REN-14)_

- [ ] **A-10** _(REN-11)_ Remove the redundant post-loop `node->getMaterial(0).setTexture(0, vehicleTex)`
  call in `spawnVehicleAgent()` that contradicts the conditional loop immediately above it. _(REN-11)_

- [ ] **A-11** _(REN-8)_ Change the signal node key in `setIntersectionSignalState()` from
  `int key = tileX * 10000 + tileZ` to `uint64_t key = tileKey(tileX, tileZ)`, matching
  all other tile-keyed registries. _(REN-8)_

- [ ] **A-12** _(REN-17)_ Promote the cloud dome geometry constants (`kDomeRings`,
  `kDomeSectors`, `kCloudDomeRadius`, `kCloudAltitude`, `kCloudDomeHeight`,
  `kCloudUVScale`, `kCloudScrollX`, `kCloudScrollZ`) from their local-constexpr positions
  in `buildCloudDomeMesh()` and `update()` to a named group in `render_constants.h`
  under a `namespace CloudDome`. _(REN-17)_

- [ ] **A-13** _(REN-18)_ Define `static constexpr float kDegToRad = static_cast<float>(M_PI / 180.0)`
  near the top of `IrrlichtRenderer.cpp` and replace the inline cast expression in `setFOV`. _(REN-18)_

- [ ] **A-14** _(REN-7)_ Move the road LOD distance constants (`kRoadLOD0to1`,
  `kRoadLOD1to2`, `kRoadCullDist`) from their local-static-with-`(void)`-suppression
  position in `placeRoadMesh()` to `render_constants.h`, or remove them entirely with
  a TODO comment if they are intentional future placeholders. _(REN-7)_

- [ ] **A-15** _(REN-15)_ Either use the per-mip `estimatedVRAM` accumulator as the stored
  VRAM estimate in `TextureCache::loadSRGB()`, or remove the unused local variable and
  document why `estimateDXTVRAM` is preferred. _(REN-15)_

- [ ] **A-16** _(REN-16)_ Guard test-only accessors in `IrrlichtRenderer.h`
  (`terrainMaterialTypeForTest`, `setTerrainMaterialTypeForTest`, `cloudNodeForTest`)
  with `#ifdef AITOWN_TESTING` or `[[deprecated("for tests only")]]` to prevent
  accidental production use. Group them under a clearly labelled `// TEST API` section. _(REN-16)_

- [ ] **A-17** _(REN-24)_ Name the overlay Y-offset magic literals in
  `render_constants.h`: `kHoverQuadYOffset`, `kOverlayYOffset`, `kZoneOverlayYOffset`,
  `kCoverageOverlayYOffset`, `kCenterlineYBias`. Replace the five anonymous float
  literals in `IrrlichtRenderer.cpp`. _(REN-24)_

- [ ] **A-18** _(REN-20)_ Hoist the repeated `std::string(AITOWN_ASSETS_DIR) + "/3d/vehicles/"` prefix
  out of the `vehicleMeshPath()` switch arms into a single `const std::string prefix` variable. _(REN-20)_

- [ ] **A-23** _(REN-28)_ Replace all `unordered_map::count(k) > 0` / `unordered_set::count(k) > 0`
  patterns with `::contains(k)` (C++20) across `IrrlichtRenderer.cpp`, `SceneEntityManager.cpp`,
  and `TerrainSystem.cpp` (~12 call sites). _(REN-28)_

- [ ] **A-24** _(REN-30)_ Change the three `std::vector<std::string>&` parameters in
  the raw-pointer `SceneEntityManager::destroy()` overload to `const std::vector<std::string>&`
  (the function only reads them). _(REN-30)_

- [ ] **A-25** _(REN-32)_ Extract a `static std::string vehicleAtlasPath(int variantIndex)`
  private helper in `IrrlichtRenderer.cpp`, parallel to the existing `vehicleMeshPath()`, to
  deduplicate the inline atlas-path string construction in `spawnVehicleAgent()`. _(REN-32)_

- [ ] **A-26** _(REN-35)_ Add `[[nodiscard]]` to `ITerrainQuery::getHeightAt()` and
  `getSlopeDegrees()` virtual declarations (pure queries with no side-effects). _(REN-35)_

- [ ] **A-27** _(TER-1)_ Replace the `std::stable_sort` on the full rebuild queue after every
  push in `TerrainSystem::enqueueRebuild()` with `std::lower_bound` + `deque::insert` to avoid
  O(n log n) per-push cost; the deque is maintained sorted by ascending distance. _(TER-1)_

- [ ] **A-28** _(TER-2)_ Replace the 2-branch if-else chains in `TerrainSystem::lodSwitchOutDistance()`
  and `lodSwitchInDistance()` with `static constexpr float` arrays indexed by LOD level for
  clarity and easier extensibility. _(TER-2)_

- [ ] **A-34** _(REN-37)_ Replace the 4-branch if/else-if heading-degree binning in
  `moveVehicleAgent()` with a `switch` on a computed quadrant index (0–3), or a
  `static constexpr` angle-range → direction-enum table, to make the binning logic
  self-documenting. _(REN-37)_

- [ ] **A-35** _(TER-3)_ Replace the 3-branch if/else-if LOD→grid-size mapping in
  `TerrainSystem::processOneRebuild()` with a `static constexpr int kLODGridSizes[]` array
  indexed by LOD level, parallel to the table approach proposed for LOD distances in A-28. _(TER-3)_

##### LOW — optional / stretch

- [ ] **A-19** _(REN-19)_ Define a `struct PoolEntryBase { int ref_count; uint64_t lastAccessTimestamp; size_t vramBytes; }`
  and have `CacheEntry`, `SRGBEntry`, and `SplatEntry` inherit from it to de-duplicate the
  three identical field sets in `TextureCache.h`. _(REN-19)_

- [ ] **A-20** _(REN-21, REN-22, REN-23, REN-25)_ Minor clean-up: add a comment explaining
  the double-set in `setIntersectionSignalState()`; normalise destructor cleanup for
  `m_agentNodes` / `m_coverageOverlayNode`; update `SceneEntityManager::track()` parameters
  to `const std::vector<std::string>&`; add `inline` to `road_lod2_color` in
  `render_constants.h`. _(REN-21, REN-22, REN-23, REN-25)_

- [ ] **A-29** _(REN-29)_ Promote `kBlockedArgb` from a function-local `constexpr` to
  `render_constants.h` alongside the other zone-colour ARGB constants. _(REN-29)_

- [ ] **A-30** _(REN-31)_ Add `[[nodiscard]]` to `LODNode::getCurrentLOD()` and
  `getMesh()` (pure queries, no side-effects). _(REN-31)_

- [ ] **A-31** _(REN-33)_ Add `[[nodiscard]]` to `RenderSystem::maxTextureSize()`,
  `supportsS3TC()`, and `supportsAnisoFiltering()` (device-queried values, no side-effects). _(REN-33)_

- [ ] **A-32** _(REN-34)_ Either make `IRenderer::setZoneHoverColour()` pure-virtual (`= 0`)
  to match all other `IRenderer` methods, or add an explicit comment documenting why the
  default no-op body is intentional so that a concrete subclass cannot silently omit it. _(REN-34)_

- [ ] **A-36** _(REN-38)_ Verify and update the `"Lighting=false: no light nodes in scene
  yet (Phase 6+)"` comment in `spawnVehicleAgent()` — if Phase 6 has not added light nodes,
  document why; if it has, remove the stale note. _(REN-38)_

- [ ] **A-37** _(REN-39)_ Apply the same stale-comment check to the equivalent
  `"no light nodes in the scene yet (Phase 6+)"` comment in `placeBuildingMesh()`. _(REN-39)_

- [ ] **A-38** _(REN-40)_ Remove or update the Phase 5/6 terrain-texture commentary in
  `rebuildTerrainChunk()` that describes texturing as a future addition — texture support is
  already implemented. _(REN-40)_

---

#### 2. Audio clean-up (Group B)

**Files:** `src/audio/AudioSystem.cpp`, `src/audio/AudioSystem.h`,
`src/audio/AudioSourcePool.h`, `src/audio/AudioSourcePool.cpp`,
`src/audio/AudioStream.h`, `src/audio/audio_constants.h`,
`src/audio/al_check.h`, `src/audio/sound_ids.h`, `src/interfaces/sound_ids.h`

##### HIGH — correctness / significant duplication

- [ ] **B-1** _(AUD-1, AUD-2, AUD-3)_ Add `alCheckError()` after every AL call that
  currently has no error check: all `alSourceStop`, `alSourcei(AL_BUFFER, 0)` calls in
  `AudioSourcePool.cpp`; the setup block in `playSound()` and `playPositionalSound()`;
  and the three listener calls in `syncListenerToCamera()`. _(AUD-1, AUD-2, AUD-3)_

- [ ] **B-2** _(AUD-4)_ Extract a private `int acquireSFXSlot(SoundPriority priority)`
  helper that encapsulates the scan-for-free-slot plus priority-based eviction logic
  currently duplicated verbatim in `playSound()` and `playPositionalSound()`. _(AUD-4)_

- [ ] **B-3** _(AUD-11)_ Wire `AudioSystem` to use `AudioSourcePool` exclusively (option B — matches intended architecture). Remove the inline pool state currently duplicated in `playSound()` and `playPositionalSound()`; delegate all source acquisition to `AudioSourcePool`. Record this architectural decision as a comment in `AudioSystem.h`. _(AUD-11)_

- [ ] **B-4** _(AUD-12)_ Delete `src/audio/sound_ids.h` (stale predecessor). Update
  `src/simulation/CitySimulation.cpp` to `#include "src/interfaces/sound_ids.h"`.
  Verify no other TU includes the `src/audio/` version. _(AUD-12)_

- [ ] **B-5** _(AUD-5)_ Define `constexpr float kDuckRampUpSeconds = 0.2f` and
  `constexpr float kDuckRampDownSeconds = 1.5f` in `audio_constants.h`. Replace the
  bare literals in `updateDuckState()`. Derive the release delta as `(1.0f - kMusicDuckGain)`. _(AUD-5)_

- [ ] **B-6** _(AUD-7)_ Define `constexpr float kVehicleEnginePitchIdle = 0.75f` and
  `constexpr float kVehicleEnginePitchFull = 1.35f` in `audio_constants.h`. Replace the
  four bare literals in the init and per-frame blocks of `updateVehicleEngines()`. _(AUD-7)_

- [ ] **B-7** _(AUD-6)_ Fix the `path.substr(path.size() - 4)` pattern in
  `processPreloadCommand()`: replace with a `hasSuffix()` helper that guards against
  short strings and avoids heap allocation. _(AUD-6)_

- [ ] **B-25** _(AUD-28)_ Remove the stale `"Phase 3 stubs — no-op. Phase 7 replaces with real
  AL error checking."` comment and the `/* Phase 7: real impl */` placeholder bodies from
  `al_check.h`; replace with accurate documentation of the actual seam contract. _(AUD-28)_

- [ ] **B-26** _(AUD-29)_ Remove the three-point stale responsibility list inside
  `AudioSystem::update()` that describes work already fully implemented on the audio thread
  (not in `update()`); replace with an accurate one-line description of what `update()` actually
  does (or a note that it is intentionally minimal). _(AUD-29)_

- [ ] **B-27** _(AUD-30)_ Remove the `"NOTE: Phase 10 deliverable … is also a Phase 10
  deliverable and is implemented in the same Phase 10 commit"` implementation-planning note
  from the `setMusicIntensity()` body; it belongs in the git log, not in shipping source. _(AUD-30)_

##### MEDIUM — best practices / simplification

- [ ] **B-8** _(AUD-8)_ Apply `[[nodiscard]]` to all acquire/find/load/decode helpers in
  `AudioSourcePool.h`, `AudioSystem.h`, and `AudioStream.h`. _(AUD-8)_

- [ ] **B-9** _(AUD-10)_ Replace the `goto cleanup_alc` in the `AudioSystem` destructor with
  an `if (m_contextMadeCurrent)` block, removing the `goto` and the label. _(AUD-10)_

- [ ] **B-10** _(AUD-14, AUD-15)_ Extract `startCrossfadeIncomingStream(slot, path, isStem, errorOut)`
  helper to de-duplicate the stream-open-and-play boilerplate shared by
  `beginIntensityCrossfade()` and `beginAmbientCrossfade()`. Extract a `finalizeCrossfade()`
  completion helper to de-duplicate the music and ambient crossfade completion blocks. _(AUD-14, AUD-15)_

- [ ] **B-11** _(AUD-13)_ Merge the first two `m_streamMutex` lock scopes in the main-menu
  EOF path inside `refillStream()`. Extract the entire main-menu EOF handler to a private
  `handleMainMenuEOF(int slot)` method. _(AUD-13)_

- [ ] **B-12** _(AUD-16)_ Cache the `getTotalFrames()` result in a local variable in
  `processPreloadCommand()` and reuse it; add `static_assert` that
  `SFX_ZONE_RESIDENTIAL < SFX_ZONE_INDUSTRIAL`. _(AUD-16)_

- [ ] **B-13** _(AUD-20)_ Forward-declare `class AudioSystem` in `AudioSourcePool.h` and
  change `m_audioSystem` / constructor parameter from `void*` to `AudioSystem*`. _(AUD-20)_

- [ ] **B-14** _(AUD-21)_ Load `m_sfxVolume` once into a local variable before the two gain
  calls in the per-frame vehicle engine update block. _(AUD-21)_

- [ ] **B-15** _(AUD-9)_ Mark `computeSamplesPlayed` `noexcept` and `[[nodiscard]]` in
  `AudioSystem.h`. _(AUD-9)_

- [ ] **B-16** _(AUD-17)_ Add a comment to `loadMusicSidecar()` documenting the parse
  limitations (no whitespace after colon, no duplicate keys, no escaping). Add a
  length-guard to the `extractFloat` / `extractInt` lambdas. _(AUD-17)_

- [ ] **B-17** _(AUD-18)_ Update the `m_stingerLastTriggerTime` comment with a concrete
  index example. Add `static_assert(kStingerCount == 2)` adjacent to the array. _(AUD-18)_

- [ ] **B-18** _(AUD-19)_ Rename `AudioSourcePool::SFXSourceSlot` to `PoolSFXEntry` (or
  similar) to distinguish it from `AudioSystem::SFXSlot`. _(AUD-19)_

- [ ] **B-28** _(AUD-31)_ Update the comment block above `alCheckError_real` / `alcCheckError_real`
  in `AudioSystem.cpp` (lines ~51–58) which misattributes their location by quoting a spec
  line about `al_check.cpp`; the real implementations live in `AudioSystem.cpp` — the comment
  should explain why rather than contradict the file layout. _(AUD-31)_

- [ ] **B-29** _(AUD-32)_ Replace `std::max(0.0f, std::min(1.0f, t))` in
  `applyCrossfadeGains()` with `std::clamp(t, 0.0f, 1.0f)` (C++17, already used elsewhere). _(AUD-32)_

- [ ] **B-30** _(AUD-33)_ Extract a private `void setupNonPositionalSource(int sourceIdx)`
  helper for the four-call sequence (`AL_SOURCE_RELATIVE`, `AL_POSITION`, `AL_ROLLOFF_FACTOR`,
  `AL_VELOCITY`) copy-pasted verbatim in both `setupStingerSource()` and
  `setupStreamSource()`. _(AUD-33)_

- [ ] **B-31** _(AUD-34)_ Define `constexpr` `kAmbientCrossfadeDurationSeconds` separately
  from `kMusicCrossfadeDurationSeconds` in `audio_constants.h` so the two crossfade categories
  can be tuned independently; `beginAmbientCrossfade()` currently reuses the music constant. _(AUD-34)_

- [ ] **B-32** _(AUD-35)_ Reuse `findEvictionCandidate()` (or extract a shared private helper)
  in `AudioSourcePool::acquireVehicleEnginePair()` to eliminate the duplicated
  priority-plus-distance eviction-candidate scan. _(AUD-35)_

- [ ] **B-33** _(AUD-36)_ Move the `AudioStream` struct definition from `AudioSystem.h` to
  `AudioStream.h` to co-locate it with the `AudioStreamUtils` namespace and remove the
  confusing split between the two headers. _(AUD-36)_

- [ ] **B-39** _(AUD-42)_ Extract the bar-boundary tracking block inside `refillStream()`
  (distinct from the main-menu EOF branch covered by B-11) into a private
  `updateBarBoundary(AudioStream& s, uint64_t samplesPlayed, ALint buffersQueued)` method. _(AUD-42)_

- [ ] **B-40** _(AUD-43)_ Extract the duplicate idle-source and move-source AL state
  initialisation sequences in `updateVehicleEngines()` into private helpers
  `setupVehicleIdleSource(ALuint src, float basePitch)` and
  `setupVehicleMoveSource(ALuint src, float basePitch)` to eliminate ~39 lines of
  repeated AL property calls. _(AUD-43)_

- [ ] **B-41** _(AUD-44)_ Extract the per-frame pitch/gain/position update block from
  `updateVehicleEngines()` into a private
  `updateVehicleEngineFrame(const VehicleAudioSlot& slot, ALuint idleSrc, ALuint moveSrc)`
  to separate one-time initialisation from recurring updates in the 144-line function. _(AUD-44)_

##### LOW — optional / stretch

- [ ] **B-19** _(AUD-22)_ Mark all `AudioStreamUtils` functions `[[nodiscard]]` and
  `noexcept`. _(AUD-22)_

- [ ] **B-20** _(AUD-23)_ Replace the `sizeof`-based EFX static asserts with
  `std::is_same_v<LPALGENFILTERS_t, LPALGENFILTERS>`. _(AUD-23)_

- [ ] **B-21** _(AUD-24)_ Move `m_rng` seeding from the member default initialiser to the
  constructor body (after guard flags are set), to avoid exception propagation from
  the default initialiser if `std::random_device` throws. _(AUD-24)_

- [ ] **B-22** _(AUD-25)_ Change logging helper signatures to accept `std::string_view`
  instead of `const std::string&`. _(AUD-25)_

- [ ] **B-23** _(AUD-26)_ Wrap `OggVorbis_File*` in a RAII holder in `openStreamOGG()` so
  the pointer is freed on all exit paths including exception paths. _(AUD-26)_

- [ ] **B-24** _(AUD-27)_ Add
  `static_assert(std::is_trivially_copyable_v<DuckState>, "DuckState must be trivially copyable")`
  in `AudioSystem.cpp`. _(AUD-27)_

- [ ] **B-34** _(AUD-37)_ Rename the `src2` local variable in the main-menu EOF branch of
  `refillStream()` to `mainMenuSrc` to distinguish it from the outer `src` loop variable. _(AUD-37)_

- [ ] **B-35** _(AUD-38)_ Remove the stale `"use AudioSourcePool logic inline (pool object is
  embedded in AudioSystem to keep the implementation self-contained in Phase 7)"` comment from
  `playSound()`; it describes a historical Phase 7 design decision superseded by the current
  `AudioSourcePool.h/.cpp` layout. _(AUD-38)_

- [ ] **B-36** _(AUD-39)_ Define `constexpr std::chrono::milliseconds kAudioThreadWakeInterval{10}`
  in `audio_constants.h` and use it in the `wait_for` call in `audioThreadFunc()` in place of the
  raw `std::chrono::milliseconds(10)` literal. _(AUD-39)_

- [ ] **B-37** _(AUD-40)_ Replace the manual `alGetError()` pre-clear + post-check pattern in
  `allocateEFXFilters()` with `alCheckError()` calls to match the convention used everywhere
  else in the file. _(AUD-40)_

- [ ] **B-38** _(AUD-41)_ Add `static_assert(sizeof(ALuint) == sizeof(unsigned int))` adjacent
  to the `reinterpret_cast<ALuint*>(m_sources)` call in the `AudioSystem` constructor to make
  the aliasing assumption explicit. _(AUD-41)_

- [ ] **B-42** _(AUD-45)_ Extract the pitch and gain crossblend calculation in
  `updateVehicleEngines()` into small named helpers (e.g. `vehicleEnginePitch(float speed,
  float basePitch)`) to make the interpolation formula readable and independently
  testable. _(AUD-45)_

---

#### 3. Simulation clean-up (Group C)

**Files:** `src/simulation/CitySimulation.cpp`, `src/simulation/CitySimulation.h`,
`src/simulation/simulation_constants.h`, `src/simulation/SaveSystem.cpp`,
`src/terrain/TerrainSystem.cpp`, `src/terrain/TerrainSystem.h`

##### HIGH — correctness / significant duplication

- [ ] **C-1** _(SIM-1)_ Extract `startingFundsForDifficulty(Difficulty)` and
  `bondMaxUsesForDifficulty(Difficulty)` private static helpers in `CitySimulation` and
  call them from both the constructor and `reset()`, eliminating the duplicated
  switch blocks. _(SIM-1)_

- [ ] **C-2** _(SIM-2)_ Extract a private `resetTrafficWindows()` method containing the
  traffic rolling-window zero-initialisation and call it from both the constructor and
  `reset()`. _(SIM-2)_

- [ ] **C-3** _(SIM-3)_ Compute `travelTime` once before the three zone-type sample blocks
  in `computeTrafficDemand()`. Extract a local lambda `computeZoneSample(adjacentCount,
  fullTime, zeroTime)` to de-duplicate the null/overcongested/normal branch logic
  replicated three times. _(SIM-3)_

- [ ] **C-4** _(SIM-5)_ Move the "which service types exist" boolean scan in
  `doDesirabilityTick()` above the tile loop so it runs once per tick, not once per tile. _(SIM-5)_

- [ ] **C-5** _(SIM-6)_ Remove the three redundant `m_taxRates[0/1/2] = 0.05f` assignments
  from the constructor body; the in-class initialiser already sets the correct default. _(SIM-6)_

- [ ] **C-6** _(SIM-7)_ Add a `computeEconomySnapshot()` first-pass method that writes
  `m_budgetSurplusPct` (and optionally caches subtotals) so that `doEconomyTick()` can
  reuse the pre-computed values instead of calling all revenue/expense helpers twice per
  budget tick. _(SIM-7)_

- [ ] **C-27** _(SIM-27)_ Split `doDensityUnlockTick()` (~295 lines) into focused private
  helpers: extract a `getDensityUnlockThreshold(TierIndex)` lookup, a
  `scanUnlockCandidates(zone, currentDensity, outTiles)` pass, and an
  `applyDensityUpgrade(tileX, tileZ)` apply pass, reducing the monolithic function to an
  orchestration loop. _(SIM-27)_

##### MEDIUM — best practices / magic numbers

- [ ] **C-7** _(SIM-4)_ Remove the redundant per-access modulo in the traffic rolling-window
  write: use the index directly (`m_trafficWindowR[m_trafficWindowIdxRC] = sampleR`)
  after it has been kept in-range by the increment. _(SIM-4)_

- [ ] **C-8** _(SIM-8)_ Add population-capacity constants (`max_pop_residential_low`,
  `max_pop_residential_medium`, etc.) to `SimulationConstants` and reference them from
  `maxPopulationForTile()`. _(SIM-8)_

- [ ] **C-9** _(SIM-9)_ Add city-rating population threshold constants to `SimulationConstants`
  (`city_rating_town_threshold`, etc.) and use them in `checkCityRatingTransition()`. _(SIM-9)_

- [ ] **C-10** _(SIM-10)_ Replace the bare `3.14159265f` literal in the heading calculation
  with `std::numbers::pi_v<float>` (C++20) or a `constexpr float kPiF`. _(SIM-10)_

- [ ] **C-11** _(SIM-11)_ Add `static constexpr int kDespawnedVehicleTile = -9999` and
  optionally a `TrafficVehicle::isDespawned()` predicate; replace the three literal
  occurrences. _(SIM-11)_

- [ ] **C-12** _(SIM-14)_ Add `constexpr float SimulationConstants::loan_interest_rate = 0.05f`
  and reference it from `processLoanRepayments()` instead of the bare `0.05f` literal. _(SIM-14)_

- [ ] **C-13** _(SIM-13)_ Replace the index-collection + reverse-loop erase in
  `processLoanRepayments()` with `std::erase_if` (C++20) or the erase-remove idiom. _(SIM-13)_

- [ ] **C-14** _(SIM-15)_ Cache the BFS result of `computePowerCoverage()` per tick: add
  `buildPowerCoverageCache()` called once at the top of `doDesirabilityTick()` and replace
  per-tile BFS calls with O(1) set lookups. _(SIM-15)_

- [ ] **C-15** _(SIM-16)_ Replace the private `ServiceType` enum with the public
  `ServiceBuildingType` enum throughout `CitySimulation`'s private data. _(SIM-16)_

- [ ] **C-16** _(SIM-17)_ Change `SimulationConstants::startingFundsForDifficulty()` to
  accept `Difficulty` (enum) instead of `int`, mark it `constexpr`, and reference the
  named constants (`starting_funds_easy/normal/hard`) within it. _(SIM-17)_

- [ ] **C-17** _(SIM-12)_ Move `setMapDimensions()` body from the header to
  `CitySimulation.cpp`. _(SIM-12)_

- [ ] **C-24** _(SIM-24)_ Replace the three fixed-length C-arrays `m_taxRates[3]`,
  `m_demandPressurePct[3]`, and `m_lastMonthTaxRevenue[3]` in `CitySimulation.h` with
  `std::array<float, 3>` to gain `fill()`, range-for, and bounds-checked `.at()`. _(SIM-24)_

- [ ] **C-25** _(SIM-25)_ After C-24, replace the three explicit per-index zero-assignments in
  `reset()` (`m_lastMonthTaxRevenue[0/1/2] = 0.0f`) with a single `.fill(0.0f)` call. _(SIM-25)_

- [ ] **C-26** _(SIM-26)_ Replace the 5-branch if-else time-of-day classifier in `tick()` with a
  `static constexpr` table of `{upperHour, SimPeriod}` pairs iterated with a ranged for-loop,
  making time-window adjustments a single-line table edit. _(SIM-26)_

- [ ] **C-28** _(SIM-28)_ Replace the 6-case `switch` mapping `tierIdx` to `(zone, density)`
  pairs inside `doDensityUnlockTick()` with a `static constexpr` struct-array
  `{ZoneType zone; DensityLevel density}` indexed by tier, eliminating the switch
  body entirely. _(SIM-28)_

- [ ] **C-29** _(SIM-29)_ Split `doDesirabilityTick()` (~158 lines) by extracting the
  service-coverage BFS phase into `buildServiceCoverageMap(outCoveredTiles)` and the
  per-tile desirability-score update into `applyDesirabilityScores(coveredTiles)`, leaving
  `doDesirabilityTick()` as a thin orchestrator. (Note: C-4 covers only the boolean-scan
  hoist; this is the full function split.) _(SIM-29)_

- [ ] **C-30** _(SIM-30)_ Split `doPopulationTick()` (~72 lines) by extracting
  `computeZoneGrowthDelta(ZoneType, tileCount)` and `accumulateHouseDemand()` helpers,
  so the tick method only coordinates the calls. _(SIM-30)_

##### LOW — optional / stretch

- [ ] **C-18** _(SIM-18)_ Replace the C-string pointer switches in `zoneAssetBaseName()`
  with a 2D `constexpr const char*` lookup table. _(SIM-18)_

- [ ] **C-19** _(SIM-19)_ Move file-scope `kVehicleTilePerSecond` and `kVehicleSpawnInterval`
  to `SimulationConstants`; remove the duplicate `kTileSizeM` and use `kTileSizeMeters`
  consistently. _(SIM-19)_

- [ ] **C-20** _(SIM-20)_ Evaluate the `AITOWN_TESTING_ENABLED` guard pattern in
  `CitySimulation.h` and apply a CMake-level enforcement to prevent the macro from being
  set in non-test builds. _(SIM-20)_

- [ ] **C-21** _(SIM-21)_ Remove the redundant `#if defined(_WIN32) / #else / #endif`
  block that includes the same `<cstdlib>` header in both branches in `SaveSystem.cpp`. _(SIM-21)_

- [ ] **C-22** _(SIM-22)_ Replace the platform-discriminated path-string construction in
  `slotFilePath()` and `autoSaveFilePath()` with `std::filesystem::path(dir) / filename`. _(SIM-22)_

- [ ] **C-23** _(SIM-23)_ Change the `///` Doxygen-style comment in `ITerrainQuery.h`
  (`flushTerrainRebuilds`) to a standard `//` comment to match the file's style convention. _(SIM-23)_

---

#### 4. UI clean-up (Group D)

**Files:** `src/ui/HUD.cpp`, `src/ui/HUD.h`, `src/ui/FinancesPanel.cpp`,
`src/ui/FinancesPanel.h`, `src/ui/UIManager.h`, `src/ui/UIManager.cpp`,
`src/ui/key_bindings.h`, `src/ui/NotificationManager.h`,
`src/ui/UIScaler.h`, `src/ui/ModalDialog.h`, `src/ui/SettingsPanel.h`,
`src/ui/CameraController.h`, `src/interfaces/IUIBackend.h`,
`src/interfaces/simulation_types.h`

##### HIGH — significant duplication / broken invariants

- [ ] **D-1** _(UI-1)_ Create `src/ui/ui_format.h` + `src/ui/ui_format.cpp` with a single
  `formatDollar(float)` function (merging the identical `formatDollar` in `HUD.cpp` and
  `fmtDollar` in `FinancesPanel.cpp`). Update both files to use the shared helper. _(UI-1)_

- [ ] **D-2** _(UI-4)_ Remove `const` from `KeyBindings::undo` and `KeyBindings::save` to
  restore default copy/move assignment; enforce their non-rebindability via a runtime guard
  in `load()`; delete `copyMutableFrom()`. Before deleting, verify no test file calls
  `copyMutableFrom()` (run `grep -r copyMutableFrom tests/`). _(UI-4)_

- [ ] **D-3** _(UI-5)_ Move `KeyBindings::writeToFile()` body from the header to a new
  `key_bindings.cpp`. Replace the 11 individual `fprintf` calls with an `ostringstream`
  built into a single string written via `std::ofstream`. _(UI-5)_

- [ ] **D-4** _(UI-3)_ Extract `WorldInteractionState` (activeTool, hoveredTile, zoneAnchor,
  overlay map, selectedZoneType, etc.) and `GameSessionState` (gameSessionActive,
  pendingNewGame, pendingLoadJson, pendingQuit, etc.) as private structs in `UIManager.h`.
  Group the remaining member variables under clearly labelled `// --- section ---` comments.
  If any test fixture currently accesses `UIManager` internal state members directly, add
  `const` accessors (e.g., `getWorldInteractionStateForTest()`) or friend declarations to
  preserve test access after extraction. _(UI-3)_

- [ ] **D-5** _(UI-2)_ Promote `ratingName(CityRatingTier)` from a file-local static in
  `HUD.cpp` to a free function declared in `src/ui/ui_format.h` (or `simulation_types.h`). _(UI-2)_

- [ ] **D-23** _(UI-23)_ Split `UIManager::update()` (~397 lines) into private sub-methods:
  `pollMainMenuRequests()`, `pollPauseMenuRequests()`, `updateModalDialogState()`, and
  `updateHUDState()`, so the top-level `update()` becomes a thin dispatcher with a clear
  phase ordering. _(UI-23)_

##### MEDIUM — best practices

- [ ] **D-6** _(UI-6)_ Remove the redundant `static` keyword from
  `static constexpr UIElementHandle kInvalidUIElement = 0` in `IUIBackend.h`
  (C++17 `constexpr` at namespace scope is already implicitly inline/internal). _(UI-6)_

- [ ] **D-7** _(UI-8)_ Remove `getLastResult()` from `ModalDialog`'s public interface;
  rename or add `peekResult() const noexcept` if test-only read access is needed. _(UI-8)_

- [ ] **D-8** _(UI-9)_ Mark `UIManager::setOverlayMapForTest()` with
  `[[deprecated("for tests only")]]`. Group all test seams under a single `// TEST SEAM API`
  section with a contract comment. _(UI-9)_

- [ ] **D-9** _(UI-10)_ Move the 12 layout constants from `NotificationManager.h`'s class
  body to `NotificationManager.cpp` as anonymous-namespace `constexpr` values. _(UI-10)_

- [ ] **D-10** _(UI-11)_ Mark `UIScaler` members that never change after construction
  (`m_virtualW`, `m_virtualH`, `m_offsetX`, `m_offsetY`) as `const int`. _(UI-11)_

- [ ] **D-11** _(UI-13)_ Replace `FinancesPanel`'s three named `ZoneRow` members
  (`m_rowR`, `m_rowC`, `m_rowI`) with `std::array<ZoneRow, 3> m_rows` indexed by
  `(int)ZoneType`. _(UI-13)_

- [ ] **D-12** _(UI-14)_ Replace `UIManager`'s `UIElementHandle m_zoneSubPanelBtns[9]`
  and `m_utilSubPanelBtns[4]` C-arrays with `std::array<UIElementHandle, 9>` and
  `std::array<UIElementHandle, 4>` respectively. _(UI-14)_

- [ ] **D-13** _(UI-7)_ Move the `PendingQuitAction` enum class from `UIManager`'s private
  member section to `ui_types.h`. _(UI-7)_

- [ ] **D-14** _(UI-12)_ Move `CameraController::resetOrbit()` body from the header to
  `CameraController.cpp`. _(UI-12)_

- [ ] **D-15** _(UI-18)_ Add `constexpr float kDefaultMasterVolume = 1.0f`,
  `kDefaultMusicVolume = 0.8f`, `kDefaultSfxVolume = 0.8f` to `ui_constants.h` (or
  `audio_types.h`). Reference them from both `SettingsPanel.h` member initialisers and
  `AudioSystem`'s constructor. _(UI-18)_

- [ ] **D-16** _(UI-19)_ Rename `ICitySimulation::getDemandPressurePct()` →
  `getDemandPressureFraction()` (returns `float` in `[0,1]`, not a percentage). Update
  all callers. The rename resolves the off-by-100 hazard documented at the declaration site. _(UI-19)_

- [ ] **D-21** _(UI-21)_ Declare `m_bindings` as `const KeyBindings` in `CameraController.h`;
  it is set once in the constructor initialiser list and never reassigned. _(UI-21)_

- [ ] **D-22** _(UI-22)_ Change the `m_overlayMap` key computation in `UIManager` from `int`
  arithmetic to `static_cast<int64_t>(tileZ) * m_mapTilesX + tileX` and update the map type
  to `std::unordered_map<int64_t, ...>` to prevent signed-integer overflow on large maps. _(UI-22)_

##### LOW — optional / stretch

- [ ] **D-17** _(UI-15)_ Add a null check or `assert` to `Minimap::setSimulation()` and
  a mode-guard to `setOverlayMode()` (fallback to `None` if traffic mode is requested
  while `m_sim == nullptr`). _(UI-15)_

- [ ] **D-18** _(UI-16)_ Apply `[[nodiscard]]` to `consumeSaveRequest()`,
  `consumeQuitRequest()`, and similar `pollFlag`-style methods in `PauseMenuPanel` and
  `MainMenuPanel`. _(UI-16)_

- [ ] **D-19** _(UI-17)_ Rename `Rect` in `IUIBackend.h` to `UIRect` to avoid collision with
  `irr::core::rect` and platform-level `Rect` types. _(UI-17)_

- [ ] **D-20** _(UI-20)_ Replace the double-duty `amount`/`repaymentTicks` fields in
  `SimulationNotification` with a `std::variant<LoanInfo, TileInfo, std::monostate>` or
  per-type named accessors with a doc comment specifying which fields apply to each
  `NotificationType`. _(UI-20)_

---

#### 5. Build and CI/CD clean-up (Group E)

**Files:** `CMakeLists.txt`, `CMakePresets.json`, `Makefile`,
`.github/workflows/_build-linux.yml`, `.github/workflows/_build-windows.yml`,
`.github/workflows/_coverage-linux.yml`, `.github/workflows/_validate-assets.yml`,
`.github/workflows/_package-windows.yml`, `.github/workflows/_package-linux-deb.yml`,
`.github/workflows/ci.yml`

##### HIGH — correctness / current defects

- [ ] **E-1** _(BUILD-3)_ Remove the duplicate `target_sources()` block that adds
  `adaptive_music_intensity_test.cpp` and `city_simulation_render_test.cpp` to
  `simulation_tests` a second time (the second block at lines 595–599). _(BUILD-3)_

- [ ] **E-2** _(BUILD-4)_ Change `target_link_libraries(aitown_terrain PUBLIC aitown_render)`
  to `PRIVATE` to stop leaking Irrlicht and GLEW headers into the terrain test graph. _(BUILD-4)_

- [ ] **E-3** _(BUILD-2)_ Audit the `AITOWN_TESTING_ENABLED=1` compile definition on
  `aitown_ui`. Confirm it is set with the `PRIVATE` qualifier (not `PUBLIC` or
  `INTERFACE`) so the flag does not leak into downstream consumers. Add an explanatory
  comment in `CMakeLists.txt` directly alongside the
  `target_compile_definitions(aitown_ui PRIVATE AITOWN_TESTING_ENABLED=1)` call that
  reads, in substance: "`AITOWN_TESTING_ENABLED=1` MUST remain on `aitown_ui PRIVATE`
  because `handleNewGameRequest` and `setGameSessionActiveForTest` are non-inline
  functions defined in `UIManager.cpp`; compiling `aitown_ui` without this flag would
  cause undefined-reference link errors in `ui_tests`. See
  `architecture/testing/testability-architecture.md`." Do **not** move the definition to
  an `INTERFACE` library or remove it from `aitown_ui`; doing so would break the
  production-library link. _(BUILD-2)_

- [ ] **E-4** _(BUILD-1)_ Create a CMake `INTERFACE` library `aitown_coverage_flags` and
  apply it only to the targets that feed lcov; remove `add_compile_options`/`add_link_options`
  global coverage flag blocks. _(BUILD-1)_

- [ ] **E-5** _(BUILD-5)_ Extract a CMake macro `aitown_copy_runtime_dlls(target)` for
  the Windows DLL post-build copy pattern. Replace the four verbatim `add_custom_command`
  blocks. _(BUILD-5)_

- [ ] **E-6** _(BUILD-6)_ Create a CMake `INTERFACE` library `aitown_irrlicht_system_deps`
  with the five Irrlicht transitive system libraries (JPEG, PNG, ZLIB, BZip2, Xxf86vm).
  Replace the five verbatim `if(UNIX AND NOT APPLE)` blocks with single
  `target_link_libraries(...PRIVATE aitown_irrlicht_system_deps)` calls. _(BUILD-6)_

- [ ] **E-7** _(BUILD-7)_ Document the `package-windows` intent explicitly: either add a
  cmake `--build` step after configure, or add a comment explaining that configure is
  run only to produce `CMakeCache.txt` for `cpack -C Release`, and add a verification
  step confirming `aitown.exe` is present in the staging artifact before cpack runs. _(BUILD-7)_

##### MEDIUM — best practices / simplification

- [ ] **E-8** _(BUILD-8)_ Remove the global `set(CMAKE_CXX_STANDARD 17)` block and replace
  with `target_compile_features(<target> PRIVATE cxx_std_17)` on each library and
  executable target. _(BUILD-8)_

- [ ] **E-9** _(BUILD-9)_ Create `aitown_project_root_includes` INTERFACE library exposing
  `${CMAKE_SOURCE_DIR}` and link it only to targets that need root-relative includes. _(BUILD-9)_

- [ ] **E-10** _(BUILD-10)_ Merge the two separate `if(BUILD_TESTING)` guards into one
  block containing `find_package(GTest)`, `find_package(rapidcheck)`,
  `enable_testing()`, and `include(AitownTestHelpers)`. _(BUILD-10)_

- [ ] **E-11** _(BUILD-11)_ Extract a reusable `_test-linux.yml` workflow for the shared
  ctest verification and run steps used by both `_build-linux.yml` and
  `_coverage-linux.yml`. _(BUILD-11)_

- [ ] **E-12** _(BUILD-12)_ Merge the two separate PowerShell GLEW verification steps in
  `_build-windows.yml` into one combined step that checks both `GL/glew.h` and
  `glew32.lib`. _(BUILD-12)_

- [ ] **E-13** _(BUILD-13)_ Add `actions/cache` for the vcpkg tool binary and installed
  packages in `_package-linux-deb.yml` to avoid re-bootstrapping vcpkg from source on
  every packaging run. _(BUILD-13)_

- [ ] **E-14** _(BUILD-14)_ Add the `xvfb-run ctest -L ^requires-opengl$` tier to the
  `make test` target, guarded by a `which xvfb-run` availability check. _(BUILD-14)_

- [ ] **E-15** _(BUILD-15)_ Add a check to the `make test` target: if
  `build/CMakeCache.txt` exists and already has `ENABLE_COVERAGE=ON`, skip the
  re-configure step; otherwise document the full-reconfigure behaviour in a comment. _(BUILD-15)_

- [ ] **E-16** _(BUILD-16)_ Add `/opt/vcpkg_installed/*` (and `$VCPKG_ROOT/installed/*`)
  to the Makefile `lcov --remove` exclusion list to match the CI pattern set. _(BUILD-16)_

- [ ] **E-17** _(BUILD-17)_ Replace the twelve individual "Verify check_N present" steps in
  `_validate-assets.yml` with a single loop step. _(BUILD-17)_

- [ ] **E-18** _(BUILD-18)_ Evaluate replacing the `prepare` serial job in `ci.yml` with
  GitHub Actions `vars` context or direct string literals to remove the serial critical-path
  job. _(BUILD-18)_

- [ ] **E-19** _(BUILD-19)_ Replace `${{ github.workspace }}` in the lcov `--remove` pattern
  in `_coverage-linux.yml` with a shell-resolved `$(pwd)` variable. _(BUILD-19)_

- [ ] **E-20** _(BUILD-20)_ Add `"VCPKG_TARGET_TRIPLET": "x64-windows"` explicitly to the
  `ci-windows` preset in `CMakePresets.json`. _(BUILD-20)_

- [ ] **E-21** _(BUILD-21)_ Add `name:` labels to the `ilammy/msvc-dev-cmd` and
  `lukka/run-vcpkg` steps in `_package-windows.yml`. _(BUILD-21)_

- [ ] **E-30** _(BUILD-30)_ Remove all `"Phase N:"` / `"Phase N delivers..."` / `"Phase N
  replaces..."` historical narrative comments from `CMakeLists.txt` library blocks: the
  `aitown_render` Phase 1 block, `aitown_audio` Phase 7 block, `aitown_sim` Phase 6/11 block,
  `aitown_ui` Phase 8 "Do NOT edit" note, `aitown_terrain` Phase 3/5 history, `KeyBindingsPanel`
  Phase 11c label, Phase 11l Deliverable 9 label on the Irrlicht PRIVATE note, Phase 9 label on
  `aitown_benchmark`, and the Phase 11m prefix on the `AITOWN_TESTING_ENABLED` comment. _(BUILD-30)_

- [ ] **E-31** _(BUILD-31)_ Renumber (or remove) the out-of-sequence `# Step N:` step annotations
  in `_build-linux.yml` and `_coverage-linux.yml`; the numbering skips Steps 2, 3, 5–7
  entirely, making cross-referencing confusing. _(BUILD-31)_

- [ ] **E-32** _(BUILD-32)_ Remove stale prose from `_coverage-linux.yml`: the `"lcov --summary
  informational step REMOVED in Phase 5"` notice and the `"At Phase 0 only smoke tests exist…"`
  paragraph in the `--ignore-errors` section (both describe historical decisions, not current
  rationale). _(BUILD-32)_

- [ ] **E-35** _(BUILD-35)_ Hoist the repeated `mkdir -p test_results` directory-creation
  command into a single dedicated step that runs before all three `ctest` invocations in
  both `_build-linux.yml` and `_coverage-linux.yml`, eliminating three per-run duplicates. _(BUILD-35)_

##### LOW — optional / stretch

- [ ] **E-22** _(BUILD-22)_ Add an explicit configure-time error if `XXFV86VM_LIB` is not
  found on Linux: `if(UNIX AND NOT APPLE AND NOT XXFV86VM_LIB) message(FATAL_ERROR ...)`
  to surface the missing `libxxf86vm-dev` as a clear configure error rather than a
  silent runtime failure. _(BUILD-22)_

- [ ] **E-23** _(BUILD-23)_ Remove the redundant `if: runner.os == 'Linux'` condition from
  the `ccache` step in `_build-linux.yml` and `_coverage-linux.yml`. _(BUILD-23)_

- [ ] **E-24** _(BUILD-24)_ Deduplicate the `empty` token in the Makefile `lcov --capture`
  `--ignore-errors` list; synchronise the flag set with `_coverage-linux.yml`. _(BUILD-24)_

- [ ] **E-25** _(BUILD-25)_ Pin the `docker/ci-linux/Dockerfile` base image from
  `debian:trixie` (rolling) to a specific digest. Add it to the existing five-item
  vcpkg baseline atomicity rule as a sixth item. _(BUILD-25)_

- [ ] **E-26** _(BUILD-26)_ Pin `mutagen` and `Pillow` to specific versions in the
  `_validate-assets.yml` pip install step, or extract a `tools/requirements-validate.txt`
  file. _(BUILD-26)_

- [ ] **E-27** _(BUILD-27)_ Replace the global `npm install -g markdownlint-cli` in
  `_markdown-lint.yml` with `npx markdownlint-cli@0.47.0` to eliminate the global
  install step and align with the local invocation pattern. _(BUILD-27)_

- [ ] **E-28** _(BUILD-28)_ Update `actions/download-artifact` pin to the same minor
  version (`v4.6.0`) as `actions/upload-artifact`. _(BUILD-28)_

- [ ] **E-29** _(BUILD-29)_ Remove the manual `OpenAL::OpenAL` IMPORTED target workaround
  in `CMakeLists.txt` (lines 54–61) given the project's cmake_minimum_required of 3.21
  already ships a `FindOpenAL.cmake` that creates the target natively. _(BUILD-29)_

- [ ] **E-33** _(BUILD-33)_ Collapse the per-phase check-number inventory in the
  `_validate-assets.yml` header comment to a single line stating the current highest check
  number so it stays accurate without requiring incremental per-phase updates. _(BUILD-33)_

- [ ] **E-34** _(BUILD-34)_ Remove the `"(Phase 7)"` parenthetical hardening annotation from
  the DLL verification step comment in `_build-windows.yml`. _(BUILD-34)_

- [ ] **E-36** _(BUILD-36)_ Refactor the `all-checks-pass` gate in `ci.yml` to iterate over a
  newline-delimited list of job-result variables rather than hardcoding each of the 7 result
  comparisons individually, making additions/removals a single-line change. _(BUILD-36)_

---

#### 6. Testing clean-up (Group F)

**Files:** `tests/` (all subdirectories), `tests/ui/MockUIBackend.h`,
`tests/simulation/SimulationTestBase.h`, `CMakeLists.txt` (test source annotations)

##### HIGH — correctness / significant duplication

- [ ] **F-1** _(TEST-1)_ In the `cs()` downcast helpers in `population_test.cpp`,
  `undo_system_test.cpp`, and `service_coverage_test.cpp`, replace `EXPECT_NE(p, nullptr)`
  with `ASSERT_NE(p, nullptr)` — `EXPECT_NE` continues execution after a null, causing a
  crash in `runTicks()` rather than a clean test failure. _(TEST-1)_

- [ ] **F-2** _(TEST-2)_ Extract `NiceSimulationTestBase` (mirroring `SimulationTestBase` but
  with `NiceMock`) to eliminate the duplicated `NiceMock` construction pattern in the 8
  NiceMock-based simulation fixtures that independently replicate the same members, `SetUp`,
  `TearDown`, `cs()`, and `runTicks()`. _(TEST-2)_

- [ ] **F-3** _(TEST-3)_ Extract a `UITestFixtureBase` class (or free helper
  `setupStandardBackendStubs()`) in a new `tests/ui/UITestBase.h` to eliminate the identical
  8-call `ON_CALL` block (`addStaticText`, `addButton`, `getVirtualWidth`, `getVirtualHeight`,
  `isElementVisible`, `isElementEnabled`, `getElementRect`, …) duplicated across 27 UI test
  fixture `SetUp()` methods. _(TEST-3)_

- [ ] **F-4** _(TEST-4)_ Centralise `makeKeyDown`, `makeClick`, `makeMouseButtonDown`, and
  equivalent `InputEvent` factory functions in a new `tests/ui/input_event_helpers.h` to
  replace the 15+ file-local identical definitions scattered across UI test files. _(TEST-4)_

##### MEDIUM — stale comments / naming / assertions

- [ ] **F-5** _(TEST-5)_ In `UIManagerModalTest::SetUp()` (line ~41), replace the `nullptr`
  `IClock*` and its stale `"Phase 3 stubs do not dereference the clock pointer"` comment with
  a real `ManualClock` instance, matching every other fixture in the file. _(TEST-5)_

- [ ] **F-6** _(TEST-6)_ Rename all `*_InPhase3Stub` and `*_Phase3*` test names in
  `ui_manager_modal_test.cpp` (e.g. `HasActiveModal_ReturnsFalse_InPhase3Stub` →
  `HasActiveModal_BeforeModalShown_ReturnsFalse`) and remove `"Phase 3 stub"` qualifiers from
  assertion failure message strings. _(TEST-6)_

- [ ] **F-7** _(TEST-7)_ Either implement real assertions in the `EscapeClosesSettings*` stub
  tests in `ui_manager_modal_test.cpp` (commented `"Phase 6 replaces SUCCEED() with real
  assertions"`) or delete the empty stubs — both `UIManager` Escape-routing paths are fully
  implemented and testable. _(TEST-7)_

- [ ] **F-8** _(TEST-8)_ Replace the four `"Phase 10:"` comment prefixes in
  `SimulationTestBase::SetUp()` with plain explanatory prose (e.g. `"tick() calls
  setMusicIntensity() once per budget tick — suppress in base fixture"`) so the fixture is
  self-documenting without version watermarks. _(TEST-8)_

- [ ] **F-9** _(TEST-9)_ Remove `"Phase N stub"` / `"Phase 6 stub — full impl in Phase 11"`
  inline comments on `save_system_test.cpp` and `undo_system_test.cpp` in the
  `simulation_tests` source list in `CMakeLists.txt`; both phases are complete. _(TEST-9)_

- [ ] **F-10** _(TEST-10)_ Replace hardcoded fund literals (`500000.0f`, `1000000.0f`,
  `200000.0f`) in `simulation_comprehensive_integration_test.cpp` with
  `SimulationConstants::starting_funds_normal / _easy / _hard` so mismatches produce a
  named constant mismatch rather than an unexplained numeric failure. _(TEST-10)_

- [ ] **F-11** _(TEST-11)_ Add at least one non-trivial `EXPECT_*` assertion to each
  `SUCCEED()`-only test body in `city_simulation_extra_test.cpp` and `economy_test.cpp`
  that sets up multi-tile state but asserts nothing — these tests inflate test count without
  verifying any behaviour. _(TEST-11)_

- [ ] **F-12** _(TEST-12)_ Remove the 16 `"Phase 3 stub bodies"` / `"Phase 3 no-ops"` comment
  blocks throughout `UIManagerTransitionTest` and `NotificationManagerStandaloneTest` in
  `ui_manager_modal_test.cpp`; the described methods have real implementations. _(TEST-12)_

##### LOW — optional / stretch

- [ ] **F-13** _(TEST-13)_ Unify `NiceCoverageTest` and `NiceExtraCoverageTest` in
  `city_simulation_extra_test.cpp` (identical members, `SetUp`, `TearDown`, `cs()`,
  `runTicks()`) into a single fixture, or have `NiceExtraCoverageTest` inherit from
  `NiceCoverageTest`. _(TEST-13)_

- [ ] **F-14** _(TEST-14)_ In `EconomyTest`, either switch `audio_` from `StrictMock` to
  `NiceMock` or remove the blanket `EXPECT_CALL(audio_, playPositionalSound(...)).Times(AnyNumber())`
  suppression — the combination silently reduces `StrictMock` to `NiceMock` behaviour for
  that method. _(TEST-14)_

- [ ] **F-15** _(TEST-15)_ Derive `PlacementConflictTest` from `SimulationTestBase` instead
  of reimplementing all 8 `EXPECT_CALL` suppressions and construction boilerplate (~35 duplicate
  lines). _(TEST-15)_

- [ ] **F-16** _(TEST-16)_ Rename the `UIManagerModalTest_Phase8` fixture class to
  `UIManagerModalBehaviourTest` (or similar) to remove the `_Phase8` delivery watermark from CI
  test output. _(TEST-16)_

- [ ] **F-17** _(TEST-17)_ Remove ordinal delivery labels (`"Method 19 — Phase 10 addition"`,
  `"Method 20 — repositions…"`) from `MockUIBackend.h` and replace with plain behavioural
  comments describing what each method does. _(TEST-17)_

- [ ] **F-18** _(TEST-18)_ Update the `save_system_test.cpp` file header which says
  `"stub implementations"` and `"real classes are not yet available"` to describe the file's
  current role: contract-level tests against a hand-written stub that avoids disk I/O, kept
  alongside `save_system_real_test.cpp`. _(TEST-18)_

---

### Exit Criteria

- [ ] All HIGH-priority deliverables (A-1–A-6, A-21–A-22, A-33, B-1–B-7, B-25–B-27,
  C-1–C-6, C-27, D-1–D-5, D-23, E-1–E-7, F-1–F-4) are implemented and all tests pass
  (`make test`).
- [ ] All MEDIUM-priority deliverables are implemented or explicitly deferred with a
  documented reason.
- [ ] All existing unit, integration, and OpenGL tests pass unchanged under
  `make test` — no regressions introduced by the refactoring.
- [ ] `npx markdownlint-cli 'architecture/**/*.md' 'implementation/*.md' 'CLAUDE.md'`
  reports zero errors.
- [ ] No new spec contradictions are introduced by the renames (specifically: if
  `getDemandPressurePct` is renamed per D-16, the corresponding spec rename must be
  tracked as a deliverable in a subsequent phase-11p spec-consistency pass).

### Open Items

- **Spec rename tracking (D-16)**: If `getDemandPressureFraction()` rename is applied,
  a follow-up spec-consistency pass is needed to update `ICitySimulation.h` references
  in all architecture spec files.
- **AudioSourcePool decision (B-3)**: Option B has been mandated — `AudioSystem` is wired to use `AudioSourcePool` exclusively, with inline duplicate pool state removed. All subsequent audio test coverage (including B-13, B-18, B-32, and B-35) follows Option B and assumes `AudioSourcePool` is retained.
