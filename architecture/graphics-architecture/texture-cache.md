# Texture Cache

- `TextureCache` manages **three distinct pools**: **(1) linear-format textures** loaded via `IVideoDriver::getTexture()` (normal maps, roughness — not sRGB, not splat maps); **(2) sRGB diffuse textures** uploaded via the raw OpenGL path (`glGenTextures` / `glCompressedTexImage2D` with sRGB internal format); **(3) splat maps** uploaded via raw OpenGL `glTexImage2D` with `GL_RGBA8` uncompressed format. Methods: `loadLinear(path)`, `loadSRGB(path, format)`, `loadSplatMap(path)`, `releaseLinear(ITexture*)`, `releaseSRGB(filename)`, `releaseSplatMap(filename)`. All three pools respect the LRU eviction budget. **Do not use `IVideoDriver::getTexture()` for sRGB textures or splat maps** — Irrlicht's internal decoder does not produce sRGB or uncompressed RGBA8 GL formats.
- **Linear pool** internal data: `std::unordered_map<std::string, CacheEntry>` where `CacheEntry` = { `ITexture*`, ref_count, last_access_timestamp, estimated_vram_bytes }
- **Splat map pool** internal data: `std::unordered_map<std::string, SplatEntry>` where `SplatEntry` = { `GLuint texId`, ref_count, last_access_timestamp, estimated_vram_bytes }. Splat maps are uploaded via raw `glTexImage2D(GL_RGBA8)` — NEVER via `glCompressedTexImage2D` (DXT compression corrupts the smooth 0–255 blend weight gradients). `loadSplatMap(path)` performs the upload sequence described in `2d-texture-standards.md` (Splat Map GPU Upload). `evictUnreferenced()` calls `glDeleteTextures(1, &entry.texId)` for zero-reference splat map entries. The EDT_NULL guard applies to the splat map pool identically to the sRGB pool. Splat map VRAM estimation: `width × height × 4` bytes (no ×1.33 overhead — single mip level only, `GL_TEXTURE_MAX_LEVEL = 0`).
- **sRGB pool** internal data: `std::unordered_map<std::string, SRGBEntry>` where `SRGBEntry` = { `GLuint texId`, ref_count, last_access_timestamp, estimated_vram_bytes }
- VRAM size estimated per texture format (format-aware, block-based calculation):
  - DXT1/BC1: `ceil(width / 4) × ceil(height / 4) × 8 × 1.33` bytes (8 bytes per 4×4 block, ×1.33 mipmap overhead)
  - DXT5/BC3: `ceil(width / 4) × ceil(height / 4) × 16 × 1.33` bytes (16 bytes per 4×4 block)
  - RGBA8 UNORM (splat maps): `width × height × 4` bytes — **no ×1.33 mip overhead** for splat maps. Splat maps are uploaded with `GL_TEXTURE_MAX_LEVEL = 0` (single mip level only; see 2D Texture Standards — Terrain Texturing & Splat Maps — "Splat map GPU upload"). The full mip chain does not exist; applying ×1.33 would over-budget splat maps and cause premature eviction. At 1024×1024: 1024 × 1024 × 4 = 4.0 MB per map (two maps = 8.0 MB, matching the VRAM budget table).
  The `CacheEntry` / `SRGBEntry` struct records the texture format at insertion time. Using a generic `bytes_per_pixel` multiplier overestimates DXT1 VRAM by 8× and DXT5 by 4×, causing excessive premature eviction.
