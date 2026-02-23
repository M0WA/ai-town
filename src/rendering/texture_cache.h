#pragma once

// TextureCache — manages three distinct texture pools:
//   (1) m_linearTextures: linear-format ITexture* loaded via IVideoDriver::getTexture()
//       (normal maps, roughness, lightmaps — not sRGB, not splat maps)
//   (2) m_srgbTextures: sRGB diffuse textures uploaded via raw OpenGL path
//       (glGenTextures / glCompressedTexImage2D with sRGB internal format)
//   (3) m_splatMaps: terrain splat/blend maps uploaded via raw OpenGL glTexImage2D (GL_RGBA8)
//
// Phase 2: skeleton stub — no real GL calls. All load methods return sentinel values.
// Phase 5 delivers the full three-pool implementation with real GL calls per
// architecture/graphics-architecture/texture-cache.md.
//
// m_driverType is required in Phase 2 so that the EDT_NULL guard on the sRGB upload
// path in Phase 5 can check m_driverType == irr::video::EDT_NULL without a header change.
//
// See architecture/graphics-architecture/texture-cache.md for full spec.
// See architecture/asset-standards/2d-texture-standards.md for texture unit assignments.

#include <string>
#include <unordered_map>

// GL types — needed for GLuint and GLenum in method signatures.
// Must come before any Irrlicht include to avoid symbol conflicts with Irrlicht's bundled GL headers.
#include <GL/glew.h>

#include <irrlicht.h>

class TextureCache {
public:
    // SRGBEntry — raw GLuint handle for sRGB diffuse textures (DXT1/DXT5 compressed).
    // Stored separately from the ITexture* linear pool — sRGB textures are never ITexture*.
    struct SRGBEntry {
        GLuint glHandle{0};
        // Phase 5 adds: int ref_count{0}; uint64_t lastAccessTimestamp{0}; size_t vramBytes{0};
    };

    // SplatEntry — raw GLuint handle for terrain splat/blend maps (GL_RGBA8 uncompressed).
    // Uploaded via glTexImage2D (NOT glCompressedTexImage2D — DXT compression corrupts blend weights).
    // Canonical member name: m_splatMaps (do NOT use m_splatMapTextures).
    struct SplatEntry {
        GLuint glHandle{0};
        // Phase 5 adds: int ref_count{0}; uint64_t lastAccessTimestamp{0}; size_t vramBytes{0};
    };

    // Constructor: driverType is stored for the EDT_NULL guard in evictUnreferenced() and
    // all raw-GL upload paths. Pass IVideoDriver::getDriverType() at construction time.
    explicit TextureCache(irr::video::E_DRIVER_TYPE driverType);
    ~TextureCache() = default;

    // Non-copyable / non-movable — pool state and GL handles are not copyable.
    TextureCache(const TextureCache&)            = delete;
    TextureCache& operator=(const TextureCache&) = delete;
    TextureCache(TextureCache&&)                 = delete;
    TextureCache& operator=(TextureCache&&)      = delete;

    // -------------------------------------------------------------------------
    // LOAD METHODS — Phase 2 stubs (no real GL calls; return sentinel values)
    // -------------------------------------------------------------------------

