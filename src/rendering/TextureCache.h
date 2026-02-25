#pragma once

// TextureCache — manages three distinct texture pools:
//   (1) m_linearTextures: linear-format ITexture* loaded via IVideoDriver::getTexture()
//       (normal maps, roughness, lightmaps — not sRGB, not splat maps)
//   (2) m_srgbTextures: sRGB diffuse textures uploaded via raw OpenGL path
//       (glGenTextures / glCompressedTexImage2D with sRGB internal format)
//   (3) m_splatMaps: terrain splat/blend maps uploaded via raw OpenGL glTexImage2D (GL_RGBA8)
//
// Phase 5: full three-pool implementation with real GL calls per
//          architecture/graphics-architecture/texture-cache.md.
//
// m_driverType is stored at construction from IVideoDriver::getDriverType().
// EDT_NULL guard in evictUnreferenced() and all raw-GL upload paths checks m_driverType.
//
// See architecture/graphics-architecture/texture-cache.md for full spec.
// See architecture/asset-standards/2d-texture-standards.md for texture unit assignments.

#include <string>
#include <unordered_map>
#include <cstdint>

// GL types — needed for GLuint and GLenum in method signatures.
// Must come before any Irrlicht include to avoid symbol conflicts with Irrlicht's bundled GL headers.
#include <GL/glew.h>

#include <irrlicht.h>
// IFileSystem is in irrlicht.h but declare explicitly for clarity.
// irr::io::IFileSystem is used in the constructor and loadSRGB.
// Full include of IFileSystem.h is available via irrlicht.h (which includes it transitively).

class TextureCache {
public:
    // CacheEntry — entry in the linear-format ITexture* pool.
    struct CacheEntry {
        irr::video::ITexture* tex{nullptr};
        int       ref_count{0};
        uint64_t  lastAccessTimestamp{0};
        size_t    vramBytes{0};
    };

    // SRGBEntry — raw GLuint handle for sRGB diffuse textures (DXT1/DXT5 compressed).
    // Stored separately from the ITexture* linear pool — sRGB textures are never ITexture*.
    struct SRGBEntry {
        GLuint   glHandle{0};
        int      ref_count{0};
        uint64_t lastAccessTimestamp{0};
        size_t   vramBytes{0};
    };

    // SplatEntry — raw GLuint handle for terrain splat/blend maps (GL_RGBA8 uncompressed).
    // Uploaded via glTexImage2D (NOT glCompressedTexImage2D — DXT compression corrupts blend weights).
    // Canonical member name: m_splatMaps (do NOT use m_splatMapTextures).
    struct SplatEntry {
        GLuint   glHandle{0};
        int      ref_count{0};
        uint64_t lastAccessTimestamp{0};
        size_t   vramBytes{0};
    };

    // Constructor: driverType is stored for the EDT_NULL guard in evictUnreferenced() and
    // all raw-GL upload paths. Pass IVideoDriver::getDriverType() at construction time.
    // driver is used by loadLinear (IVideoDriver::getTexture) and loadSplatMap (createImageFromFile).
    // fileSystem is used by loadSRGB to open raw DDS files (IrrlichtDevice::getFileSystem()).
    //   Pass nullptr when no file system is available (e.g., EDT_NULL test context).
    explicit TextureCache(irr::video::E_DRIVER_TYPE driverType,
                          irr::video::IVideoDriver* driver = nullptr,
                          irr::io::IFileSystem* fileSystem = nullptr);
    ~TextureCache() = default;

    // Non-copyable / non-movable — pool state and GL handles are not copyable.
    TextureCache(const TextureCache&)            = delete;
    TextureCache& operator=(const TextureCache&) = delete;
    TextureCache(TextureCache&&)                 = delete;
    TextureCache& operator=(TextureCache&&)      = delete;

    // -------------------------------------------------------------------------
    // LOAD METHODS
    // -------------------------------------------------------------------------

    // loadSRGB() — load a diffuse texture via the sRGB raw-GL upload path.
    // format: GL_COMPRESSED_SRGB_S3TC_DXT1_EXT (opaque) or
    //         GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT (transparent diffuse).
    //
    // Dispatch logic:
    //   if filename == "vehicles_sprite_atlas_d.dds" → LINEAR pool via loadLinear()
    //     (Exception: synthetic palette-swatch roof colors, not photographic diffuse)
    //   if path ends with "_billboard" → GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT upload;
    //     wrap mode GL_CLAMP_TO_EDGE; GL_TEXTURE_MAX_LEVEL = 3
    //   if path ends with "_d" → GL_COMPRESSED_SRGB_S3TC_DXT1_EXT upload;
    //     wrap mode GL_REPEAT (default); GL_TEXTURE_MAX_LEVEL = 3
    //
    // sRGB upload is fully raw GL: glGenTextures + glCompressedTexImage2D.
    // DO NOT use addTexture(ECF_A8R8G8B8) + glCompressedTexImage2D — invalid operation.
    //
    // GL_ACTIVE_TEXTURE save/restore required in OnSetConstants() when binding this handle.
    // See architecture/graphics-architecture/shader-loading.md CRITICAL save/restore section.
    GLuint loadSRGB(const std::string& path, GLenum format);