- **sRGB upload path** — **fully raw OpenGL only**: `addTexture(ECF_A8R8G8B8)` + `glCompressedTexImage2D` is **invalid** — `addTexture` allocates the GL object with a linear internal format already committed; `glCompressedTexImage2D` on that object produces `GL_INVALID_OPERATION`. The `addTexture` + `COpenGLTexture::getOpenGLTextureName()` approach has the same root cause. The only correct path:

  ```cpp
  GLuint texId = 0;
  glGenTextures(1, &texId);
  glBindTexture(GL_TEXTURE_2D, texId);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 3); // clamp mip chain
  for (int mip = 0; mip < numMips; ++mip) {
      glCompressedTexImage2D(GL_TEXTURE_2D, mip,
          GL_COMPRESSED_SRGB_S3TC_DXT1_EXT, // or SRGB_ALPHA_S3TC_DXT5_EXT
          mipWidth, mipHeight, 0, mipDataSize, mipData);
  }
  glBindTexture(GL_TEXTURE_2D, 0);
  ```

  **Wrap mode requirement**: Billboard atlas textures (filename suffix `_billboard`) MUST have
  wrap modes set to `GL_CLAMP_TO_EDGE`:

  ```cpp
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  ```

  The default `GL_REPEAT` causes ghost-frame artifacts at the horizontal strip boundary of the
  1×8 billboard atlas — pixel data from frame 7 bleeds into frame 0 UV sampling. Non-billboard
  sRGB atlas textures use the default `GL_REPEAT` (road markings, building facade). The Phase 1
  stub body is a no-op; this is a Phase 2 implementation requirement that must be documented in
  Phase 1.

  `TextureCache` stores `texId` in `m_srgbTextures` (the sRGB pool). sRGB textures are **not** stored as `ITexture*` and are **not** in node material slots (the `ITexture*` linear pool). Deletion calls `glDeleteTextures(1, &texId)`.

## GL_TEXTURE_MAX_LEVEL Dispatch Table

The `GL_TEXTURE_MAX_LEVEL` parameter controls how many mip levels the GPU may sample. Irrlicht's `IVideoDriver::getTexture()` does not set this parameter — it must be set explicitly via `glTexParameteri` immediately after the texture object is created. The dispatch key is the texture category, identified by filename suffix or by which `TextureCache` method (`loadSRGB`, `loadLinear`, `loadSplatMap`) is used to load the texture.

| Texture category | Filename suffix | TextureCache method | GL internal format | `GL_TEXTURE_MAX_LEVEL` | Rationale |
|---|---|---|---|---|---|
| Diffuse atlas (sRGB DXT1) | `_d` | `loadSRGB()` | `GL_COMPRESSED_SRGB_S3TC_DXT1_EXT` | **3** | 4-level mip chain mandatory (CLAUDE.md); building atlas mip chain clamped at 4 levels. |
| Billboard imposter atlas (sRGB DXT5) | `_billboard` | `loadSRGB()` | `GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT` | **3** | 4-level mip chain mandatory (billboard imposter spec). |
| Lightmap (linear, baked) | `_lm` | `loadLinear()` (or dedicated `loadLightmap()` if added) | `GL_RGB` / `GL_RGBA` linear | _(driver default)_ | Single mip only; lightmaps are pre-filtered at bake time; hardware mip filtering would corrupt the baked radiance values. `GL_TEXTURE_MAX_LEVEL` cannot be set via `glTexParameteri` through the `IVideoDriver` path — see note below. |
| Splat map (terrain blend) | `_splat` | `loadSplatMap()` | `GL_RGBA8` uncompressed | **0** | Single mip only; smooth 0–255 blend weight gradients must not be filtered across mip levels (DXT compression and mip-averaging destroy the gradient precision). Documented in Splat map pool section above. |
| Normal map (linear DXT5nm) | `_n` | `loadLinear()` | `GL_COMPRESSED_RGBA_S3TC_DXT5_EXT` | _(driver default)_ | See note below — `GL_TEXTURE_MAX_LEVEL` cannot be set via `glTexParameteri` through the `IVideoDriver` path. |
| Specular map (linear) | `_s` | `loadLinear()` | `GL_RGBA` linear | _(driver default)_ | See note below — same constraint as normal maps. |
| Vehicle sprite atlas | `_sprite` | `loadLinear()` | `GL_RGBA` linear | _(driver default)_ | No mip chain authored; vehicle sprites are rendered at near-fixed screen size (zoomed-out city view). |

**Note on `loadLinear()` rows**: Normal map (`_n`) and specular (`_s`) textures uploaded via `IVideoDriver::getTexture()` cannot have `GL_TEXTURE_MAX_LEVEL` set via `glTexParameteri` — `IVideoDriver` provides no access to raw GL parameters. These textures have `GL_TEXTURE_MAX_LEVEL` set to the driver default (which for a fully-specified mip chain is the number of mip levels minus 1). Phase 2 may implement a raw-GL upload path for normal maps if explicit mip-level control is required; until then, normal maps and specular maps are excluded from the GL_TEXTURE_MAX_LEVEL dispatch.