    // loadSRGB() — load a diffuse texture via the sRGB raw-GL upload path.
    // format: GL_COMPRESSED_SRGB_S3TC_DXT1_EXT (opaque) or
    //         GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT (transparent diffuse).
    //
    // STUB DISPATCH LOGIC (no real GL calls — logging only):
    // if (path ends with "_d")         → sRGB upload path (Phase 5: glCompressedTexImage2D with GL_COMPRESSED_SRGB_S3TC_DXT1_EXT)
    //   EXCEPTION: "vehicles_sprite_atlas_d.dds" → LINEAR path via loadLinear()
    //   (roof color palette swatches, not photographic diffuse — must NOT be sRGB-decoded)
    //   Check filename BEFORE suffix. See architecture/asset-standards/2d-texture-standards.md.
    // if (path ends with "_billboard") → sRGB upload path (Phase 5: GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT)
    //   NOTE: _billboard suffix applies exclusively to the per-building imposter atlas
    //   (1024×128 DXT5 sRGB strip per asset). No other billboard-type atlas uses this suffix —
    //   if a new sprite or imposter atlas type is added, verify it uses a distinct suffix before
    //   reusing _billboard routing. See architecture/asset-standards/3d-model-standards.md LOD File Naming Convention.
    // if (path ends with "_n")         → linear path — use loadLinear() NOT loadSRGB()
    // if (path ends with "_s")         → linear path — use loadLinear() NOT loadSRGB()
    // if (path ends with "_sp")        → linear path — use loadLinear() NOT loadSRGB()
    //   (_sp = specular packed: roughness/metallic/AO multi-channel — linear data, not sRGB photographic)
    //   See architecture/graphics-architecture/texture-cache.md GL_TEXTURE_MAX_LEVEL dispatch table.
    // if (path ends with "_lm")        → linear path — use loadLinear() NOT loadSRGB()
    // STUB BODY (Phase 2):
    //   fprintf(stderr, "STUB: would upload %s via sRGB path\n", path.c_str());
    //   return GLuint{0};
    //
    // Phase 5 implementation requirement: if path ends with "_billboard", set
    // GL_TEXTURE_WRAP_S = GL_CLAMP_TO_EDGE and GL_TEXTURE_WRAP_T = GL_CLAMP_TO_EDGE
    // after glTexParameteri filter calls. Default GL_REPEAT causes ghost-frame artifacts
    // at the 1x8 horizontal strip boundary. See architecture/graphics-architecture/texture-cache.md.
    //
    // TODO Phase 5: apply GL_TEXTURE_MAX_LEVEL dispatch table per texture-cache.md:
    //   _billboard suffix -> GL_TEXTURE_MAX_LEVEL = 3 (4-level mip chain mandatory)
    //   _d suffix (sRGB)  -> GL_TEXTURE_MAX_LEVEL = 3 (standard 4-level mip chain)
    //   splat maps        -> use loadSplatMap() NOT loadSRGB(); GL_TEXTURE_MAX_LEVEL=0 (single mip)
    //   NOTE: _n, _s, _sp use loadLinear() NOT loadSRGB() — no glTexParameteri access
    //
    // NOTE: _lm (lightmap) textures use loadLinear() NOT loadSRGB() — they are linear-format
    // textures uploaded via IVideoDriver::getTexture(). Do NOT add _lm dispatch to loadSRGB().
    // See texture-cache.md GL_TEXTURE_MAX_LEVEL table for the full dispatch.
    //
    // TODO Phase 5: When binding this GLuint in OnSetConstants(), save the current
    // GL_ACTIVE_TEXTURE unit with glGetIntegerv(GL_ACTIVE_TEXTURE, &savedUnit) BEFORE
    // calling glActiveTexture(). Restore with glActiveTexture(savedUnit) AFTER unbinding.
    // Failure to restore corrupts Irrlicht's internal active-unit tracking.
    // See architecture/graphics-architecture/shader-loading.md — CRITICAL save/restore section.
    GLuint loadSRGB(const std::string& path, GLenum format);

    // loadLinear() — load a linear-format texture via IVideoDriver::getTexture().
    // Used for: normal maps (_n), specular (_s), specular-packed (_sp), lightmaps (_lm).
    //
    // NOTE: all textures with _lm suffix (e.g., buildings_atlas_lm.dds, vehicles_diffuse_atlas_lm.dds)
    // MUST be loaded via loadLinear(). Do NOT route _lm textures through loadSRGB().
    // Lightmap data is pre-baked linear irradiance — sRGB gamma expansion would corrupt the lighting.
    // See architecture/graphics-architecture/texture-cache.md GL_TEXTURE_MAX_LEVEL dispatch table (_lm row).
    //
    // TODO Phase 5: _lm suffix textures uploaded via loadLinear() MUST have GL_TEXTURE_MAX_LEVEL = 0
    // enforced after upload; IVideoDriver::getTexture() does not expose glTexParameteri — use raw GL
    // after load to set GL_TEXTURE_MAX_LEVEL = 0 on the lightmap texture object.
    // Per architecture/asset-standards/2d-texture-standards.md lightmap mip exemption:
    // lightmaps are single-mip; generating a mip chain introduces blur that corrupts lightmap precision.
    irr::video::ITexture* loadLinear(const std::string& path);

