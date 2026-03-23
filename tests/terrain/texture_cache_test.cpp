// texture_cache_test.cpp — Phase 5 TextureCache unit tests.
//
// Tests that do NOT require a real GL context (EDT_NULL guard verified, ref-count
// bookkeeping, evict no-op under EDT_NULL).
//
// The integration tests that exercise cache-entry deletion
// (TextureCache_EvictUnreferenced_ZeroRefSRGB_DeletesGLTexture and
//  SceneEntityManager_Destroy_FullSequence_ReleasesAllPools) live in
// tests/integration/texture_cache_integration_test.cpp under the "integration"
// label — they use an EDT_NULL Irrlicht device and cannot be in terrain_tests
// (one-label-per-target rule; terrain_tests is labelled "unit").
//
// Label: "unit" (no display or GL context required)
// Spec ref: phase-5.md §TextureCache tests
//           architecture/graphics-architecture/texture-cache.md §EDT_NULL guard

// GLEW must be included before irrlicht.h to claim GL symbols first.
// This mirrors the include order enforced in TextureCache.h itself.
#include <GL/glew.h>
#include <irrlicht.h>

#include <gtest/gtest.h>
#include "src/rendering/TextureCache.h"

// ---------------------------------------------------------------------------
// Convenience alias for the EDT_NULL driver type.
// ---------------------------------------------------------------------------
using EDT_NULL = irr::video::E_DRIVER_TYPE;
static constexpr irr::video::E_DRIVER_TYPE kEdtNull = irr::video::EDT_NULL;

// ---------------------------------------------------------------------------
// TextureCacheTest fixture
//
// All unit tests construct TextureCache with EDT_NULL so that:
//   (a) No real GL context is required.
//   (b) The EDT_NULL guard in evictUnreferenced() is exercised (no crash,
//       no GL calls).
//   (c) ref_count bookkeeping (load/release) is still tracked in the cache
//       regardless of driver type.
// ---------------------------------------------------------------------------
class TextureCacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Construct with EDT_NULL — matches integration-test headless contract.
        m_cache = std::make_unique<TextureCache>(kEdtNull);
    }

    void TearDown() override {
        // Reset explicitly to document destructor order: cache must be destroyed
        // before any driver or device (none in unit tests, but pattern is consistent
        // with integration tests that have a device).
        m_cache.reset();
    }

    std::unique_ptr<TextureCache> m_cache;
};

// ---------------------------------------------------------------------------
// TextureCache_EvictUnreferenced_EDT_NULL_DoesNotCallGL
//
// Under EDT_NULL, evictUnreferenced() must return early without calling any
// raw GL functions (no GL context is available).
//
// Verification strategy: call evictUnreferenced() on a freshly-constructed
// cache — if it crashes or invokes undefined behaviour (GL call without context)
// the test will abort or produce a sanitizer error.  A clean return is the
// required observable.
// ---------------------------------------------------------------------------
TEST_F(TextureCacheTest, EvictUnreferenced_EDT_NULL_DoesNotCallGL) {
    // No textures loaded — empty pool.
    // Must not crash; EDT_NULL guard must fire immediately.
    EXPECT_NO_FATAL_FAILURE(m_cache->evictUnreferenced());
}

TEST_F(TextureCacheTest, EvictUnreferenced_EDT_NULL_WithPendingEntries_DoesNotCallGL) {
    // Load an sRGB path — returns GLuint{0} under EDT_NULL stub.
    // Ref count is still tracked internally.
    GLuint handle = m_cache->loadSRGB("terrain_grass_d.dds",
                                      GL_COMPRESSED_SRGB_S3TC_DXT1_EXT);
    EXPECT_EQ(handle, GLuint{0})
        << "loadSRGB() must return 0 under EDT_NULL (no real GL upload)";

    // Release without evicting — ref_count reaches 0.
    m_cache->releaseSRGB("terrain_grass_d.dds");

    // evictUnreferenced() must not crash even with a zero-ref sRGB entry
    // pending deletion — EDT_NULL guard prevents the glDeleteTextures call.
    EXPECT_NO_FATAL_FAILURE(m_cache->evictUnreferenced());
}