**Vehicle normal atlas mip levels**: The vehicle normal atlas (`vehicles_normal_atlas_n.dds`) requires exactly 4 authored mip levels pre-baked into the DDS file header to prevent driver-side mip generation beyond level 3. Because the atlas is uploaded via `IVideoDriver::getTexture()` (linear pool), `glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 3)` cannot be applied through the Irrlicht driver path. See `building-atlas-layout.md` for the V1 workaround.

**Implementation rule**: `GL_TEXTURE_MAX_LEVEL` must be set via `glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, N)` immediately after `glGenTextures` / `glBindTexture`, before any `glCompressedTexImage2D` or `glTexImage2D` upload call. This applies only to textures uploaded through the raw-GL path (`loadSRGB()` and `loadSplatMap()`). The sRGB upload code block above already sets `GL_TEXTURE_MAX_LEVEL = 3` for diffuse and billboard atlases — this table is the authoritative cross-reference for all texture categories. If a new texture category is added, this table MUST be updated and the corresponding `TextureCache` load method MUST set the correct `GL_TEXTURE_MAX_LEVEL` where the raw-GL path is used.

- **Eviction policy**: When budget exceeded, evict the **LRU zero-reference** entry from either pool (linear: `IVideoDriver::removeTexture()`; sRGB: `glDeleteTextures()`); error-log and skip if no zero-reference entries available.
- **Eviction safety**: Before any entity destruction, `SceneEntityManager::destroy()` must iterate over every material slot on the scene node and zero the texture layers explicitly (clearing the node's own material array), AND call `IVideoDriver::setMaterial(SMaterial{})` (to flush the driver's last-bound state). Both steps are required:

  ```cpp
  // In SceneEntityManager::destroy(entity):
  ISceneNode* node = entity->getNode();

  // Step 1: Clear all texture slots AND decrement TextureCache ref counts
  if (node) {
      for (u32 m = 0; m < node->getMaterialCount(); ++m) {
          for (u32 t = 0; t < irr::video::MATERIAL_MAX_TEXTURES; ++t) {
              SMaterial& mat = node->getMaterial(m);  // cache reference — getMaterial() returns SMaterial&
              ITexture* tex = mat.getTexture(t);
              if (tex) {
                  textureCache->releaseLinear(tex); // MUST decrement ref_count before clearing slot (linear-format textures only)
                  mat.setTexture(t, nullptr); // modifies the actual material slot via the cached reference
              }
          }
      }
  }
  // Step 2: Set driver's active material to default — prevents the previously-bound
  // textures from being used in subsequent draw calls if any intervening code paths
  // call driver->drawMeshBuffer() before the scene node is fully removed.
  // NOTE: setMaterial() sets the *next draw call's* material state, NOT a "last-bound"
  // state flush. The node's own material slots are cleared in Step 1 above; this Step 2
  // guards against renderer state that persists between scene node removal and the next drawAll().
  driver->setMaterial(irr::video::SMaterial{});
  // Step 3: Now safe to evict zero-reference textures (LRU budget-check path)
  textureCache->evictUnreferenced();
  // Step 4: Null the game object pointer BEFORE remove() — do not use node* after remove()
  entity->setNode(nullptr);
  node->remove();  // node may be destroyed here; do not access node* after this line
  ```

  **`TextureCache` exposes release paths per pool** — use the correct one based on how the texture was created. **All release methods only decrement ref_count; they do NOT immediately delete the texture object.** The actual deletion of GL resources happens exclusively in `evictUnreferenced()`. This separation prevents double-free: if the same texture is bound to multiple material slots, multiple `releaseLinear()` calls correctly decrement ref_count step-by-step until it reaches 0, at which point `evictUnreferenced()` performs the single deletion.
  - **`TextureCache::releaseLinear(ITexture* tex)`**: used in `SceneEntityManager::destroy()` when iterating material slots (Step 1 above). Applies only to **linear-format `ITexture*` entries** (normal maps, roughness). Looks up the linear-pool `CacheEntry` by `ITexture*`, decrements `ref_count`. **Does NOT call `IVideoDriver::removeTexture()` immediately** — zero-ref entries remain in the map until `evictUnreferenced()` is called. sRGB diffuse textures are **not** tracked as `ITexture*` in the linear pool and are **not** present in node material slots (they are raw `GLuint` values in the sRGB pool); this method will not find them and must not be called for them. Splat maps are raw `GLuint` values in the splat map pool and are also **not** `ITexture*` objects; this method must not be called for splat maps either. (**Note**: there is no method named `releaseByITexture` — that name must not appear anywhere in code or documentation.)
  - **`TextureCache::releaseSRGB(const std::string& filename)`**: releases an sRGB diffuse texture from the raw-GL pool. Decrements `ref_count`. **Does NOT call `glDeleteTextures()` immediately** — zero-ref sRGB entries remain until `evictUnreferenced()` is called.
  - **`TextureCache::releaseSplatMap(const std::string& filename)`**: releases a splat map entry from the splat map pool. Decrements `ref_count`. Does NOT call `glDeleteTextures()` immediately — zero-ref entries remain until `evictUnreferenced()` is called. **Phase 1 stub requirement**: `releaseSplatMap(const std::string& filename)` must be present in the Phase 1 `TextureCache` stub header as a no-op method. `SceneEntityManager::destroy()` calls this in Step 1c — if the method is missing from the Phase 1 header, Phase 2 `SceneEntityManager::destroy()` will fail to compile.
  - **`TextureCache::releaseLinear(const std::string& key)`**: used when explicitly releasing a linear texture by string key. Decrements `ref_count`. Used for linear textures not reachable via material slot iteration.
  `evictUnreferenced()` covers both pools and is the ONLY path that deletes GL resources: for linear zero-ref entries it calls `IVideoDriver::removeTexture()` and removes the entry from the map; for sRGB zero-ref entries it calls `glDeleteTextures(1, &entry.texId)` and then erases the map entry — there is NO `glBindTexture(GL_TEXTURE_2D, 0)` call before deletion. **OpenGL 3.0+ auto-unbind guarantee**: `glDeleteTextures` automatically unbinds the texture from ALL texture units (OpenGL 3.0+ spec, core behaviour). **No manual pre-delete unbind is required or permitted** — `glDeleteTextures` auto-unbinds; calling `glBindTexture(GL_TEXTURE_2D, 0)` immediately before `glDeleteTextures` is redundant overhead and must NOT be present in the eviction path. The correct and complete sRGB eviction sequence is exactly two lines:

  ```cpp
  // glDeleteTextures automatically unbinds from all texture units (OpenGL 3.0+ guarantee).
  // No manual glBindTexture(0) or active-unit save/restore before deletion — auto-unbind handles it.
  glDeleteTextures(1, &entry.texId);  // auto-unbinds from all units; no pre-delete unbind
  m_srgbTextures.erase(it);           // remove map entry; texId is now invalid
  ```

  **Splat map pool eviction — same pass**: `evictUnreferenced()` also evicts zero-reference entries from the splat map pool (`m_splatMaps`) in the same call. Splat maps use `GLuint` (raw `glTexImage2D` upload, `GL_RGBA8`) and are tracked in `m_splatMaps` separately from `m_srgbTextures`. **Canonical member name**: `m_splatMaps` — implementations must use this exact name. Do NOT use `m_splatMapTextures` (that name appears in some earlier plan drafts and is incorrect). The deletion sequence is identical:

  ```cpp
  // Splat map pool eviction — runs in the same evictUnreferenced() call as sRGB eviction:
  for (auto it = m_splatMaps.begin(); it != m_splatMaps.end(); ) {
      if (it->second.ref_count == 0) {
          glDeleteTextures(1, &it->second.texId);  // auto-unbinds; no pre-delete unbind needed
          it = m_splatMaps.erase(it);
      } else {
          ++it;
      }
  }
  ```

  The `EDT_NULL` guard at the top of `evictUnreferenced()` covers both the sRGB pool AND the splat map pool — a single early-return before any pool iteration is sufficient.

  **`_splat` is NOT a DDS suffix — suffix dispatch clarification**: In the `GL_TEXTURE_MAX_LEVEL` dispatch table, `_splat` appears as a logical tag identifying the texture category. It is NOT a file suffix used by the suffix dispatch table to select the sRGB raw-GL upload path. Splat maps are RGBA PNG files (`terrain_blend.png`, not `terrain_blend_splat.dds`) and are loaded via `loadSplatMap(path)` — which calls `glTexImage2D(GL_RGBA8)` — NOT via `loadSRGB()` or `glCompressedTexImage2D`. Linear color space is correct for splat maps (they encode blend weights, not perceptual color). The `_splat` tag is an internal documentation convention; there is no runtime code that dispatches on a `_splat` filename suffix. Any code that routes a `_splat`-suffixed path through `loadSRGB()` or the `glCompressedTexImage2D` path is a bug.

  **CRITICAL constraint — `evictUnreferenced()` must NOT be called from within `OnSetConstants()`**: If eviction fires while a draw call is in progress (inside the shader callback), `glDeleteTextures` would delete a texture that may still be referenced by the current draw command's parameter snapshot. `evictUnreferenced()` is called from `SceneEntityManager::destroy()` during the game logic update phase — strictly between `beginScene()`/`endScene()` boundaries but NOT within any `drawAll()` call. Callers must not invoke `evictUnreferenced()` from inside shader callback methods. Without calling `releaseLinear()` in Step 1, the ref_count is never decremented and `evictUnreferenced()` in Step 3 is always a no-op, causing texture memory to leak.
  `driver->setMaterial()` alone is insufficient — it sets the _next draw call's_ state, not the scene node's own material slots. Both must be cleared.

  **Note**: `getMaterial()` must be called **once per loop iteration** and the result cached as `SMaterial&`. Calling `getMaterial()` a second time risks modifying a temporary (if any intervening code path returns by value), silently leaving the texture slot populated.
