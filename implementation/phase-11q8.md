## Phase 11q8: Demolish Drag-Select + Modal on Mouse-Up

**Status: TODO**

**Prerequisite**: phase-11q7 merged.

### Goal

Click-dragging with the Demolish tool is currently broken: the "Demolish yes/no"
confirmation modal fires on the first mouse-up, making it impossible to drag a
multi-tile selection. The root cause is that the modal opens **before** the
drag is complete.

Fix: convert the demolish tool to a **rectangular drag-select** (same mechanic as
the Zone tool). The confirmation modal now fires **after** the full rectangle is
selected — on mouse-up — showing "Demolish N tile(s)?". The "Confirm before
demolish" setting in Settings > Gameplay is preserved: when OFF, mouse-up demolishes
the rectangle immediately without a modal.

Specific changes:

- Replace the single-tile click → modal → yes/no flow with a rectangular drag-select.
  Mouse-down records the anchor; mouse-move shows a live rect preview (occupied tiles
  in demolish-red, empty tiles in blocked-grey); mouse-up either shows the confirm
  modal (confirm ON) or demolishes immediately (confirm OFF).
- The confirmation modal now asks "Confirm Demolish? N tile(s) will be demolished."
  where N is the count of occupied tiles in the selected rectangle. If the selection
  contains zero occupied tiles the modal is suppressed and nothing happens.
- Wire the "Confirm before demolish" SettingsPanel toggle to UIManager — it was never
  forwarded (dead code); connect it via a callback so changes take effect immediately.
- Add `demolishReleaseX/Z` to `WorldInteractionState` to store the drag-end tile while
  the modal is pending (the modal poll needs it to iterate the rect).
- Rename `demolishPendingTileX/Z` → `demolishAnchorX/Z` to reflect the drag anchor.
- Update `architecture/ui-ux/input-arbitration.md` §7 and the standalone Demolition
  Tool section to describe the new drag-select + post-selection modal contract.
- Update `architecture/ui-ux/modal-dialog-system.md` to describe the updated demolish
  confirmation modal (now N-tile rect confirmation, not single-tile).
- Replace the old modal-flow demolition tests with drag-select tests.

---

### Issues to Fix

#### 1. Extend `WorldInteractionState` in `UIManager.h`

In `UIManager.h`, inside `struct WorldInteractionState`:

- Rename `demolishPendingTileX` → `demolishAnchorX` (drag start tile).
- Rename `demolishPendingTileZ` → `demolishAnchorZ`.
- Add `demolishReleaseX{-1}` and `demolishReleaseZ{-1}` — the tile where the mouse
  was released; stored on mouse-up so the modal poll can iterate the full rect.
- Keep `bool demolishModalPending{false}` — still needed (modal path retained).

`m_demolishConfirmEnabled` and `setDemolishConfirm()` in `UIManager.h` are **kept**
(used by tests and wired to SettingsPanel in Issue 7).

```cpp
// UIManager.h — WorldInteractionState (before → after)

// Before:
int  demolishPendingTileX{-1};
int  demolishPendingTileZ{-1};
bool demolishModalPending{false};

// After:
int  demolishAnchorX{-1};
int  demolishAnchorZ{-1};
int  demolishReleaseX{-1};  // NEW — stored on mouse-up for modal poll
int  demolishReleaseZ{-1};  // NEW
bool demolishModalPending{false};  // kept
```

---

#### 2. Mouse-down handler — record anchor and single-tile highlight

In `UIManager.cpp`, in the `MouseButtonDown` handler, update the Demolish branch
(currently ~lines 1174–1182):

```cpp
// Phase 11q8: record drag anchor on mouse-down.
if (m_world.activeTool == ActiveTool::Demolish) {
    m_world.demolishAnchorX = hitX;
    m_world.demolishAnchorZ = hitZ;
    m_renderer->setTileHoverHighlight(hitX, hitZ, 1);
    return true;
}
```

---

#### 3. Mouse-move handler — rectangular demolish preview while dragging