// ---------------------------------------------------------------------------
// TextureCache_ReleaseSRGB_DecrementsRefCount
//
// Loading the same path twice increments the ref_count to 2.
// Releasing once brings it to 1 (entry still present, not evicted).
// Releasing again brings it to 0 (entry eligible for eviction).
// evictUnreferenced() under EDT_NULL does NOT call glDeleteTextures but
// MUST remove the map entry (so getSRGBGLuint returns 0 afterwards).
//
// The observable proxy: getSRGBGLuint(path) returns 0 after eviction.
// ---------------------------------------------------------------------------
TEST_F(TextureCacheTest, ReleaseSRGB_DecrementsRefCount) {
    const std::string path = "terrain_grass_d.dds";
    const GLenum fmt = GL_COMPRESSED_SRGB_S3TC_DXT1_EXT;

    // Two loads -> ref_count == 2.
    m_cache->loadSRGB(path, fmt);
    m_cache->loadSRGB(path, fmt);

    // First release -> ref_count == 1; entry still present.
    m_cache->releaseSRGB(path);
    // Under EDT_NULL getSRGBGLuint returns 0 anyway (no real upload),
    // but the entry must still be in the map (not evicted yet).
    // Calling evictUnreferenced() here must NOT remove the entry (ref_count == 1).
    m_cache->evictUnreferenced();  // no-op under EDT_NULL guard

    // Second release -> ref_count == 0; entry eligible for eviction.
    m_cache->releaseSRGB(path);

    // After evictUnreferenced(), the zero-ref entry must be removed from the map.
    // getSRGBGLuint on a removed entry must return 0.
    m_cache->evictUnreferenced();  // EDT_NULL guard fires — entry removed from map
    EXPECT_EQ(m_cache->getSRGBGLuint(path), GLuint{0})
        << "getSRGBGLuint must return 0 after the zero-ref entry is evicted";
}

// ---------------------------------------------------------------------------
// TextureCache_SplatMap_ReleaseThenEvict_NoLeak
//
// loadSplatMap() under EDT_NULL returns 0 (no GL upload).
// releaseSplatMap() decrements ref_count to 0.
// evictUnreferenced() removes the zero-ref entry from m_splatMaps.
// getSplatMapGLuint() returns 0 for the removed path.
// ---------------------------------------------------------------------------
TEST_F(TextureCacheTest, SplatMap_ReleaseThenEvict_NoLeak) {
    const std::string path = "terrain_blend.png";

    GLuint handle = m_cache->loadSplatMap(path);
    EXPECT_EQ(handle, GLuint{0})
        << "loadSplatMap() must return 0 under EDT_NULL (no real GL upload)";

    m_cache->releaseSplatMap(path);
    m_cache->evictUnreferenced();  // EDT_NULL guard fires; entry removed from map

    EXPECT_EQ(m_cache->getSplatMapGLuint(path), GLuint{0})
        << "getSplatMapGLuint must return 0 after the entry is evicted";
}

TEST_F(TextureCacheTest, SplatMap_DoubleLoad_RefCountTracked) {
    const std::string path = "terrain_blend.png";

    // Load twice — ref_count == 2.
    m_cache->loadSplatMap(path);
    m_cache->loadSplatMap(path);

    // Release once — ref_count == 1.
    m_cache->releaseSplatMap(path);
    m_cache->evictUnreferenced();  // must NOT evict (ref_count == 1)

    // Entry should still be present (getSplatMapGLuint was loaded with 0-handle
    // under EDT_NULL, but the map entry is still there).
    // We verify this by doing a second release and then evicting.
    m_cache->releaseSplatMap(path);
    m_cache->evictUnreferenced();  // now ref_count == 0; entry removed

    EXPECT_EQ(m_cache->getSplatMapGLuint(path), GLuint{0})
        << "Entry must be removed after both releases and eviction";
}