    // loadSplatMap() — load a terrain splat/blend map via raw GL (GL_RGBA8 uncompressed).
    // Splat maps must NOT be loaded via loadSRGB() or glCompressedTexImage2D —
    // DXT compression corrupts the smooth 0–255 blend weight gradients.
    // Returns sentinel GLuint{0} in Phase 2 (no real GL calls).
    GLuint loadSplatMap(const std::string& path);

    // -------------------------------------------------------------------------
    // RELEASE METHODS — Phase 2 stubs (no-ops; Phase 5 implements ref_count decrement)
    // -------------------------------------------------------------------------

    // releaseLinear(ITexture*) — release a linear-format texture by pointer.
    // Used in SceneEntityManager::destroy() when iterating material slots.
    // Does NOT call IVideoDriver::removeTexture() immediately — actual deletion in evictUnreferenced().
    void releaseLinear(irr::video::ITexture* tex);

    // releaseLinear(key) — release a linear texture by string key.
    // Used for linear textures not reachable via material slot iteration.
    void releaseLinear(const std::string& key);

    // releaseSRGB() — release an sRGB diffuse texture by filename.
    // Decrements ref_count only; actual glDeleteTextures() in evictUnreferenced().
    // Required in Phase 2 so SceneEntityManager::destroy() (Step 1b) can compile in Phase 5.
    void releaseSRGB(const std::string& filename);

    // releaseSplatMap() — release a splat map entry by filename.
    // Decrements ref_count only; actual glDeleteTextures() in evictUnreferenced().
    // Required in Phase 2 so SceneEntityManager::destroy() (Step 1c) can compile in Phase 5.
    void releaseSplatMap(const std::string& filename);

    // -------------------------------------------------------------------------
    // EVICTION
    // -------------------------------------------------------------------------

    // evictUnreferenced() — evict zero-reference entries from all three pools.
    // sRGB pool: glDeleteTextures() for zero-ref entries.
    // Splat map pool: glDeleteTextures() for zero-ref entries.
    // Linear pool: IVideoDriver::removeTexture() for zero-ref entries.
    // EDT_NULL guard: if m_driverType == EDT_NULL, returns immediately (no GL context).
    // Must NOT be called from within OnSetConstants() — see texture-cache.md CRITICAL constraint.
    void evictUnreferenced();

    // -------------------------------------------------------------------------
    // ACCESSORS
    // -------------------------------------------------------------------------

    // getSRGBGLuint() — retrieve the raw GLuint handle for an sRGB texture by filename.
    // Returns GLuint{0} if not found (Phase 2 stub always returns 0).
    // NOTE: canonical accessor name is getSRGBGLuint — never getGLuint (unqualified).
    GLuint getSRGBGLuint(const std::string& filename) const;

    // getSplatMapGLuint() — retrieve the raw GLuint handle for a splat map by filename.
    // Returns GLuint{0} if not found (Phase 2 stub always returns 0).
    // Parallel to getSRGBGLuint — both return GLuint from their respective raw-GL pools.
    GLuint getSplatMapGLuint(const std::string& filename) const;

private:
    // Driver type stored at construction — used by EDT_NULL guard in evictUnreferenced()
    // and all raw-GL upload paths. Checked as: m_driverType == irr::video::EDT_NULL.
    irr::video::E_DRIVER_TYPE m_driverType;

    // Pool 1: sRGB diffuse textures (raw GLuint — NOT ITexture*)
    // Uploaded via glGenTextures / glCompressedTexImage2D with sRGB internal format.
    // Key: filename string. Separate from m_linearTextures — sRGB textures are never ITexture*.
    std::unordered_map<std::string, SRGBEntry> m_srgbTextures;

    // Pool 2: terrain splat/blend maps (raw GLuint — NOT ITexture*)
    // Uploaded via glTexImage2D(GL_RGBA8). Single mip level (GL_TEXTURE_MAX_LEVEL = 0).
    // Canonical name: m_splatMaps (do NOT use m_splatMapTextures).
    std::unordered_map<std::string, SplatEntry> m_splatMaps;

    // Pool 3: linear-format textures (ITexture* via IVideoDriver::getTexture())
    // Includes: normal maps (_n), specular (_s), specular-packed (_sp), lightmaps (_lm).
    std::unordered_map<std::string, irr::video::ITexture*> m_linearTextures;
};