In `UIManager.cpp`, in the `MouseMove` handler, after the Road-tool drag-preview
block and before the `// All other cases` comment, add:

```cpp
// Phase 11q8: Demolish drag preview — occupied tiles in demolish-red,
// empty tiles in blocked-grey.
else if (m_world.lmbHeld
         && m_world.activeTool == ActiveTool::Demolish
         && m_world.demolishAnchorX != -1) {
    int x0 = std::min(m_world.demolishAnchorX, hitX);
    int x1 = std::max(m_world.demolishAnchorX, hitX);
    int z0 = std::min(m_world.demolishAnchorZ, hitZ);
    int z1 = std::max(m_world.demolishAnchorZ, hitZ);
    std::vector<std::pair<int,int>> occupiedTiles;
    std::vector<std::pair<int,int>> emptyTiles;
    const size_t total = static_cast<size_t>((x1-x0+1)*(z1-z0+1));
    occupiedTiles.reserve(total);
    emptyTiles.reserve(total);
    for (int tz = z0; tz <= z1; ++tz) {
        for (int tx = x0; tx <= x1; ++tx) {
            bool occupied = false;
            if (m_sim) {
                QueryResult q = m_sim->queryTile(tx, tz);
                occupied = q.isZoned || q.isRoad
                        || q.serviceType != ServiceBuildingType::None;
            }
            (occupied ? occupiedTiles : emptyTiles).push_back({tx, tz});
        }
    }
    m_renderer->setTileHoverHighlight(-1, -1);
    m_renderer->setTilePlacementPreview(occupiedTiles, kHoverArgbDemolish, emptyTiles);
}
```

---

#### 4. Mouse-up handler — show confirm modal (or immediate demolish)

In `UIManager.cpp`, **replace** the entire Demolish branch in the `MouseButtonUp`
handler (~lines 936–975) with:

```cpp
// Phase 11q8: Demolish mouse-up — show confirm modal (if enabled and
// occupied tiles > 0), or demolish immediately (confirm disabled).
if (m_world.activeTool == ActiveTool::Demolish && m_world.demolishAnchorX != -1) {
    int releaseX = m_world.hoveredTileX;
    int releaseZ = m_world.hoveredTileZ;
    if (releaseX == -1) releaseX = m_world.demolishAnchorX;
    if (releaseZ == -1) releaseZ = m_world.demolishAnchorZ;

    // Count occupied tiles in the selected rectangle.
    int x0 = std::min(m_world.demolishAnchorX, releaseX);
    int x1 = std::max(m_world.demolishAnchorX, releaseX);
    int z0 = std::min(m_world.demolishAnchorZ, releaseZ);
    int z1 = std::max(m_world.demolishAnchorZ, releaseZ);
    int occupiedCount = 0;
    if (m_sim) {
        for (int tz = z0; tz <= z1; ++tz) {
            for (int tx = x0; tx <= x1; ++tx) {
                QueryResult q = m_sim->queryTile(tx, tz);
                if (q.isZoned || q.isRoad
                        || q.serviceType != ServiceBuildingType::None)
                    ++occupiedCount;
            }
        }
    }

    if (occupiedCount == 0) {
        // Nothing to demolish — clear and cancel silently.
        m_world.demolishAnchorX = m_world.demolishAnchorZ = -1;
        if (m_renderer) {
            m_renderer->setTilePlacementPreview({}, 0u, {});
            m_renderer->clearDemolishHighlight();
        }
    } else if (m_demolishConfirmEnabled && m_modal) {
        // Show confirm modal — defer actual demolish to modal Accept.
        m_world.demolishReleaseX = releaseX;
        m_world.demolishReleaseZ = releaseZ;
        m_modal->showDemolishConfirm(occupiedCount);
        m_world.demolishModalPending = true;
    } else {
        // Confirm disabled — demolish all occupied tiles immediately.
        for (int tz = z0; tz <= z1; ++tz) {
            for (int tx = x0; tx <= x1; ++tx) {
                if (m_sim) {
                    QueryResult q = m_sim->queryTile(tx, tz);
                    if (q.isZoned || q.isRoad
                            || q.serviceType != ServiceBuildingType::None) {
                        m_sim->demolishTile(tx, tz);
                        if (m_world.mapTilesX > 0) {
                            int64_t key = static_cast<int64_t>(tz)
                                          * m_world.mapTilesX + tx;
                            m_world.overlayMap.erase(key);
                        }
                    }
                }
            }
        }
        if (m_renderer) {
            m_renderer->setZoneOverlay(m_world.mapTilesX, m_world.mapTilesZ,
                                       m_world.overlayMap);
            m_renderer->setTilePlacementPreview({}, 0u, {});
            m_renderer->clearDemolishHighlight();
        }
        setUnsavedChanges(true);
        m_world.demolishAnchorX = m_world.demolishAnchorZ = -1;
    }
    m_world.lmbHeld = false;
    return true;
}
```