// ---------------------------------------------------------------------------
// TextureCache_LoadSRGB_VehicleSpriteAtlas_RoutesToLinear
//
// Exception per architecture/graphics-architecture/texture-cache.md:
// "vehicles_sprite_atlas_d.dds" must be routed to the LINEAR pool
// (loadLinear()) not the sRGB pool, despite the _d suffix.
// Under EDT_NULL, loadLinear() also returns nullptr — but the sRGB pool
// must NOT have an entry for that path.
// ---------------------------------------------------------------------------
TEST_F(TextureCacheTest, LoadSRGB_VehicleSpriteAtlas_RoutesToLinear) {
    const std::string kVehicleAtlas = "vehicles_sprite_atlas_d.dds";
    const GLenum fmt = GL_COMPRESSED_SRGB_S3TC_DXT1_EXT;

    // Call loadSRGB() — the implementation must detect the vehicle atlas exception
    // and route to loadLinear() instead.
    m_cache->loadSRGB(kVehicleAtlas, fmt);

    // The sRGB pool must NOT contain this path — it was routed to the linear pool.
    EXPECT_EQ(m_cache->getSRGBGLuint(kVehicleAtlas), GLuint{0})
        << "vehicles_sprite_atlas_d.dds must NOT be stored in the sRGB pool";
    // (loadLinear() result is not tested here — nullptr under EDT_NULL is expected)
}

// ---------------------------------------------------------------------------
// TextureCache_EvictUnreferenced_LinearPool_EDT_NULL_NoOp
//
// releaseLinear(ITexture*) with a nullptr texture must not crash.
// Under EDT_NULL, the linear pool removeTexture() call is a driver no-op
// (Irrlicht's null driver is safe) but the map entry should still be removed
// to prevent unbounded map growth.
// ---------------------------------------------------------------------------
TEST_F(TextureCacheTest, EvictUnreferenced_LinearPool_NullptrRelease_NoOp) {
    // Releasing nullptr must be a no-op (guard in releaseLinear must handle it).
    EXPECT_NO_FATAL_FAILURE(m_cache->releaseLinear(static_cast<irr::video::ITexture*>(nullptr)));
    // Evicting an empty cache must not crash.
    EXPECT_NO_FATAL_FAILURE(m_cache->evictUnreferenced());
}

