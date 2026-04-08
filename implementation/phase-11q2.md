## Phase 11q2: Extract `IVehicleRenderer` from `IRenderer`

**Status: Planned**

**Prerequisite**: none. Independent of phase-11q1.
**Blocks**: phase-11q3 (IrrlichtRenderer split depends on this interface).

### Goal

`IRenderer` has 36 declared methods — one over the SonarCloud `cpp:S1448` threshold
of 35. Extract the 9 vehicle / traffic agent / service coverage overlay methods into
a new pure-virtual interface `IVehicleRenderer`. `IRenderer` inherits from it, so
**all existing callers that hold `IRenderer*` are untouched** — they still reach all
9 methods via the inheritance chain. Zero call-site changes required.

---

### Methods to Move

The following 9 methods leave `IRenderer` and are declared in `IVehicleRenderer`:

| Method | Defined at (IRenderer.h) |
|---|---|
| `spawnVehicleAgent(AgentHandle, int, int, ZoneType)` | Phase 11d traffic agent API |
| `moveVehicleAgent(AgentHandle, float, float, float)` | Phase 11d traffic agent API |
| `despawnVehicleAgent(AgentHandle)` | Phase 11d traffic agent API |
| `setIntersectionSignalState(int, int, SignalPhase)` | Phase 11d traffic agent API |
| `showServiceCoverageOverlay(int, int, ServiceBuildingType, bool)` | Phase 11d service coverage API |
| `hideServiceCoverageOverlay()` | Phase 11d service coverage API |
| `placeVehicle(uint32_t, const std::string&, float, float, float, float)` | Phase 10 vehicle rendering API |
| `moveVehicle(uint32_t, float, float, float, float)` | Phase 10 vehicle rendering API |
| `removeVehicle(uint32_t)` | Phase 10 vehicle rendering API |

After extraction `IRenderer` declares 27 methods (below the 35-method threshold).

---

### Deliverables

---

#### 1. Create `src/interfaces/IVehicleRenderer.h`

New pure-virtual interface with exactly the 9 methods listed above, plus `virtual
~IVehicleRenderer() = default;`. Required includes: `simulation_types.h` (for
`AgentHandle`, `ZoneType`, `SignalPhase`, `ServiceBuildingType`).

The full interface:

```cpp
#pragma once
#include "simulation_types.h"
#include <string>
#include <cstdint>

// IVehicleRenderer — vehicle, traffic agent, and service coverage overlay methods
// extracted from IRenderer to keep class method counts within SonarCloud S1448 limits.
// IRenderer inherits this interface so all IRenderer* callers reach these methods
// without any call-site changes.
// main-thread-only.
class IVehicleRenderer {
public:
    virtual ~IVehicleRenderer() = default;

    // --- Phase 11d: Traffic agent API ---
    virtual void spawnVehicleAgent(AgentHandle handle,
                                   int tileX, int tileZ,
                                   ZoneType zone) = 0;
    virtual void moveVehicleAgent(AgentHandle handle,
                                  float worldX, float worldZ,
                                  float headingDeg) = 0;
    virtual void despawnVehicleAgent(AgentHandle handle) = 0;
    virtual void setIntersectionSignalState(int tileX, int tileZ,
                                            SignalPhase phase) = 0;

    // --- Phase 11d: Service coverage overlay API ---
    virtual void showServiceCoverageOverlay(int tileX, int tileZ,
                                            ServiceBuildingType type,
                                            bool degraded) = 0;
    virtual void hideServiceCoverageOverlay() = 0;

    // --- Phase 10: Legacy vehicle rendering API ---
    virtual void placeVehicle(uint32_t vehicleId,
                              const std::string& assetName,
                              float worldX, float worldY, float worldZ,
                              float yawDegrees) = 0;
    virtual void moveVehicle(uint32_t vehicleId,
                             float worldX, float worldY, float worldZ,
                             float yawDegrees) = 0;
    virtual void removeVehicle(uint32_t vehicleId) = 0;
};
```

- [ ] Create `src/interfaces/IVehicleRenderer.h` with the content above.

---

#### 2. Update `src/interfaces/IRenderer.h`

- [ ] Add `#include "IVehicleRenderer.h"` near the top of `IRenderer.h`.
- [ ] Change the class declaration line from `class IRenderer {` to
  `class IRenderer : public IVehicleRenderer {`.
- [ ] Delete the 9 method declarations that now live in `IVehicleRenderer`. Remove
  the entire "Phase 11d — Traffic agent rendering API", the "Phase 11d —
  Service coverage overlay API", and the "Phase 10 — Vehicle rendering API"
  blocks from `IRenderer.h` — the section comments migrate to
  `IVehicleRenderer.h`.
- [ ] Verify the remaining method count in `IRenderer` is 27 (including
  `setZoneHoverColour` default-impl).

---

#### 3. Update `MockRenderer` in tests

- [ ] Locate `MockRenderer` (search `tests/` for `class MockRenderer`).
- [ ] If `MockRenderer` declares the 9 moved methods as `MOCK_METHOD` overrides,
  those overrides are already inherited from `IVehicleRenderer` through `IRenderer`
  — no change needed *unless* the mock explicitly re-declares them (which would
  cause a compile error due to ambiguous override). If they are re-declared,
  verify they still compile and the override tag is correct.
- [ ] Run `make build` and fix any compiler errors in test files before the next step.

---

#### 4. Verify no other files declare a mock or stub of `IRenderer`

- [ ] `grep -r "IRenderer" tests/ src/ --include="*.h" --include="*.cpp"` — check
  every file that mentions `IRenderer`. Confirm each either:
  - includes `IRenderer.h` and uses the interface (fine — inherits `IVehicleRenderer`
    automatically), or
  - derives from `IRenderer` (fine — picks up `IVehicleRenderer` via base class).
  No file should need manual changes unless it independently re-declares the 9 methods.

---

### Exit Criteria

- [ ] `npx markdownlint-cli 'implementation/phase-11q2.md'` — no errors.
- [ ] All deliverable checkboxes above are checked.
- [ ] `make build` passes with zero new warnings.
- [ ] `ctest -LE "integration|requires-opengl"` — zero regressions.
- [ ] `ctest -L "^integration$"` — zero regressions.
- [ ] `xvfb-run --auto-servernum ctest -L "^requires-opengl$"` — zero regressions.
- [ ] `IRenderer.h` declares exactly 27 methods (count manually or via grep).
- [ ] `IVehicleRenderer.h` declares exactly 9 pure-virtual methods.
- [ ] SonarCloud re-scan shows `cpp:S1448` resolved on `src/interfaces/IRenderer.h`.
