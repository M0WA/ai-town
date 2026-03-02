#pragma once

// VRAMProfiler — queries available/used VRAM from the GPU driver.
//
// Call init() once after a valid OpenGL context exists (i.e. after RenderSystem creates
// the Irrlicht device and calls glewInit()). All query methods are then safe to call at
// any time from the render thread.
//
// Detection strategy (tried in order):
//   1. GL_NVX_gpu_memory_info (NVIDIA) — provides both total and free VRAM in KB.
//      usedMB() = (total - currentFree) / 1024.0f.
//   2. GL_ATI_meminfo (AMD/ATI) — provides free texture VRAM only.
//      usedMB() returns -1.0f (no used query available).
//      totalMB() returns -1.0f (no total query available).
//   3. Manual fallback — external callers feed texture sizes via addTexture().
//      usedMB() accumulates sizes. totalMB() returns -1.0f.
//   4. UNAVAILABLE — none of the above succeeded. Both accessors return -1.0f.
//
// Thread safety: NOT thread-safe. Call only from the render/main thread.

// GL constants required for VRAM queries — may not be present in all GLEW versions.
#ifndef GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX
#define GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX  0x9048
#endif
#ifndef GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX
#define GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX 0x9049
#endif
#ifndef GL_TEXTURE_FREE_MEMORY_ATI
#define GL_TEXTURE_FREE_MEMORY_ATI 0x87FC
#endif

class VRAMProfiler {
public:
    enum class Method {
        NVX,         // GL_NVX_gpu_memory_info (NVIDIA)
        ATI,         // GL_ATI_meminfo (AMD/ATI) — free only, no total/used
        MANUAL,      // external accumulation via addTexture()
        UNAVAILABLE  // no query method available
    };

    VRAMProfiler() = default;

    // Must be called after a valid OpenGL context exists and glewInit() has been called.
    // Detects the best available VRAM query method and stores total VRAM if queryable.
    void init();

    // Returns used VRAM in MB. Returns -1.0f if unavailable (ATI or UNAVAILABLE method,
    // or if init() has not been called).
    float usedMB() const;

    // Returns total GPU VRAM in MB. Returns -1.0f if unavailable (ATI, UNAVAILABLE, or MANUAL).
    float totalMB() const;

    // Returns the detection method selected by init().
    Method method() const { return m_method; }

    // Human-readable name of the detection method. Safe to call before init().
    const char* methodName() const;

    // Manual fallback: accumulate texture VRAM. Only meaningful when method() == MANUAL.
    // width, height: texture dimensions in pixels.
    // bppCompressed: bytes per pixel (use 0.5f for DXT1, 1.0f for DXT5/BC3, 4.0f for RGBA8).
    // mipLevels: number of mip levels (1 = no mips). Each mip adds ~1/3 overhead total.
    void addTexture(int width, int height, float bppCompressed, int mipLevels);

    // Resets the manual accumulator to zero. No-op for non-MANUAL methods.
    void resetManual();

private:
    Method  m_method{Method::UNAVAILABLE};
    float   m_totalMB{-1.0f};    // cached at init() for NVX; -1 for ATI/MANUAL/UNAVAILABLE
    float   m_manualUsedMB{0.0f}; // accumulator for MANUAL fallback
};
