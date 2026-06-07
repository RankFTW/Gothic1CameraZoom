#include "camera_zoom.h"
#include <cmath>
#include <algorithm>

namespace CameraZoom {

static ZoomState s_state;

ZoomState& GetState() {
    return s_state;
}

void UpdateSmoothing(float deltaTime) {
    float target = s_state.zoomMultiplier;
    float current = s_state.currentSmoothed;
    // Exponential smoothing for buttery transitions
    s_state.currentSmoothed = current + (target - current) * (1.0f - std::exp(-s_state.smoothSpeed * deltaTime));
    // Clamp to valid range
    s_state.currentSmoothed = std::clamp(s_state.currentSmoothed, s_state.minMultiplier, s_state.maxMultiplier);
}

void Reset() {
    s_state.zoomMultiplier = 1.0f;
    s_state.currentSmoothed = 1.0f;
}

float GetDisplayZoomLevel() {
    // multiplier 0.5 = 2x zoom, multiplier 0.33 = 3x zoom, etc.
    if (s_state.currentSmoothed <= 0.001f) return 1.0f;
    return 1.0f / s_state.currentSmoothed;
}

} // namespace CameraZoom