- **sRGB texture binding to shader texture units**: sRGB diffuse textures are raw `GLuint` values (not `ITexture*`) and cannot be set via Irrlicht's `SMaterial::setTexture()`. To bind an sRGB texture for a draw call, the renderer must:

  ```cpp
  // Before each draw call that uses an sRGB diffuse texture:
  glActiveTexture(GL_TEXTURE0 + diffuseUnit);  // select the texture unit (unit 0 for diffuse)
  glBindTexture(GL_TEXTURE_2D, texId);         // bind the raw GLuint from TextureCache sRGB pool
  // After the draw call, unbind to avoid state leakage:
  glActiveTexture(GL_TEXTURE0 + diffuseUnit);
  glBindTexture(GL_TEXTURE_2D, 0);
  ```

  The GLSL shader's diffuse sampler uniform (e.g., `uniform sampler2D u_diffuse`) must be assigned the corresponding texture unit index (e.g., `glUniform1i(diffuseLocation, diffuseUnit)`). This binding happens in the renderer's per-draw-call setup, NOT in `TextureCache`. `TextureCache` only manages storage and lifetime; the renderer owns the texture unit assignment. The texture unit for diffuse (unit 0) and normal map (unit 1) must be documented in the shader constant header (`src/rendering/shader_constants.h`) and used consistently across all terrain and building shaders.
