#pragma once

// ============================================================================
// Camera Zoom v2 - Viewport Scaling Approach
//
// Instead of hooking instructions (which crashes with DLSS Frame Gen),
// we intercept the bind_viewports event and shrink the viewport around
// its center. The game renders to a smaller area, which gets stretched
// to fill the screen = magnification with correct perspective.
//
// Only zoom-in is supported (multiplier 0.3 to 1.0). Zoom-out causes
// rendering artifacts because the viewport exceeds the render target.
// ============================================================================

namespace CameraZoom {

struct ZoomState {
    float zoomMultiplier = 1.0f;   // 1.0 = no zoom, lower = more zoom in
    float minMultiplier  = 0.3f;   // Max zoom in (3.3x magnification)
    float maxMultiplier  = 1.0f;   // No zoom out allowed
    float stepSize       = 0.05f;  // Per mouse wheel notch
    float smoothSpeed    = 8.0f;   // Interpolation speed
    float currentSmoothed = 1.0f;  // Smoothed value used for rendering
};

ZoomState& GetState();

// Call once per frame with delta time to smooth the zoom
void UpdateSmoothing(float deltaTime);

// Reset zoom to default (no zoom)
void Reset();

// Returns the current effective zoom level for display (e.g., 2.0x)
float GetDisplayZoomLevel();

} // namespace CameraZoom