---

#### 5. Update demolish modal poll in `UIManager::updateModalDialogState()`

The modal poll currently demolishes a single tile. Update it to demolish the full
stored rectangle (`demolishAnchorX/Z` → `demolishReleaseX/Z`) on Accept:

```cpp
// Poll demolish confirmation modal result.
if (m_world.demolishModalPending && m_modal && !m_modal->isActive()) {
    m_world.demolishModalPending = false;
    auto result = m_modal->pollResult();
    if (result == ModalDialog::DialogResult::Accept) {
        // Demolish all occupied tiles in the stored rectangle.
        int x0 = std::min(m_world.demolishAnchorX, m_world.demolishReleaseX);
        int x1 = std::max(m_world.demolishAnchorX, m_world.demolishReleaseX);
        int z0 = std::min(m_world.demolishAnchorZ, m_world.demolishReleaseZ);
        int z1 = std::max(m_world.demolishAnchorZ, m_world.demolishReleaseZ);
        if (m_sim && m_world.mapTilesX > 0) {
            for (int tz = z0; tz <= z1; ++tz) {
                for (int tx = x0; tx <= x1; ++tx) {
                    QueryResult q = m_sim->queryTile(tx, tz);
                    if (q.isZoned || q.isRoad
                            || q.serviceType != ServiceBuildingType::None) {
                        m_sim->demolishTile(tx, tz);
                        int64_t key = static_cast<int64_t>(tz)
                                      * m_world.mapTilesX + tx;
                        m_world.overlayMap.erase(key);
                    }
                }
            }
            if (m_renderer)
                m_renderer->setZoneOverlay(m_world.mapTilesX, m_world.mapTilesZ,
                                           m_world.overlayMap);
            setUnsavedChanges(true);
        }
    }
    // Accept or Cancel: always clear highlight and state.
    if (m_renderer) {
        m_renderer->setTilePlacementPreview({}, 0u, {});
        m_renderer->clearDemolishHighlight();
    }
    m_world.demolishAnchorX = m_world.demolishAnchorZ = -1;
    m_world.demolishReleaseX = m_world.demolishReleaseZ = -1;
}
```

---

#### 6. Update `showDemolishConfirm()` body text in `ModalDialog.cpp`

`showDemolishConfirm(int tileCount)` already accepts a count. Update
`layoutDemolishConfirm()` body text (~line 320) from "Demolish [tileCount] tile(s)?
You can press Ctrl+Z to undo." to "Confirm Demolish? [tileCount] tile(s) will be
demolished. You can press Ctrl+Z to undo." — making it clear the number represents
the pre-counted occupied tiles in the selection, not merely "selected tiles".

No other changes to `ModalDialog` are required. `showDemolishConfirm`,
`layoutDemolishConfirm`, `DemolishConfirm` dialog type, and its keyboard handlers
all stay.

---

#### 7. Wire "Confirm before demolish" toggle from `SettingsPanel` to `UIManager`