- **sRGB texture entity lifetime** — entities must explicitly track and release sRGB textures on destroy: Because sRGB diffuse textures are raw `GLuint` values (not `ITexture*`) and are NOT present in scene node material slots, the linear-pool material-slot iteration in `SceneEntityManager::destroy()` Step 1 **never finds or releases sRGB textures**. Without explicit tracking, sRGB ref_counts are never decremented and `evictUnreferenced()` is permanently a no-op for the sRGB pool — producing unbounded VRAM growth as entities are destroyed and recreated over the game session. **Required design**: Each entity class that loads sRGB diffuse textures (buildings, terrain decorations) must maintain a `std::vector<std::string> m_srgbTextureFilenames` member. At load time, after each `textureCache->loadSRGB(filename, format)` call, push `filename` into `m_srgbTextureFilenames`. In `SceneEntityManager::destroy()`, **after** Step 1 (linear slot iteration), add a Step 1b:

  ```cpp
  // Step 1b: Release sRGB diffuse textures by filename (not reachable via material slot iteration)
  for (const auto& filename : entity->getSRGBTextureFilenames()) {
      textureCache->releaseSRGB(filename);
  }
  ```

  This ensures the sRGB pool ref_counts are decremented, allowing `evictUnreferenced()` (Step 3) to reclaim them. Failure to include this step is a VRAM leak that compounds with each city growth/demolish cycle.

  ```cpp
  // Step 1c: Release splat map pool entries (terrain chunk entities only)
  for (const auto& filename : entity->getSplatMapFilenames()) {
      textureCache->releaseSplatMap(filename);
  }
  ```

  **Step 1c: Release splat map pool entries.** For terrain chunk entities that hold a splat map, call `textureCache->releaseSplatMap(filename)` for each splat map filename stored in the entity's `std::vector<std::string> m_splatMapFilenames`. This applies ONLY to terrain chunk entities — building and vehicle entities do not have splat maps and `m_splatMapFilenames` will be empty for them. The `releaseSplatMap` call only decrements ref_count; actual GL deletion occurs in `evictUnreferenced()` (Step 3). If `m_splatMapFilenames` is empty (non-terrain entity), this step is a no-op. Splat maps are raw `GLuint` values in the splat map pool — they are NOT `ITexture*` objects and are NOT found by the material slot iteration in Step 1. Without Step 1c, the splat map pool ref_counts are never decremented for destroyed terrain chunks, causing the splat map pool to grow unboundedly across chunk load/unload cycles.
