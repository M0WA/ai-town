// VRAMProfiler.cpp — VRAM query implementation.
// Include order: GLEW before Irrlicht (project convention — GLEW symbol duplication mitigation).

#include <GL/glew.h>
#include <irrlicht.h>

#include "VRAMProfiler.h"

#include <cmath>   // std::ceil

void VRAMProfiler::init()
{
    // NVX path: NVIDIA extension providing total and free VRAM.
    if (glewIsSupported("GL_NVX_gpu_memory_info"))
    {
        GLint totalKB = 0;
        glGetIntegerv(GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX, &totalKB);
        m_totalMB = static_cast<float>(totalKB) / 1024.0f;
        m_method  = Method::NVX;
        return;
    }

    // ATI/AMD path: provides free texture memory only.
    if (glewIsSupported("GL_ATI_meminfo"))
    {
        // totalMB and usedMB remain -1 — only free VRAM is queryable via ATI.
        m_totalMB = -1.0f;
        m_method  = Method::ATI;
        return;
    }

    // Manual fallback: caller must feed texture sizes via addTexture().
    m_method  = Method::MANUAL;
    m_totalMB = -1.0f;
}

float VRAMProfiler::usedMB() const
{
    switch (m_method)
    {
    case Method::NVX:
    {
        GLint totalKB = 0;
        GLint availKB = 0;
        glGetIntegerv(GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX, &totalKB);
        glGetIntegerv(GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX, &availKB);
        if (totalKB <= 0) { return -1.0f; }
        auto usedKB = static_cast<float>(totalKB - availKB);
        return usedKB / 1024.0f;
    }
    case Method::ATI:
        // ATI only exposes free memory — used VRAM is not queryable.
        return -1.0f;

    case Method::MANUAL:
        return m_manualUsedMB;

    case Method::UNAVAILABLE:
    default:
        return -1.0f;
    }
}

float VRAMProfiler::totalMB() const
{
    // NVX: cached at init(). ATI/MANUAL/UNAVAILABLE: -1.0f.
    return m_totalMB;
}

const char* VRAMProfiler::methodName() const
{
    switch (m_method)
    {
    case Method::NVX:         return "NVX (GL_NVX_gpu_memory_info)";
    case Method::ATI:         return "ATI (GL_ATI_meminfo)";
    case Method::MANUAL:      return "MANUAL (accumulated)";
    case Method::UNAVAILABLE: return "UNAVAILABLE";
    default:                  return "UNKNOWN";
    }
}

void VRAMProfiler::addTexture(int width, int height, float bppCompressed, int mipLevels)
{
    if (m_method != Method::MANUAL) { return; }

    // Base level size in bytes.
    float baseBytes = static_cast<float>(width) * static_cast<float>(height) * bppCompressed;

    // Each additional mip level is 1/4 of the previous, so the full chain is
    // base * (1 + 1/4 + 1/16 + ...) ≈ base * 4/3 for a full chain.
    // For partial chains we sum explicitly.
    float totalBytes = 0.0f;
    float levelBytes = baseBytes;
    for (int i = 0; i < mipLevels; ++i)
    {
        totalBytes += levelBytes;
        levelBytes  = levelBytes / 4.0f;
        if (levelBytes < 1.0f) { break; }
    }

    m_manualUsedMB += totalBytes / (1024.0f * 1024.0f);
}

void VRAMProfiler::resetManual()
{
    m_manualUsedMB = 0.0f;
}