`SettingsPanel::m_demolishConfirm` toggles but never calls back to `UIManager` — the
setting has no effect in production. Fix by adding a setter callback, mirroring the
keybindings pattern.

**`SettingsPanel.h`** — add a setter for the callback:

```cpp
// Set callback invoked whenever the "Confirm before demolish" toggle changes.
void setDemolishConfirmChangeFn(std::function<void(bool)> fn) {
    m_demolishConfirmChangeFn = fn;
}

private:
std::function<void(bool)> m_demolishConfirmChangeFn;
```

**`SettingsPanel.cpp`** — in the toggle click handler (~line 552), call the callback:

```cpp
m_demolishConfirm = !m_demolishConfirm;
if (m_demolishConfirmChangeFn) m_demolishConfirmChangeFn(m_demolishConfirm);
return true;
```

**`UIManager.cpp`** — in the constructor (after `m_settings` is created), wire it:

```cpp
m_settings->setDemolishConfirmChangeFn([this](bool enabled) {
    m_demolishConfirmEnabled = enabled;
});
```

---

#### 8. Update `architecture/ui-ux/input-arbitration.md` — Demolition Tool (two locations)

**Note**: This spec update was pre-applied during the fix-tech-implementation review.
Verify that both locations described below already contain the new drag-select
contract. If they do, no editing is required for this issue.

The spec currently describes the old modal flow in **two separate places** — both
must reflect the new drag-select + post-selection modal contract.

**Location A — inline paragraph at §7 World Interaction layer (~line 41)**

Replace (or verify) the **Demolition Tool** paragraph with:

> **Demolition Tool**: The demolish tool is activated via the Demolish button in the
> toolbar (or hotkey `X`). While Demolish is NOT the active tool, left-mouse events
> must NEVER trigger demolition logic. **Mouse-down** (LMB press): calls
> `pickTerrainTile()` to identify the hovered tile; if the ray-cast succeeds, records
> the anchor in `m_demolishAnchorX`/`m_demolishAnchorZ` and calls
> `IRenderer::setTileHoverHighlight(tileX, tileZ, 1)` for a demolish-red single-tile
> highlight; returns `true` (consumed). **Mouse-move while LMB held**: when `lmbHeld`
> and `demolishAnchorX != -1`, computes the axis-aligned rectangle from anchor to
> current hover tile, partitions tiles into `occupiedTiles` (`isZoned || isRoad ||
> serviceType != None`) and `emptyTiles`, calls
> `IRenderer::setTilePlacementPreview(occupiedTiles, kHoverArgbDemolish, emptyTiles)`,
> and clears single-tile hover via `setTileHoverHighlight(-1, -1)`. **Mouse-up** (LMB
> release): reads release tile from `m_world.hoveredTileX/Z`; counts occupied tiles in
> `[min(anchor,release), max(anchor,release)]`. If zero occupied tiles: clears state
> silently (no modal, no demolish). If one or more occupied tiles: when
> `m_demolishConfirmEnabled` is true, stores the release tile in `demolishReleaseX/Z`,
> calls `ModalDialog::showDemolishConfirm(N)` and sets `demolishModalPending`; when
> `m_demolishConfirmEnabled` is false, demolishes all occupied tiles immediately via
> `ICitySimulation::demolishTile()`, updates `overlayMap`, and calls
> `setZoneOverlay()`. **Modal poll** (`updateModalDialogState()`): when
> `demolishModalPending` is true and the modal closes, reads `pollResult()`; on Accept,
> iterates the stored rect and demolishes all occupied tiles; on Cancel, clears state
> without demolishing. **Tool switching / RMB cancel**: anchor and release state are
> cleared synchronously; `setTilePlacementPreview({}, 0, {})` and
> `clearDemolishHighlight()` are called.

**Location B — standalone `## Demolition Tool` section (~lines 189-244)**

Replace (or verify) the standalone section body with the same contract text as
Location A above.

---

#### 9. Update `architecture/ui-ux/modal-dialog-system.md` — demolish confirmation

