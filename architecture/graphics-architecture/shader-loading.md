# Shader Loading

## Prerequisites and Null Checks

Before calling `addHighLevelShaderMaterialFromFiles()`, obtain `IGPUProgrammingServices* gpu = driver->getGPUProgrammingServices()` and null-check it: if `gpu == nullptr` (driver does not support GPU programs, e.g. `EDT_NULL` or software rasterizer), log an error and return early — do not call `addHighLevelShaderMaterialFromFiles()`. Under `EDT_NULL`, `getGPUProgrammingServices()` returns null — all shader load paths must be guarded. Under a non-`EDT_NULL` driver, a null return is a fatal configuration error in debug builds.

```cpp
IGPUProgrammingServices* gpu = driver->getGPUProgrammingServices();
if (!gpu) {
    // EDT_NULL or software rasterizer: GPU programs are not supported.
    // Log and return; callers must handle a missing/invalid material type.
    LOG_ERROR("getGPUProgrammingServices() returned null — shader loading skipped.");
    return;
}
// Safe to call gpu->addHighLevelShaderMaterialFromFiles() below.
```

- Terrain splatting shader loaded via `IVideoDriver::addHighLevelShaderMaterialFromFiles()`
- **Shader callback lifetime** — **Irrlicht DOES take a reference to the callback (verified)**: Irrlicht's material renderer internally calls `grab()` on the `IShaderConstantSetCallBack` when passed to `addHighLevelShaderMaterialFromFiles()`, and calls `drop()` on its own destruction. **This behavior has been verified against both Irrlicht Tutorial 10 and the Irrlicht source (`source/Irrlicht/COpenGLSLMaterialRenderer.cpp`)**; it is a confirmed, documented fact — not an assumption or an inferred pattern. Tutorial 10 explicitly demonstrates raw allocation followed by `->drop()` after passing to `addHighLevelShaderMaterialFromFiles`, and `COpenGLSLMaterialRenderer` calls `CallBack->grab()` in its constructor and `CallBack->drop()` in its destructor. Therefore, use **raw heap allocation + `->drop()` after passing** — Irrlicht's reference count keeps the callback alive for exactly as long as the material renderer exists. **Do NOT use `std::unique_ptr` or `std::vector<std::unique_ptr<...>>`** — `unique_ptr` calls `delete` directly (bypassing `IReferenceCounted`), causing a double-free when Irrlicht also drops the callback. There is no `m_shaderCallbacks` member in `RenderSystem`.

  ```cpp
  // Correct pattern — matches Irrlicht Tutorial 10:
  // IMPORTANT: Capture all data needed for error messages as LOCAL VARIABLES BEFORE
  // calling addHighLevelShaderMaterialFromFiles. After cb->drop(), 'cb' may be destroyed
  // and must NEVER be accessed again. The path strings vsFile/fsFile are already local
  // to the caller and safe to use in error handling below.
  MyTerrainShaderCallback* cb = new MyTerrainShaderCallback();
  s32 matType = gpu->addHighLevelShaderMaterialFromFiles(
      vsFile, "main", video::EVST_VS_1_1,
      fsFile, "main", video::EPST_PS_1_1,
      cb, video::EMT_SOLID);
  cb->drop(); // Always drop our ref unconditionally.
              // On success: Irrlicht called grab() internally; ref_count remains 1 (held by renderer).
              // On failure (matType == -1): Irrlicht did NOT call grab(); drop() reduces ref_count 1→0,
              // destroying 'cb' immediately. 'cb' is now a dangling pointer — do NOT dereference it.
  if (matType == -1) {
      // Error handling — 'cb' IS DESTROYED here (on the failure path drop() reduced ref_count to 0).
      // Using cb->anything() here is a use-after-free.
      // Use only pre-captured local variables (vsFile, fsFile) for error messages.
      LOG("Shader compile failed: vs=" + vsFile + " fs=" + fsFile);
  }
  ```

  The material renderers are owned by `IVideoDriver`, which is owned by `IrrlichtDevice`, owned by `RenderSystem` — the lifetime chain is already correct without manual management. **Failure path safety**: `cb->drop()` is called unconditionally BEFORE the `-1` check. On a failure return, Irrlicht does not call `grab()`, so `drop()` reduces the ref_count from 1 to 0 and **destroys the callback** — it is not merely "may be destroyed", it IS destroyed. Any error-handling code in the `matType == -1` branch that references `cb` is a use-after-free. Error messages must be constructed from local path variables captured before the call, not from callback state.

  **sRGB texture binding in shader callbacks**: Because Irrlicht manages texture unit bindings internally during `drawAll()`, raw `glActiveTexture()` + `glBindTexture()` calls for sRGB diffuse textures (raw `GLuint`) must be made **inside `IShaderConstantSetCallBack::OnSetConstants()`** — not between `drawAll()` calls. GL calls made within `OnSetConstants()` execute immediately before the associated draw call and are not overridden by Irrlicht's internal state management. The shader callback receives the active `IVideoDriver*`; `textureCache->getSRGBGLuint(filename)` provides the raw `GLuint` for the sRGB texture to bind. See texture-cache.md for the full sRGB upload and binding specification.

  **CRITICAL — save and restore GL_ACTIVE_TEXTURE inside OnSetConstants()**: Irrlicht's OpenGL renderer tracks its own `m_CurrentTexture` and active texture unit state. Calling `glActiveTexture()` inside `OnSetConstants()` changes the driver's active unit without updating Irrlicht's internal tracking, corrupting subsequent Irrlicht draw calls (Irrlicht may bind textures to the wrong unit on the next `setMaterial()` call). **Required pattern** — save and restore the previously active texture unit:

  ```cpp
  void OnSetConstants(IMaterialRendererServices* services, s32 userData) override {
      // Save Irrlicht's current active texture unit state:
      GLint savedUnit = 0;
      glGetIntegerv(GL_ACTIVE_TEXTURE, &savedUnit);

      // Bind sRGB diffuse texture to unit 0 (kTexUnitDiffuse):
      glActiveTexture(GL_TEXTURE0);  // unit 0
      glBindTexture(GL_TEXTURE_2D, m_srgbDiffuseTexId);

      // ... set uniform samplers, etc. ...

      // Unbind the sRGB texture BEFORE restoring Irrlicht's active unit.
      // This MUST be done here — inside OnSetConstants() — because Irrlicht provides
      // no post-draw callback. "After the draw call" is structurally impossible:
      // OnSetConstants() is the ONLY hook available before the draw, and there is
      // no IShaderConstantSetCallBack::OnPostDraw() equivalent. The unbind must
      // occur while GL_TEXTURE0 is still active (before the restore below).
      glBindTexture(GL_TEXTURE_2D, 0);  // unbind from unit 0 while it is still current

      // Restore Irrlicht's active texture unit before returning:
      // IMPORTANT: glGetIntegerv(GL_ACTIVE_TEXTURE, &savedUnit) returns the GL enum
      // GL_TEXTUREi (e.g., 0x84C0 for unit 0, not integer 0). Passing savedUnit
      // directly to glActiveTexture() is correct — do NOT convert to integer offset.
      // GL_ACTIVE_TEXTURE always returns the full GLenum, not the unit index.
      glActiveTexture(static_cast<GLenum>(savedUnit));
  }
  ```

  **Important**: Do NOT add an "after the draw call" unbind step — `IShaderConstantSetCallBack` has no post-draw callback. All GL state setup AND cleanup for sRGB textures must occur within `OnSetConstants()`. The pattern above (bind → set uniforms → unbind → restore unit) is the complete and correct sequence.

  Without this save/restore, Irrlicht's internal active-unit tracking is corrupted starting from the first frame that uses a custom sRGB shader, producing intermittent texture-on-wrong-unit artifacts that are difficult to reproduce.

  **Terrain Splat Shader — 5-Unit Binding Sequence in OnSetConstants()**

  The terrain splat shader binds 5 raw `GLuint` textures simultaneously in `OnSetConstants()`:

  - Unit 4: splat map (`getSplatMapGLuint()`)
  - Unit 5: terrain detail layer 0 / R-channel (`getSRGBGLuint()`, biome base/grass)
  - Unit 6: terrain detail layer 1 / G-channel (`getSRGBGLuint()`, asphalt)
  - Unit 7: terrain detail layer 2 / B-channel (`getSRGBGLuint()`, soil)
  - Unit 8: terrain detail layer 3 / A-channel (`getSRGBGLuint()`, concrete)

  Required binding sequence (must be in exactly this order):

  ```cpp
  // Save current active unit
  GLint savedUnit;
  glGetIntegerv(GL_ACTIVE_TEXTURE, &savedUnit);

  // Bind splat map (unit 4, linear upload — NOT sRGB)
  glActiveTexture(GL_TEXTURE0 + kTexUnitSplatMap);  // = 4
  glBindTexture(GL_TEXTURE_2D, tc->getSplatMapGLuint(m_splatPath));

  // Bind 4 sRGB terrain detail layers (units 5–8)
  for (int i = 0; i < 4; ++i) {
      glActiveTexture(GL_TEXTURE0 + kTexUnitTerrainLayer0 + i);  // = 5,6,7,8
      glBindTexture(GL_TEXTURE_2D, tc->getSRGBGLuint(m_detailPaths[i]));
  }

  // Set sampler uniforms AFTER all bindings
  int unit4 = kTexUnitSplatMap;  // 4
  services->setPixelShaderConstant("u_splatMap", &unit4, 1);
  for (int i = 0; i < 4; ++i) {
      int unit = kTexUnitTerrainLayer0 + i;  // 5–8
      const char* names[] = {"u_layer0","u_layer1","u_layer2","u_layer3"};
      services->setPixelShaderConstant(names[i], &unit, 1);
  }

  // Restore saved active unit
  glActiveTexture(static_cast<GLenum>(savedUnit));
  ```

  **Do NOT unbind individual units** before restoring — Irrlicht's driver state machine
  tracks active texture units internally. Restoring `GL_ACTIVE_TEXTURE` is sufficient.
  The `GL_ACTIVE_TEXTURE` save/restore pattern is mandatory for ALL raw-GL texture bindings
  in `OnSetConstants()` to prevent corrupting Irrlicht's internal texture state.

  **sRGB Gamma Fallback — Uniform Bool Approach**

  When `GL_EXT_texture_sRGB` is absent (`RenderSystem::isSRGBTextureSupported()` returns `false`),
  terrain textures are uploaded as linear (not sRGB). The fragment shader must apply a manual
  `pow(color.rgb, vec3(2.2))` gamma correction.

  **Mechanism: uniform bool `u_srgbLinear`** (not two shader variants).

  In `OnSetConstants()`:

  ```cpp
  bool srgbLinear = !m_renderer->isSRGBTextureSupported();
  // Irrlicht setPixelShaderConstant passes bool as int (1 = true, 0 = false)
  int srgbLinearInt = srgbLinear ? 1 : 0;
  services->setPixelShaderConstant("u_srgbLinear", &srgbLinearInt, 1);
  ```

  In the terrain fragment shader:

  ```glsl
  uniform bool u_srgbLinear;
  // ...
  vec4 color = texture(u_layer0, uv) * splatWeights.r
             + texture(u_layer1, uv) * splatWeights.g
             + texture(u_layer2, uv) * splatWeights.b
             + texture(u_layer3, uv) * splatWeights.a;
  if (u_srgbLinear) {
      color.rgb = pow(color.rgb, vec3(2.2));
  }
  gl_FragColor = color;
  ```

  **Why not two shader variants**: selecting between shader variants at runtime requires
  storing two `s32` material IDs and branching in the render path. The uniform bool
  is evaluated once per draw call on modern drivers with negligible overhead.

  **Note on shader version enums**: Irrlicht's GLSL backend **ignores** the `EVST_VS_*` / `EPST_PS_*` enum values entirely when compiling OpenGL GLSL shaders. These enums are meaningful only for the Direct3D HLSL backend. For GLSL, the active GLSL version is determined exclusively by the `#version` directive in the shader source file itself. Use `EVST_VS_1_1` / `EPST_PS_1_1` as the conventional placeholder values (matching Irrlicht Tutorial 10). All GLSL shader files must begin with a `#version` pragma appropriate for the features used (e.g. `#version 130` for `texture()`, `in`/`out` qualifiers, and multi-texture sampling). Do not assume that passing `EVST_VS_3_0` or higher will gate or enable any GLSL feature — it has no effect on the GLSL compilation path.