- **LOD swap release policy**: When `node->setMesh(newLODMesh)` is called for a LOD level transition (e.g., LOD0→LOD1), the entity's sRGB texture management follows this protocol:
  1. If the new LOD level requires an sRGB diffuse texture that is **not yet in `m_srgbTextureFilenames`** (i.e., a different texture than any previously loaded LOD): call `textureCache->loadSRGB(newFilename, format)` to increment ref_count, then push `newFilename` to `m_srgbTextureFilenames`.
  2. If the new LOD level uses an sRGB diffuse texture **already in `m_srgbTextureFilenames`** (same filename as a previously loaded LOD): do NOT call `loadSRGB()` again — the ref_count was already incremented at first load. Do NOT push the duplicate filename to `m_srgbTextureFilenames`.
  3. Do **NOT** call `releaseSRGB()` for the previous LOD's texture at swap time — the previous LOD texture remains referenced until entity destruction.
  4. `releaseSRGB()` is called ONLY at entity destruction (Step 1b in `SceneEntityManager::destroy()`), once per entry in `m_srgbTextureFilenames`. If the same filename appears twice (loaded once for LOD0, once for LOD1 because both call `loadSRGB` and both push to the list), two `releaseSRGB()` calls are correct — they match two `loadSRGB()` calls.
  This policy ensures the VRAM budget never counts a texture that has been evicted (ref_count 0) while still visible at any LOD level.

- **sRGB texture tracking across LOD levels (LOD swap safety)**: `m_srgbTextureFilenames` must track ALL sRGB textures loaded for an entity across ALL LOD levels simultaneously — not just the current active LOD. When `node->setMesh(newLODMesh)` is called for an LOD swap (e.g., LOD0→LOD1), if the LOD1 mesh requires a different sRGB diffuse texture (e.g., lower-resolution `_lod1_diffuse.dds` instead of `_lod0_diffuse.dds`), the new texture is loaded via `textureCache->loadSRGB()` and its filename must be pushed to `m_srgbTextureFilenames` at load time. The LOD0 texture remains in `m_srgbTextureFilenames` until `SceneEntityManager::destroy()` releases all tracked filenames. **Do not pop LOD0 filenames on LOD swap** — `releaseSRGB()` is called only at entity destruction (Step 1b), not at LOD swap time. If LOD0 and LOD1 share the same sRGB texture filename, `loadSRGB()` increments the ref_count (not a double-load); `releaseSRGB()` at destroy decrements once per entry in `m_srgbTextureFilenames`. If the same filename appears twice in `m_srgbTextureFilenames` (once for LOD0, once for LOD1 that loaded the same texture), two `releaseSRGB()` calls are made — this is correct, as two `loadSRGB()` calls incremented the ref_count twice.
- **EDT_NULL guard for `evictUnreferenced()`**: `evictUnreferenced()` calls `glDeleteTextures()` for zero-reference sRGB pool entries. Under `EDT_NULL` (used in integration tests), there is no OpenGL context and `glDeleteTextures()` invokes undefined behavior. `evictUnreferenced()` must check the driver type before calling any raw GL deletion:

  ```cpp
  void TextureCache::evictUnreferenced() {
      if (m_driverType == video::EDT_NULL) return;  // no GL context; skip raw GL deletion
      // ... existing eviction logic for linear and sRGB pools ...
  }
  ```

  `m_driverType` is set at `TextureCache` construction from `IVideoDriver::getDriverType()`. The linear-pool path (`IVideoDriver::removeTexture()`) is also a no-op under `EDT_NULL` (Irrlicht's null driver is safe here), but the raw-GL sRGB deletion path MUST be guarded explicitly.
- All texture load/evict calls occur on the main render thread only