Update the demolish confirmation entry to reflect the drag-select flow. The modal now
fires after a rectangular drag-selection, showing the count of occupied tiles:

- **Title**: "Confirm Demolish"
- **Body**: "Confirm Demolish? [N] tile(s) will be demolished. You can press Ctrl+Z
  to undo." — where N is the pre-counted number of occupied tiles in the selected
  rectangle.
- **Buttons**: **Yes** (primary) and **Cancel** (safe-exit, default Tab focus per
  global modal rule). Keyboard: Tab navigates, Enter activates focused, Escape
  activates Cancel.
- Modal is suppressed entirely if N = 0 (nothing to demolish).
- "Confirm before demolish" toggle in Settings > Gameplay controls whether this modal
  fires. When the toggle is OFF, tiles are demolished immediately on mouse-up.

---

#### 10. Replace demolition tests in `tests/ui/demolition_input_test.cpp`

Replace all four existing tests (which test the old single-tile click + same-tile
check flow) with the following tests covering the new drag-select behavior.

**Remove** (old modal contract, no longer exists):

- `DemolitionInput_MouseUp_SameTile_ConfirmModalOpened`
- `DemolitionInput_MouseUp_DifferentTile_NoModal`
- `DemolitionInput_ConfirmYes_CallsDemolishTile`
- `DemolitionInput_ConfirmNo_NoDemolition`

**Add** — the test file also needs a `mouseMove` helper alongside the existing
`mouseButtonDown`/`mouseButtonUp` helpers:

```cpp
static InputEvent mouseMove(int virtX, int virtY)
{
    InputEvent ev{};
    ev.type  = InputEvent::Type::MouseMove;
    ev.x     = virtX; ev.y     = virtY;
    ev.physX = virtX; ev.physY = virtY;
    return ev;
}
```

**Test 1** — drag over occupied tile, confirm ON → modal opens with correct count:

```cpp
// Drag on a single occupied tile (confirm ON) → modal shows "1 tile".
TEST_F(DemolitionInputTest, DemolishDragConfirmOn_OccupiedTile_ModalOpens)
{
    uiManager_->setDemolishConfirm(true);
    activateDemolishTool();

    stubPickTile(5, 5);
    ON_CALL(sim_, queryTile(5, 5)).WillByDefault([]{
        QueryResult q; q.isZoned = true; return q; });

    EXPECT_CALL(sim_, demolishTile(_, _)).Times(0);

    uiManager_->onEvent(mouseButtonDown(0, 500, 500));
    uiManager_->onEvent(mouseMove(500, 500));
    uiManager_->onEvent(mouseButtonUp(0, 500, 500));

    EXPECT_TRUE(uiManager_->hasActiveModal())
        << "Confirm modal must open after drag over occupied tile (confirm ON)";
}
```

**Test 2** — drag over empty tiles only → no modal, no demolish:

```cpp
// All tiles in selection are empty → no modal, no demolishTile call.
TEST_F(DemolitionInputTest, DemolishDrag_AllEmptyTiles_NoModalNoDemolish)
{
    uiManager_->setDemolishConfirm(true);
    activateDemolishTool();

    stubPickTile(3, 3);
    ON_CALL(sim_, queryTile(_, _)).WillByDefault([]{ return QueryResult{}; });

    EXPECT_CALL(sim_, demolishTile(_, _)).Times(0);

    uiManager_->onEvent(mouseButtonDown(0, 300, 300));
    uiManager_->onEvent(mouseMove(300, 300));
    uiManager_->onEvent(mouseButtonUp(0, 300, 300));

    EXPECT_FALSE(uiManager_->hasActiveModal())
        << "No modal must open when selection contains only empty tiles";
}
```

**Test 3** — confirm OFF, drag over occupied tile → immediate demolish, no modal:

```cpp
// Confirm OFF — demolishTile called immediately on mouse-up, no modal.
TEST_F(DemolitionInputTest, DemolishDrag_ConfirmOff_ImmediateDemolish)
{
    uiManager_->setDemolishConfirm(false);
    activateDemolishTool();

    stubPickTile(5, 5);
    ON_CALL(sim_, queryTile(5, 5)).WillByDefault([]{
        QueryResult q; q.isZoned = true; return q; });

    EXPECT_CALL(sim_, demolishTile(5, 5)).Times(1);
    EXPECT_CALL(renderer_, setZoneOverlay(_, _, _)).Times(AnyNumber());
    EXPECT_CALL(renderer_, clearDemolishHighlight()).Times(AnyNumber());

    uiManager_->onEvent(mouseButtonDown(0, 500, 500));
    uiManager_->onEvent(mouseMove(500, 500));
    uiManager_->onEvent(mouseButtonUp(0, 500, 500));

    EXPECT_FALSE(uiManager_->hasActiveModal());
}
```

**Test 4** — modal Accept → demolishes occupied tiles in rect:

```cpp
// Confirm ON, accept modal → demolishTile called for occupied tiles.
TEST_F(DemolitionInputTest, DemolishDrag_ConfirmOn_Accept_DemolishesTiles)
{
    uiManager_->setDemolishConfirm(true);
    activateDemolishTool();

    stubPickTile(5, 5);
    ON_CALL(sim_, queryTile(5, 5)).WillByDefault([]{
        QueryResult q; q.isZoned = true; return q; });

    uiManager_->onEvent(mouseButtonDown(0, 500, 500));
    uiManager_->onEvent(mouseMove(500, 500));
    uiManager_->onEvent(mouseButtonUp(0, 500, 500));

    ASSERT_TRUE(uiManager_->hasActiveModal());

    EXPECT_CALL(sim_, demolishTile(5, 5)).Times(1);
    EXPECT_CALL(renderer_, setZoneOverlay(_, _, _)).Times(AnyNumber());
    EXPECT_CALL(renderer_, clearDemolishHighlight()).Times(AnyNumber());

    // Accept the modal.
    // UIManager exposes acceptModal() for testing the Accept path.
    uiManager_->acceptModal();
    uiManager_->update(0.016f);
}
```

**Test 5** — modal Cancel → no demolish:

```cpp
// Confirm ON, cancel modal → demolishTile NOT called.
TEST_F(DemolitionInputTest, DemolishDrag_ConfirmOn_Cancel_NoDemolish)
{
    uiManager_->setDemolishConfirm(true);
    activateDemolishTool();

    stubPickTile(3, 7);
    ON_CALL(sim_, queryTile(3, 7)).WillByDefault([]{
        QueryResult q; q.isRoad = true; return q; });

    uiManager_->onEvent(mouseButtonDown(0, 500, 500));
    uiManager_->onEvent(mouseMove(500, 500));
    uiManager_->onEvent(mouseButtonUp(0, 500, 500));

    ASSERT_TRUE(uiManager_->hasActiveModal());

    EXPECT_CALL(sim_, demolishTile(_, _)).Times(0);
    EXPECT_CALL(renderer_, clearDemolishHighlight()).Times(AtLeast(1));

    uiManager_->closeModal();
    uiManager_->update(0.016f);
}
```

**Test 6** — Zone tool active → no demolish (regression guard):

```cpp
// Zone tool must NOT enter demolish code path.
TEST_F(DemolitionInputTest, DemolishDrag_ZoneTool_DoesNotTrigger)
{
    activateZoneTool();

    EXPECT_CALL(renderer_, pickTerrainTile(_, _, _, _))
        .WillRepeatedly(DoAll(SetArgReferee<2>(5), SetArgReferee<3>(5), Return(true)));
    EXPECT_CALL(sim_, demolishTile(_, _)).Times(0);
    EXPECT_CALL(sim_, placeZone(_, _, _, _, _)).Times(AnyNumber());
    ON_CALL(sim_, getTreasuryBalance()).WillByDefault(Return(100000.0f));

    uiManager_->onEvent(mouseButtonDown(0, 500, 500));
    uiManager_->onEvent(mouseMove(500, 500));
    uiManager_->onEvent(mouseButtonUp(0, 500, 500));

    EXPECT_FALSE(uiManager_->hasActiveModal());
}
```