## Phase 2 GLSL Stub Files — Co-Landing Requirement

The Phase 2 GLSL stub files MUST be co-landed in the same commit as `shader_stub_compile_test.cpp` that asserts they can be found and compiled. This is a hard requirement, not a convention.

**Rationale**: `shader_stub_compile_test.cpp` calls `addHighLevelShaderMaterialFromFiles()` with paths to the Phase 2 lighting shader stubs. If those files are absent on disk, Irrlicht returns immediately with a `−1` material type and the test fails with a file-not-found error. Committing the test without the GLSL files causes an immediate CI failure on every subsequent push until the files are added — this is a broken-tree state and must not enter the branch.

**Exit criterion**: A green `shader_stub_compile_test` in CI is ONLY valid evidence that both the shader files AND the shader loading code are present and functional. A green result obtained by stubbing the test to skip when the files are absent is NOT a valid exit criterion — the skip must never be triggered in CI (see the `GTEST_SKIP()` guard rules in `irrlicht-device-lifecycle.md`).

**Co-landing checklist (single commit must include all of the following):**

- `assets/shaders/lighting.vert` — Phase 2 stub (minimal valid GLSL with `#version 130`, passthrough vertex shader)
- `assets/shaders/lighting.frag` — Phase 2 stub (minimal valid GLSL with `#version 130`, constant color output)
- `assets/shaders/terrain.vert` — Phase 2 stub (minimal valid GLSL with `#version 130`, passthrough vertex shader)
- `assets/shaders/terrain.frag` — Phase 2 stub (minimal valid GLSL with `#version 130`, constant color output)
- `assets/shaders/billboard.vert` — Phase 2 stub (minimal valid GLSL with `#version 130`, passthrough vertex shader)
- `assets/shaders/billboard.frag` — Phase 2 stub (minimal valid GLSL with `#version 130`, constant color output)
- `tests/rendering/shader_stub_compile_test.cpp` — exercises the lighting shaders (NOTE: not shader_loading_test.cpp)
- `src/rendering/shader_loader.cpp` (or the relevant loading code) — the implementation being tested

