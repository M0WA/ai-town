#pragma once
#include "vec3.h"

struct CameraState {
    vec3  position;
    vec3  forward;
    vec3  up;
    float pitch{0.0f};  // degrees — internal camera spherical coordinate; used by tests
    float yaw{0.0f};    // degrees — internal camera spherical coordinate; used by tests
    float targetX{0.f};
    float targetZ{0.f};
    float zoomDistance{200.f};
};
