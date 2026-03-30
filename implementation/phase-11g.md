## Phase 11g: Per-Resolution Bitmap Font Switching

**Status: Planned**

### Goal

Replace the single fixed-size bitmap font set (`hud_font.xml` / `hud_mono_font.xml` at 22 px
physical cell height) with three resolution-tier font sets (22 px / 33 px / 44 px physical),
selected at startup based on the physical screen height. This ensures UI text scales with screen
resolution: at 720 p text renders at 22 px (the current minimum-viable size); at 1080 p it
renders at 33 px; at 1440 p and above it renders at 44 px. The virtual coordinate layout
(`kLineH = 33` virtual px) remains unchanged — only the physical font glyphs grow to match the
display.

**Background**: Phase 11 (and the Phase 11c/11d/11f fix pass) established `kLineH = 33` virtual
pixels as the worst-case row height that prevents bitmap font overlap at 720 p
(`ceil(22 × 1080 / 720) = 33`). At 1080 p a 22 px physical font is readable but small; at
1440 p+ it is noticeably undersized. True per-resolution font switching was deferred from those
passes with a note in `architecture/ui-ux/query-inspector-panel.md`: *"Dynamic per-frame
font-size switching is deferred to a future typography pass."* This phase implements that
deferred pass — at startup (not per-frame).

---

### Deliverables

#### 1. Spec Update — `architecture/ui-ux/resolution-ui-scaling.md`

- [x] Add a **Bitmap Font Tier Selection** section documenting the three resolution tiers,
  physical font sizes, and the selection rule:

  ```text
  screen height < 900 px  → 720p tier  → 22 px physical cell font
  900 ≤ height < 1260 px  → 1080p tier → 33 px physical cell font
  height ≥ 1260 px        → 1440p tier → 44 px physical cell font
  ```

  Boundaries chosen so that 720 p (720 px) and 768 p (768 px) use the 720p tier; 1080 p
  (1080 px) uses the 1080p tier; 1440 p (1440 px) and 4 K (2160 px) use the 1440p tier.
  (`gamedesign-ux`)

- [x] Document that `kLineH = 33` virtual pixels is invariant across all tiers. Place the full
  math proof in the **new Bitmap Font Tier Selection section in
  `architecture/ui-ux/resolution-ui-scaling.md`** only:
  - `22 × 1080 / 720 = 33.0`
  - `33 × 1080 / 1080 = 33.0`
  - `44 × 1080 / 1440 = 33.0`

  Do **not** repeat the full proof in `query-inspector-panel.md`; that file only needs the
  Panel Layout Constants table row for `kLineH` updated to read "valid for all three
  resolution tiers (720p/1080p/1440p)" (see Deliverable 2). No layout constant changes are
  required in any panel. (`gamedesign-ux`)

- [x] Document that font tier selection occurs once at `IrrlichtUIBackend` construction
  (before any UI element is created) and is NOT dynamic — resolution changes during a
  session are not supported in V1. (`gamedesign-ux`, `graphics-dev-irrlicht`)

- [x] Update the existing **Bitmap Font Physical Size** section in
  `architecture/ui-ux/resolution-ui-scaling.md` to replace **all occurrences** of
  `tools/generate_hud_font.py` with `tools/generate_bitmap_fonts.py`, and note that
  the new script now generates all six tier-specific font pairs (22 px, 33 px, 44 px for
  both proportional and mono typefaces) rather than the original single-size pair.
  (`gamedesign-ux`)

- [x] Add a **Font Atlas Exception** note to `architecture/asset-standards/2d-texture-standards.md`
  **immediately before the `## DDS Authoring Pipeline` section heading**, stating that
  Irrlicht's `CGUIFont` bitmap font loader reads PNG natively (not via
  `IVideoDriver::getTexture()`) and font atlas PNGs are exempt from the DDS-only runtime
  texture rule. No DDS conversion is required for files under `assets/fonts/`.
  (`graphics-dev-irrlicht`, `graphics-artist-2d-texture`)

#### 2. Spec Update — `architecture/ui-ux/query-inspector-panel.md`

- [x] **Remove the entire "Font scaling note" paragraph** (currently lines 85–89 of
  `query-inspector-panel.md`) and replace it with a single sentence: "For per-resolution
  font tier selection (22 px / 33 px / 44 px depending on screen height), see the Bitmap
  Font Tier Selection section in `architecture/ui-ux/resolution-ui-scaling.md`."
  (`gamedesign-ux`)

- [x] Update the Panel Layout Constants table to clarify that `kLineH = 33` is valid for all
  three resolution tiers (not only 720 p as currently noted). (`gamedesign-ux`)

