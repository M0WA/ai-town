#include "UIScaler.h"
#include <algorithm>  // std::clamp

UIScaler::UIScaler(int virtualW, int virtualH, int viewportW, int viewportH, int offsetX, int offsetY)
    : m_virtualW(virtualW)
    , m_virtualH(virtualH)
    , m_viewportW(viewportW)
    , m_viewportH(viewportH)
    , m_offsetX(offsetX)
    , m_offsetY(offsetY)
{}

UIRect UIScaler::getViewportRect() const {
    return UIRect{m_offsetX, m_offsetY, m_viewportW, m_viewportH};
}

UIScaler::VirtualPoint UIScaler::unproject(int physicalX, int physicalY) const {
    // Guard against divide-by-zero (EDT_NULL path returns {0,0} screen size)
    if (m_viewportW <= 0 || m_viewportH <= 0) {
        return VirtualPoint{0, 0};
    }

    // Translate to viewport-relative coordinates
    const int relX = physicalX - m_offsetX;
    const int relY = physicalY - m_offsetY;

    // Scale to virtual space
    // Use integer arithmetic with rounding to avoid accumulation errors
    const int vx = (relX * m_virtualW) / m_viewportW;
    const int vy = (relY * m_virtualH) / m_viewportH;

    // Clamp to valid virtual range [0, virtualW-1] x [0, virtualH-1]
    // Exclusive upper bound: maximum valid output is (virtualW-1, virtualH-1)
    // i.e. (1919, 1079) in 1920x1080 virtual space
    const int clampedX = std::clamp(vx, 0, m_virtualW - 1);
    const int clampedY = std::clamp(vy, 0, m_virtualH - 1);

    return VirtualPoint{clampedX, clampedY};
}