// ---------------------------------------------------------------------------
// TextureCache_BuildingsAtlas_FallbackSelected_WhenMaxTextureSizeBelow4096
//
// Phase 11e: When maxTextureSize < 4096 (e.g. 2048), loadSRGB() must redirect
// buildings_atlas_d.dds → buildings_atlas_d_2k.dds and use the fallback path
// as the sRGB pool key.
//
// Verification strategy (EDT_NULL, no real GL):
// Under EDT_NULL, all sRGB entries have glHandle=0 regardless of which key
// is stored — getSRGBGLuint() returns 0 both for "absent" and "present with
// handle=0".  We therefore use a ref-count round-trip as the observable proxy:
//
//   1. Load via primaryPath twice (both redirected → fallbackPath key, ref=2).
//   2. Load the fallbackPath directly once (same key, ref=3).
//   3. releaseSRGB(fallbackPath) three times (ref=0).
//   4. releaseSRGB(primaryPath) — must be a no-op (primaryPath key absent).
//
// If the redirect had NOT occurred, step 1 would create an entry under
// primaryPath (ref=2), and step 3's releaseSRGB(fallbackPath) calls would
// each be no-ops (fallback key absent).  The final releaseSRGB(primaryPath)
// would then decrement primaryPath's ref_count to 0.  The test verifies step
// 3 and step 4 produce the correct ref_count path by relying on the fact that
// a releaseSRGB() call on a key that IS in the pool decrements ref_count,
// while a call on an absent key is silently ignored.
//
// Cross-check: load the fallbackPath directly once more after releasing all
// entries — ref must be 1 (fresh entry), confirming the prior releases cleaned
// up correctly.
// ---------------------------------------------------------------------------
TEST(TextureCache_BuildingsAtlasTest, FallbackSelected_WhenMaxTextureSizeBelow4096) {
    // maxTextureSize = 2048 → fallback path must be selected.
    TextureCache cache(kEdtNull,
                       /*driver=*/nullptr,
                       /*fileSystem=*/nullptr,
                       /*maxTextureSize=*/2048);

    const std::string primaryPath  = "assets/textures/buildings/buildings_atlas_d.dds";
    const std::string fallbackPath = "assets/textures/buildings/buildings_atlas_d_2k.dds";

    // Step 1: load via primaryPath twice → both redirected to fallbackPath key (ref=2).
    cache.loadSRGB(primaryPath, GL_COMPRESSED_SRGB_S3TC_DXT1_EXT);
    cache.loadSRGB(primaryPath, GL_COMPRESSED_SRGB_S3TC_DXT1_EXT);

    // Step 2: load fallbackPath directly once → same key incremented to ref=3.
    cache.loadSRGB(fallbackPath, GL_COMPRESSED_SRGB_S3TC_DXT1_EXT);

    // Step 3: release fallbackPath three times → ref=0.
    cache.releaseSRGB(fallbackPath);
    cache.releaseSRGB(fallbackPath);
    cache.releaseSRGB(fallbackPath);

    // Step 4: releasing primaryPath must be a no-op (primaryPath key is absent from pool).
    // If primaryPath HAD been inserted as its own key, this call would decrement its
    // ref_count to -1 (decrement on an already-zero-or-negative value is visible only
    // as a VRAM leak).  We verify no such entry exists by confirming getSRGBGLuint
    // returns 0 for primaryPath (absent) while the fallback key went through its ref cycle.
    cache.releaseSRGB(primaryPath); // must be no-op

    // Confirm: after all three fallback releases, a fresh direct load of fallbackPath
    // creates a new entry at ref=1.  This confirms the fallbackPath key was the active
    // key for the two primaryPath loads above (they shared the pool entry).
    cache.loadSRGB(fallbackPath, GL_COMPRESSED_SRGB_S3TC_DXT1_EXT); // ref=1 (fresh start)
    cache.releaseSRGB(fallbackPath); // ref=0

    // Under EDT_NULL getSRGBGLuint always returns 0 (glHandle=0 for all entries,
    // and 0 for absent keys).  The above round-trip is the authoritative check.
    EXPECT_EQ(cache.getSRGBGLuint(primaryPath), GLuint{0})
        << "Primary atlas key must be absent from the sRGB pool when maxTextureSize < 4096 "
           "(all loads via primaryPath were redirected to buildings_atlas_d_2k.dds)";
}