#### 3. Spec Update — `architecture/ui-ux/hud-layout.md`

- [x] Add a **Font Tier Assets** subsection to the existing **Font Loading** section in
  `hud-layout.md` documenting the six asset paths:
  - `assets/fonts/hud_font_720.xml` + `assets/fonts/hud_font_720.png` (22 px)
  - `assets/fonts/hud_font_1080.xml` + `assets/fonts/hud_font_1080.png` (33 px)
  - `assets/fonts/hud_font_1440.xml` + `assets/fonts/hud_font_1440.png` (44 px)
  - `assets/fonts/hud_mono_font_720.xml` + `assets/fonts/hud_mono_font_720.png` (22 px)
  - `assets/fonts/hud_mono_font_1080.xml` + `assets/fonts/hud_mono_font_1080.png` (33 px)
  - `assets/fonts/hud_mono_font_1440.xml` + `assets/fonts/hud_mono_font_1440.png` (44 px)

  The existing `assets/fonts/hud_font.xml` and `assets/fonts/hud_mono_font.xml` are
  retained as the 720p-tier copies (symlinks or duplicates) for backwards compatibility
  with any hard-coded references in Phase 8 code that cannot be updated in this phase.
  (`gamedesign-ux`)

- [x] Update the **Font Loading** Implementation paragraph in `hud-layout.md` to replace
  `m_monoFont` with `m_hudMonoFont` and update the constructor load description to reference
  the tier-specific font paths (`hud_mono_font_720.xml` etc.) rather than the bare
  `hud_mono_font.xml` name. (`gamedesign-ux`)

#### 4. Font Asset Generation — `tools/generate_bitmap_fonts.py`

- [x] Author `tools/generate_bitmap_fonts.py` that generates Irrlicht-compatible bitmap font
  XML + PNG pairs for each tier. The script uses Pillow (`PIL`) to render a TTF source font
  (DejaVu Sans and DejaVu Sans Mono from `assets/fonts/source/`) at the target physical
  pixel cell heights (22, 33, 44 px) and writes:
  - `assets/fonts/hud_font_720.xml` / `hud_font_720.png`
  - `assets/fonts/hud_font_1080.xml` / `hud_font_1080.png`
  - `assets/fonts/hud_font_1440.xml` / `hud_font_1440.png`
  - `assets/fonts/hud_mono_font_720.xml` / `hud_mono_font_720.png`
  - `assets/fonts/hud_mono_font_1080.xml` / `hud_mono_font_1080.png`
  - `assets/fonts/hud_mono_font_1440.xml` / `hud_mono_font_1440.png`

  The XML format follows the Irrlicht `CGUIFont` bitmap font schema:
  `<font type="bitmap"><Texture file="..." />` with per-character `<c .../>` entries.
  (`graphics-dev-irrlicht`, `graphics-artist-2d-texture`)

  > **Note:** Irrlicht's `CGUIFont` bitmap font loader reads PNG natively via its own
  > font-atlas path — it does not use `IVideoDriver::getTexture()`. These font atlas PNGs
  > are not game textures and are exempt from the project's DDS-only runtime texture rule.
  > No DDS conversion is required.

- [x] The script accepts `--cell-heights 22,33,44` and `--output-dir assets/fonts` as
  arguments, with sensible defaults. Running `python3 tools/generate_bitmap_fonts.py`
  without arguments regenerates all six pairs. The script requires Python 3 with Pillow
  (`pip install Pillow`). Add `python3-pil` (or `python3-pillow`) to
  `.devcontainer/Dockerfile` so all contributors can run the script without manual
  dependency installation. The font generation script is a developer tool only — CI uses
  the pre-generated committed PNG files and does not run the script. (`graphics-dev-irrlicht`)