**Note**: `shader_stub_compile_test.cpp` exercises the `lighting` shaders; all six GLSL files must be present so Phase 3 can build against them.

If any of these artifacts is missing from the commit, the commit is incomplete and must not be merged.

## Phase 8 GLSL Co-Landing Requirement

The Phase 8 GLSL files MUST be co-landed in the same commit as `IrrlichtUIBackend.cpp` in Phase 8.

**Co-landing checklist (single commit must include all of the following):**

- `assets/shaders/ui_quad.vert` — 2D UI textured-quad vertex shader (position + UV attributes, NDC/orthographic transform, outputs interpolated UV `v_uv` to fragment stage); no sampler uniform in vertex stage
- `assets/shaders/ui_quad.frag` — 2D UI textured-quad fragment shader (samples `u_tex` sampler uniform at interpolated `v_uv`, outputs texel colour)

**Note**: These two files MUST be co-landed in the same commit as `IrrlichtUIBackend.cpp` in Phase 8. They are NOT Phase 2 files. See `implementation/phase-8.md` §IrrlichtUIBackend for the full co-landing requirement.

- **Error handling — `addHighLevelShaderMaterialFromFiles()` path**: Check return value of `−1` (shader compile/link failure):
  - **Debug builds** (`NDEBUG` not defined): assert/abort with error message
  - **Release builds**: fall back to `EMT_SOLID` with magenta diffuse (highly visible error indicator); display a non-fatal error notification; log to file; do not abort. Clean shutdown proceeds normally.
- Never apply an unvalidated (−1) material type index to a mesh node

- **Exception — ui_quad raw GL path**: The ui_quad GLSL program (`ui_quad.vert` / `ui_quad.frag`) in `IrrlichtUIBackend` is compiled via raw GL calls (`glCreateShader` / `glShaderSource` / `glCompileShader` / `glLinkProgram`), NOT via `addHighLevelShaderMaterialFromFiles()`. The `−1` return code and `EMT_SOLID` fallback do NOT apply to this path. The correct fallback is:
  - On `GL_COMPILE_STATUS == GL_FALSE` (per shader): log `glGetShaderInfoLog`, debug-assert; set `m_uiQuadProgram = 0` and return.
  - On `GL_LINK_STATUS == GL_FALSE`: log `glGetProgramInfoLog`, debug-assert; set `m_uiQuadProgram = 0` and return.
  - In `setElementImage`, if `m_uiQuadProgram == 0`, return immediately (silent no-op — caller falls back to Irrlicht's software renderer path for this element).