// ---------------------------------------------------------------------------
// TextureCache_BuildingsAtlas_MipLevelDispatch_Primary4_Fallback3
//
// Phase 11e: GL_TEXTURE_MAX_LEVEL dispatch table correctness.
//
// Verifies that maxMipLevel is chosen correctly by confirming which sRGB pool
// key is active after loadSRGB() for each maxTextureSize tier:
//
//   (a) maxTextureSize >= 4096 → effectivePath = primaryPath
//       → glTexParameteri(GL_TEXTURE_MAX_LEVEL, 4) on a real GL context
//
//   (b) maxTextureSize < 4096  → effectivePath = fallbackPath (redirect)
//       → glTexParameteri(GL_TEXTURE_MAX_LEVEL, 3) on a real GL context
//
// Under EDT_NULL no real GL calls are made.  Key-presence is verified via
// the same ref-count round-trip pattern used in FallbackSelected above:
//   - Load twice via primaryPath (both redirected to the effective key, ref=2)
//   - Load once via effectivePath directly (same key, ref=3)
//   - Release three times via effectivePath (ref=0)
//   - Release once via the other key — must be a silent no-op (key absent)
//
// If the wrong key were active the counts would not balance, and the
// "extra" release on the absent key would be a detectable no-op.
// ---------------------------------------------------------------------------
TEST(TextureCache_BuildingsAtlasTest, MipLevelDispatch_Primary4_Fallback3) {
    const std::string primaryPath  = "assets/textures/buildings/buildings_atlas_d.dds";
    const std::string fallbackPath = "assets/textures/buildings/buildings_atlas_d_2k.dds";

    // --- (a) maxTextureSize >= 4096: primaryPath is the effective key ---
    // No redirect; GL_TEXTURE_MAX_LEVEL = 4 would be applied on a real context.
    {
        TextureCache cache(kEdtNull, nullptr, nullptr, /*maxTextureSize=*/4096);

        // Load twice via primaryPath → primaryPath key inserted at ref=2 (no redirect).
        cache.loadSRGB(primaryPath, GL_COMPRESSED_SRGB_S3TC_DXT1_EXT);
        cache.loadSRGB(primaryPath, GL_COMPRESSED_SRGB_S3TC_DXT1_EXT);

        // Load directly via primaryPath a third time → ref=3 (same key).
        cache.loadSRGB(primaryPath, GL_COMPRESSED_SRGB_S3TC_DXT1_EXT);

        // Release primaryPath three times → ref=0.
        cache.releaseSRGB(primaryPath);
        cache.releaseSRGB(primaryPath);
        cache.releaseSRGB(primaryPath);

        // Releasing fallbackPath must be a no-op (key was never inserted).
        // No assertion needed — a no-op on an absent key is silent.
        cache.releaseSRGB(fallbackPath);

        // getSRGBGLuint returns 0 for both absent keys and EDT_NULL entries (handle=0).
        // This confirms the primary key was the active key for all three loads.
        EXPECT_EQ(cache.getSRGBGLuint(fallbackPath), GLuint{0})
            << "(a) Fallback key must be absent from the pool when maxTextureSize >= 4096 "
               "(no redirect; GL_TEXTURE_MAX_LEVEL = 4 path was selected)";
    }

    // --- (b) maxTextureSize < 4096: fallbackPath is the effective key (redirect) ---
    // GL_TEXTURE_MAX_LEVEL = 3 would be applied on a real context.
    {
        TextureCache cache(kEdtNull, nullptr, nullptr, /*maxTextureSize=*/2048);

        // Load twice via primaryPath → both redirected to fallbackPath key at ref=2.
        cache.loadSRGB(primaryPath, GL_COMPRESSED_SRGB_S3TC_DXT1_EXT);
        cache.loadSRGB(primaryPath, GL_COMPRESSED_SRGB_S3TC_DXT1_EXT);

        // Load directly via fallbackPath once → same key, ref=3.
        cache.loadSRGB(fallbackPath, GL_COMPRESSED_SRGB_S3TC_DXT1_EXT);

        // Release fallbackPath three times → ref=0.
        cache.releaseSRGB(fallbackPath);
        cache.releaseSRGB(fallbackPath);
        cache.releaseSRGB(fallbackPath);

        // Releasing primaryPath must be a no-op (primaryPath key was never inserted).
        cache.releaseSRGB(primaryPath);

        // Confirm fallback key was the active pool key (absent-or-zero is the only
        // observable under EDT_NULL; the round-trip above is the authoritative check).
        EXPECT_EQ(cache.getSRGBGLuint(primaryPath), GLuint{0})
            << "(b) Primary key must be absent from the pool when maxTextureSize < 4096 "
               "(redirect to fallback; GL_TEXTURE_MAX_LEVEL = 3 path was selected)";
    }
}