- [x] Source TTF files are committed under `assets/fonts/source/`:
  - `DejaVuSans.ttf` (SIL OFL 1.1 — compatible with the project's open-source posture)
  - `DejaVuSansMono.ttf`

  Both files are ≤700 KB; total source asset addition ≤1.4 MB. (`graphics-artist-2d-texture`)

- [x] Generated PNG files use RGBA8 (Pillow `RGBA` mode) with white glyphs on a transparent
  background, compatible with Irrlicht's `EBF_MIPMAP` font loading path. Font atlas PNGs
  are alpha-coverage masks (white glyphs on transparent background) and do NOT require
  embedded sRGB ICC profiles; use Pillow's default PNG output without `icc_profile`
  embedding. (`graphics-dev-irrlicht`)

#### 5. `IrrlichtUIBackend` — Font Tier Selection

- [x] Add a `FontTier` enum in `src/ui/IrrlichtUIBackend.h`:

  ```cpp
  enum class FontTier { k720p, k1080p, k1440p };
  static FontTier selectFontTier(int screenHeight);
  ```

  `selectFontTier` is a `static` pure function (testable without a device):
  - `screenHeight < 900` → `k720p`
  - `900 ≤ screenHeight < 1260` → `k1080p`
  - `screenHeight ≥ 1260` → `k1440p`

  (`graphics-dev-irrlicht`)

- [x] In `IrrlichtUIBackend::IrrlichtUIBackend(...)` constructor, call `selectFontTier` with
  `getScreenHeight()` and load the matching font pair via
  `m_env->getFont("assets/fonts/hud_font_<tier>.xml")` and
  `m_env->getFont("assets/fonts/hud_mono_font_<tier>.xml")`. Store as
  `m_hudFont` and `m_hudMonoFont` (`irr::gui::IGUIFont*`). (`graphics-dev-irrlicht`)

  > **Naming clarification:** `m_hudFont` is a new `irr::gui::IGUIFont*` member (not
  > previously present in `IrrlichtUIBackend`). The existing `m_monoFont` member is
  > renamed to `m_hudMonoFont` in the same commit to match the tier-aware naming
  > convention. All existing code that references `m_monoFont` must be updated to
  > `m_hudMonoFont` as part of this deliverable.

- [x] Apply `m_hudFont` as the environment default font via
  `m_env->getSkin()->setFont(m_hudFont)` immediately after loading, so all
  subsequently created `IGUIStaticText` elements inherit the correct tier font without
  per-element font assignment. (`graphics-dev-irrlicht`)

- [x] `m_hudMonoFont` is applied per-element via the existing `setElementMonoFont(handle)`
  `IUIBackend` method; no new interface methods are required. (`graphics-dev-irrlicht`)

- [x] EDT_NULL fallback: when the device driver is `EDT_NULL` (headless CI), skip font loading
  — `m_hudFont` and `m_hudMonoFont` remain `nullptr`. This preserves existing headless
  test behaviour. (`graphics-dev-irrlicht`)

#### 6. CI Asset Verification — `tools/validate_assets.py` Check #31

- [x] Add **Check #31** to `tools/validate_assets.py`: verify all six font XML files and their
  paired PNG files exist under `assets/fonts/`:
  - `hud_font_720.xml`, `hud_font_720.png`
  - `hud_font_1080.xml`, `hud_font_1080.png`
  - `hud_font_1440.xml`, `hud_font_1440.png`
  - `hud_mono_font_720.xml`, `hud_mono_font_720.png`
  - `hud_mono_font_1080.xml`, `hud_mono_font_1080.png`
  - `hud_mono_font_1440.xml`, `hud_mono_font_1440.png`

  The check exits non-zero if any file is missing. (`cicd-dev-github`)

- [x] Add a `Verify check_31 present` guard step to the `validate-assets` job in
  `.github/workflows/_validate-assets.yml`: grep `tools/validate_assets.py` for the `check_31` function
  symbol; exit 1 with a descriptive error message if not found. Follow the same pattern as
  the Phase 11d guard steps. (`cicd-dev-github`)

#### 7. Unit Tests — `FontTierSelectionTest` and EDT_NULL Constructor Test

- [x] Add `tests/ui/font_tier_test.cpp` containing `FontTierSelectionTest` with at least the
  following cases:

  | Test name | Input `screenHeight` | Expected `FontTier` |
  |---|---|---|
  | `Below720p_Uses720pTier` | 480 | `k720p` |
  | `At720p_Uses720pTier` | 720 | `k720p` |
  | `At768p_Uses720pTier` | 768 | `k720p` |
  | `At899p_Uses720pTier` | 899 | `k720p` |
  | `At900p_Uses1080pTier` | 900 | `k1080p` |
  | `At1080p_Uses1080pTier` | 1080 | `k1080p` |
  | `At1259p_Uses1080pTier` | 1259 | `k1080p` |
  | `At1260p_Uses1440pTier` | 1260 | `k1440p` |
  | `At1440p_Uses1440pTier` | 1440 | `k1440p` |
  | `At2160p_Uses1440pTier` | 2160 | `k1440p` |

  All 10 tests call `IrrlichtUIBackend::selectFontTier(screenHeight)` directly (static method,
  no device required; unit label). (`test-dev-cpp`)

- [x] Add a test case `IrrlichtUIBackend_EDT_NULL_ConstructorSkipsFontLoadingGracefully` in
  `font_tier_test.cpp` (or a separate EDT_NULL test file) to verify constructor behavior
  on headless devices:
  - Construct `IrrlichtUIBackend` with an `EDT_NULL` device (headless, no font loading required).
  - Assert that `m_hudFont == nullptr` and `m_hudMonoFont == nullptr` after construction.
  - Assert no crash occurs during construction or destruction.
  - This test runs under the `unit` label (no display required; EDT_NULL is headless-safe).
  - The live-device constructor verification (applying correct tier font on real OpenGL device)
    is covered by the existing `requires-opengl` integration test suite (Phase 8 or later;
    no new test required here). (`test-dev-cpp`)

- [x] Add `font_tier_test.cpp` to the `ui_tests` CMake target. (`test-dev-cpp`)

---

### Exit Criteria

- [ ] `architecture/ui-ux/resolution-ui-scaling.md` documents the three font tiers, physical
  sizes, boundary values, and the `kLineH = 33` invariant.
- [ ] `architecture/ui-ux/query-inspector-panel.md` deferral note replaced with Phase 11g
  reference; Panel Layout Constants table updated.
- [ ] `architecture/ui-ux/hud-layout.md` Font Tier Assets subsection present with all six
  asset paths.
- [ ] `tools/generate_bitmap_fonts.py` present; running it regenerates all six XML + PNG pairs
  without error (requires Pillow and source TTF files).
- [ ] All six tier-specific font XML + PNG pairs (12 files) committed under `assets/fonts/`;
  backward-compatibility files (`hud_font.xml`, `hud_mono_font.xml`) exist as symlinks or
  copies of the 720p tier files.
- [ ] Source TTF files (`DejaVuSans.ttf`, `DejaVuSansMono.ttf`) committed under
  `assets/fonts/source/`.
- [ ] `IrrlichtUIBackend::selectFontTier(int)` static method present and matches the
  boundary table.
- [ ] `IrrlichtUIBackend` constructor: (a) EDT_NULL path skips font loading gracefully
  (`m_hudFont == nullptr`, `m_hudMonoFont == nullptr`, no crash); verified by
  `IrrlichtUIBackend_EDT_NULL_ConstructorSkipsFontLoadingGracefully` unit test.
  (b) Live-device constructor loads and applies the correct font tier; verified by the
  existing `requires-opengl` integration test suite (not a new test required).
- [ ] `tools/validate_assets.py` Check #31 present and exits zero with all six pairs present.
- [ ] All 10 `FontTierSelectionTest` boundary cases pass, plus the EDT_NULL constructor test
  (`IrrlichtUIBackend_EDT_NULL_ConstructorSkipsFontLoadingGracefully`).
- [ ] `architecture/asset-standards/2d-texture-standards.md` Font Atlas Exception note present
  immediately before the `## DDS Authoring Pipeline` section heading.
- [ ] `architecture/ui-ux/hud-layout.md` Font Loading Implementation paragraph updated to
  reference `m_hudMonoFont` (renamed from `m_monoFont` in Phase 11g) and tier-specific font
  paths.
- [ ] `npx markdownlint-cli 'architecture/**/*.md' 'implementation/*.md' 'CLAUDE.md'` exits
  zero.

---

### Team

| Role | Responsibility |
|---|---|
| `gamedesign-ux` | Author spec updates in `resolution-ui-scaling.md`, `query-inspector-panel.md`, `hud-layout.md`; sign off on tier boundary values and `kLineH` invariant documentation |
| `graphics-artist-2d-texture` | Commit source TTF files; review generated PNG glyph quality for all three tiers; sign off on visual result |
| `graphics-dev-irrlicht` | Author `tools/generate_bitmap_fonts.py`; implement `FontTier` enum + `selectFontTier()` in `IrrlichtUIBackend`; wire font loading in constructor; co-author the font tier selection timing documentation in `resolution-ui-scaling.md` (Deliverable 1c) |
| `test-dev-cpp` | Author `font_tier_test.cpp`; add to `ui_tests` CMake target |
| `cicd-dev-github` | Implement `validate_assets.py` Check #31; verify `validate-assets` CI job exits zero after all font assets are committed |

---

### Dependencies

- Requires Phase 11 complete: `IrrlichtUIBackend` full 17-method implementation must be in
  place (`setElementMonoFont` is used for mono font per-element application).
- No dependency on Phase 11f or Phase 12 — can be implemented in parallel with Phase 12
  Polish pass.
- Post-V1 extension: dynamic font reload on resolution change (e.g., toggling fullscreen)
  is deferred to a post-V1 accessibility pass.