Note: Test 4 calls `acceptModal()` — add this test-seam to `UIManager.h` alongside
`closeModal()`:

```cpp
// acceptModal — test seam: simulate player clicking the primary/Accept button
// of the currently active modal.
void acceptModal();
```

In `UIManager.cpp`, implement by calling the modal's internal accept path (sets
`m_lastResult = DialogResult::Accept` then closes modal):

```cpp
void UIManager::acceptModal() {
    if (m_modal) m_modal->acceptForTest();
}
```

And in `ModalDialog.h`/`.cpp` add:

```cpp
// acceptForTest — test seam: record Accept result and close modal.
void acceptForTest() {
    m_lastResult = DialogResult::Accept;
    closeModal();
}
```

---

#### 11. Extract demolish event handlers from `onEvent` to reduce cognitive complexity

`UIManager::onEvent` currently scores **728 CRITICAL** (spec limit: 25). Issues 2–4
rewrite the three demolish sub-handlers inside `onEvent` — extract them as private
helper methods consistent with the existing pattern (`doTerrainPlacement`,
`updateModalDialogState`, etc.).

Add three private method declarations to `UIManager.h`:

```cpp
// Demolish drag-select sub-handlers.
bool onDemolishMouseDown(int hitX, int hitZ);
void onDemolishMouseMove(int hitX, int hitZ);
bool onDemolishMouseUp();
```

In `UIManager.cpp`, implement as standalone methods and replace the three inlined
blocks in `onEvent` with single delegation calls:

```cpp
// MouseButtonDown:
if (m_world.activeTool == ActiveTool::Demolish)
    return onDemolishMouseDown(hitX, hitZ);

// MouseMove (inside the lmbHeld drag-preview chain):
else if (m_world.lmbHeld
         && m_world.activeTool == ActiveTool::Demolish
         && m_world.demolishAnchorX != -1)
    onDemolishMouseMove(hitX, hitZ);

// MouseButtonUp:
if (m_world.activeTool == ActiveTool::Demolish && m_world.demolishAnchorX != -1)
    return onDemolishMouseUp();
```

**Score targets** (verify with `python3 tools/cognitive_complexity.py`):

- `onDemolishMouseDown` ≤ 4
- `onDemolishMouseMove` ≤ 15
- `onDemolishMouseUp` ≤ 20 (higher budget due to the 3-path branch:
  zero-occupied / confirm-modal / immediate)
- `onEvent` must score strictly less than 728 (confirms extraction occurred)

Full reduction of `onEvent` to ≤ 25 is deferred to a dedicated refactor phase.

---

### Exit Criteria

- [ ] `make build` succeeds with no warnings.
- [ ] `ctest -LE "integration|requires-opengl"` passes.
- [ ] `ctest --test-dir build -R DemolishDrag --output-on-failure` discovers exactly
      6 tests, all passing.
- [ ] The four old `DemolitionInput_*` tests are **absent** from the test binary.
- [ ] Manual play test: activate Demolish tool, drag over a mixed area — occupied
      tiles show red preview, mouse-up opens "Confirm Demolish? N tile(s)" modal;
      accepting demolishes them; cancelling does not. With confirm OFF, mouse-up
      demolishes immediately.
- [ ] "Confirm before demolish" toggle in Settings > Gameplay takes effect
      immediately (no restart required).
- [ ] `python3 tools/cognitive_complexity.py src/ui/UIManager.cpp` —
      `onDemolishMouseDown` ≤ 4, `onDemolishMouseMove` ≤ 15,
      `onDemolishMouseUp` ≤ 20, `onEvent` < 728.
- [ ] `npx markdownlint-cli 'architecture/**/*.md' 'implementation/*.md' 'CLAUDE.md'`
      reports no errors.
