# Shader Loading

- Terrain splatting shader loaded via `IVideoDriver::addHighLevelShaderMaterialFromFiles()`
- **Shader callback lifetime** — **Irrlicht DOES take a reference to the callback**: Irrlicht's material renderer internally calls `grab()` on the `IShaderConstantSetCallBack` when passed to `addHighLevelShaderMaterialFromFiles()`, and calls `drop()` on its own destruction. Therefore, use **raw heap allocation + `->drop()` after passing** — Irrlicht's reference count keeps the callback alive for exactly as long as the material renderer exists. **Do NOT use `std::unique_ptr` or `std::vector<std::unique_ptr<...>>`** — `unique_ptr` calls `delete` directly (bypassing `IReferenceCounted`), causing a double-free when Irrlicht also drops the callback. There is no `m_shaderCallbacks` member in `RenderSystem`.

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

  **sRGB texture binding in shader callbacks**: Because Irrlicht manages texture unit bindings internally during `drawAll()`, raw `glActiveTexture()` + `glBindTexture()` calls for sRGB diffuse textures (raw `GLuint`) must be made **inside `IShaderConstantSetCallBack::OnSetConstants()`** — not between `drawAll()` calls. GL calls made within `OnSetConstants()` execute immediately before the associated draw call and are not overridden by Irrlicht's internal state management. The shader callback receives the active `IVideoDriver*`; `TextureCache::getGLuint(filename)` provides the raw `GLuint` for the sRGB texture to bind. See texture-cache.md for the full sRGB upload and binding specification.

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
      glActiveTexture(static_cast<GLenum>(savedUnit));
  }
  ```

  **Important**: Do NOT add an "after the draw call" unbind step — `IShaderConstantSetCallBack` has no post-draw callback. All GL state setup AND cleanup for sRGB textures must occur within `OnSetConstants()`. The pattern above (bind → set uniforms → unbind → restore unit) is the complete and correct sequence.

  Without this save/restore, Irrlicht's internal active-unit tracking is corrupted starting from the first frame that uses a custom sRGB shader, producing intermittent texture-on-wrong-unit artifacts that are difficult to reproduce.

  **Note on shader version enums**: Irrlicht's GLSL backend **ignores** the `EVST_VS_*` / `EPST_PS_*` enum values entirely when compiling OpenGL GLSL shaders. These enums are meaningful only for the Direct3D HLSL backend. For GLSL, the active GLSL version is determined exclusively by the `#version` directive in the shader source file itself. Use `EVST_VS_1_1` / `EPST_PS_1_1` as the conventional placeholder values (matching Irrlicht Tutorial 10). All GLSL shader files must begin with a `#version` pragma appropriate for the features used (e.g. `#version 130` for `texture()`, `in`/`out` qualifiers, and multi-texture sampling). Do not assume that passing `EVST_VS_3_0` or higher will gate or enable any GLSL feature — it has no effect on the GLSL compilation path.
- **Error handling**: Check return value of `−1` (shader compile/link failure):
  - **Debug builds** (`NDEBUG` not defined): assert/abort with error message
  - **Release builds**: fall back to `EMT_SOLID` with magenta diffuse (highly visible error indicator); display a non-fatal error notification; log to file; do not abort. Clean shutdown proceeds normally.
- Never apply an unvalidated (−1) material type index to a mesh node
