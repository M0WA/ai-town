// texture_cache.cpp — Phase 2 stub implementation.
// All load/release/evict methods are no-ops returning sentinel values.
// Phase 5 delivers the full three-pool implementation with real GL calls.
// See architecture/graphics-architecture/texture-cache.md for the full spec.

// GLEW must come before any Irrlicht or OpenGL includes.
#include <GL/glew.h>

#include <irrlicht.h>

#include "texture_cache.h"

#include <cstdio>  // fprintf

TextureCache::TextureCache(irr::video::E_DRIVER_TYPE driverType)
    : m_driverType{driverType}
{
}

GLuint TextureCache::loadSRGB(const std::string& path, GLenum /*format*/) {
    // Phase 2 stub — no real GL calls.
    fprintf(stderr, "STUB: would upload %s via sRGB path\n", path.c_str());
    return GLuint{0};
}

irr::video::ITexture* TextureCache::loadLinear(const std::string& path) {
    // Phase 2 stub — no real IVideoDriver::getTexture() call.
    fprintf(stderr, "STUB: would load linear texture %s\n", path.c_str());
    return nullptr;
}

GLuint TextureCache::loadSplatMap(const std::string& path) {
    // Phase 2 stub — no real GL calls.
    fprintf(stderr, "STUB: would upload splat map %s via GL_RGBA8 path\n", path.c_str());
    return GLuint{0};
}

void TextureCache::releaseLinear(irr::video::ITexture* /*tex*/) {
    // Phase 2 stub — no-op. Phase 5 decrements ref_count and calls evictUnreferenced().
}

void TextureCache::releaseLinear(const std::string& /*key*/) {
    // Phase 2 stub — no-op. Phase 5 decrements ref_count and calls evictUnreferenced().
}

void TextureCache::releaseSRGB(const std::string& /*filename*/) {
    // Phase 2 stub — no-op. Phase 5 decrements ref_count; actual glDeleteTextures() in evictUnreferenced().
}

void TextureCache::releaseSplatMap(const std::string& /*filename*/) {
    // Phase 2 stub — no-op. Phase 5 decrements ref_count; actual glDeleteTextures() in evictUnreferenced().
}

void TextureCache::evictUnreferenced() {
    // EDT_NULL guard: no GL context available — skip all raw GL deletion.
    if (m_driverType == irr::video::EDT_NULL) return;
    // Phase 2 stub — no-op. Phase 5 iterates all three pools and deletes zero-ref entries.
}

GLuint TextureCache::getSRGBGLuint(const std::string& /*filename*/) const {
    // Phase 2 stub — always returns 0. Phase 5 looks up m_srgbTextures.
    return GLuint{0};
}

GLuint TextureCache::getSplatMapGLuint(const std::string& /*filename*/) const {
    // Phase 2 stub — always returns 0. Phase 5 looks up m_splatMaps.
    return GLuint{0};
}