    // loadLinear() — load a linear-format texture via IVideoDriver::getTexture().
    // Used for: normal maps (_n), specular (_s), specular-packed (_sp), lightmaps (_lm).
    // Returns nullptr if load fails or driver is null.
    //
    // NOTE: _lm lightmap textures are linear irradiance — do NOT route through loadSRGB().
    // GL_TEXTURE_MAX_LEVEL cannot be set via the IVideoDriver path; driver default is used.
    irr::video::ITexture* loadLinear(const std::string& path);

    // loadSplatMap() — load a terrain splat/blend map via raw GL (GL_RGBA8 uncompressed).
    // Splat maps must NOT be loaded via loadSRGB() or glCompressedTexImage2D —
    // DXT compression corrupts the smooth 0–255 blend weight gradients.
    // GL_TEXTURE_MAX_LEVEL = 0 (single mip only).
    // EDT_NULL guard: returns GLuint{0} if m_driverType == EDT_NULL.
    //
    // Uses IVideoDriver::createImageFromFile() (NOT getTexture()) to obtain CPU-side pixels.
    // Irrlicht's PNG loader returns straight alpha — no premultiplied-alpha correction needed.
    GLuint loadSplatMap(const std::string& path);

    // -------------------------------------------------------------------------
    // RELEASE METHODS — Phase 5 implements ref_count decrement
    // -------------------------------------------------------------------------

    // releaseLinear(ITexture*) — release a linear-format texture by pointer.
    // Used in SceneEntityManager::destroy() when iterating material slots.
    // Decrements ref_count only. Does NOT call IVideoDriver::removeTexture() immediately.
    // Actual deletion deferred to evictUnreferenced().
    void releaseLinear(irr::video::ITexture* tex);

    // releaseLinear(key) — release a linear texture by string key.
    // Used for linear textures not reachable via material slot iteration.
    void releaseLinear(const std::string& key);

    // releaseSRGB() — release an sRGB diffuse texture by filename.
    // Decrements ref_count only; actual glDeleteTextures() in evictUnreferenced().
    void releaseSRGB(const std::string& filename);

    // releaseSplatMap() — release a splat map entry by filename.
    // Decrements ref_count only; actual glDeleteTextures() in evictUnreferenced().
    void releaseSplatMap(const std::string& filename);

    // -------------------------------------------------------------------------
    // EVICTION
    // -------------------------------------------------------------------------

    // evictUnreferenced() — evict zero-reference entries from all three pools.
    // EDT_NULL guard: if m_driverType == EDT_NULL, returns immediately (no GL context).
    //   This single early-return protects the sRGB pool, splat map pool, and linear pool.
    // sRGB pool: glDeleteTextures() for zero-ref entries (auto-unbinds per GL 3.0+ spec).
    //   No pre-delete glBindTexture(0) — OpenGL 3.0+ auto-unbind guarantee.
    // Splat map pool: glDeleteTextures() for zero-ref entries (same as sRGB pool).
    // Linear pool: IVideoDriver::removeTexture() for zero-ref entries.
    //
    // CRITICAL: must NOT be called from within OnSetConstants() — eviction during a draw
    // call is undefined behaviour. Call only from the game logic update phase.
    void evictUnreferenced();

    // -------------------------------------------------------------------------
    // ACCESSORS
    // -------------------------------------------------------------------------

    // getSRGBGLuint() — retrieve the raw GLuint handle for an sRGB texture by filename.
    // Returns GLuint{0} if not found.
    // NOTE: canonical name is getSRGBGLuint — never getGLuint (unqualified).
    GLuint getSRGBGLuint(const std::string& filename) const;

    // getSplatMapGLuint() — retrieve the raw GLuint handle for a splat map by filename.
    // Returns GLuint{0} if not found.
    // Parallel to getSRGBGLuint — both return GLuint from their raw-GL pools.
    GLuint getSplatMapGLuint(const std::string& filename) const;

    // getEstimatedVRAMBytes() — sum VRAM across all three pools.
    // DXT1/BC1:  ceil(w/4) * ceil(h/4) * 8 * 1.33
    // DXT5/BC3:  ceil(w/4) * ceil(h/4) * 16 * 1.33
    // RGBA8 splat: w * h * 4 (NO x1.33 — single mip, GL_TEXTURE_MAX_LEVEL = 0)
    size_t getEstimatedVRAMBytes() const;

private:
    // Driver type stored at construction — used by EDT_NULL guard in evictUnreferenced()
    // and all raw-GL upload paths. Checked as: m_driverType == irr::video::EDT_NULL.
    irr::video::E_DRIVER_TYPE m_driverType;

    // IVideoDriver pointer — used by loadLinear (getTexture) and loadSplatMap (createImageFromFile).
    // May be nullptr (for legacy EDT_NULL tests that pass only driverType).
    irr::video::IVideoDriver* m_driver{nullptr};

    // IFileSystem pointer — used by loadSRGB to open raw DDS files.
    // Obtained from IrrlichtDevice::getFileSystem(). May be nullptr in EDT_NULL test context.
    irr::io::IFileSystem* m_fileSystem{nullptr};

    // Internal access timestamp counter — incremented on each load call.
    uint64_t m_accessCounter{0};

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
    // Key: path string. Value: CacheEntry with ref_count and VRAM estimate.
    std::unordered_map<std::string, CacheEntry> m_linearTextures;

    // Reverse lookup: ITexture* → path string for releaseLinear(ITexture*).
    std::unordered_map<irr::video::ITexture*, std::string> m_linearTexturesByPtr;
};
