#define RESHADE_ADDON_IMPL
#include <reshade.hpp>
#include "input.h"

#include <Windows.h>
#include <chrono>
#include <algorithm>
#include <cmath>

// ============================================================================
// Camera Zoom - Mouse Wheel companion for Gothic1RemakeCore + UI addon
// Requires: Gothic1RemakeCore.dll AND Gothic1RemakeUI.addon64 (from Nexus mod)
// The Nexus UI enables the fix. We just send FOV values via mouse wheel.
// ============================================================================

using SetValuesFn = void(__cdecl*)(int index, float value);
static SetValuesFn s_setValues = nullptr;

static bool s_initialized = false;
static HWND s_gameWindow = nullptr;
static std::chrono::steady_clock::time_point s_lastFrameTime;

static float s_fovOffset = 0.0f;
static float s_fovStep = 0.1f;
static float s_fovMin = 0.0f;
static float s_fovMax = 2.5f;
static float s_smoothSpeed = 10.0f;
static float s_currentSmoothed = 0.0f;

static HWND FindGameWindow() {
    return FindWindowW(L"UnrealWindow", nullptr);
}

static void OnReshadePresent(reshade::api::effect_runtime* runtime) {
    if (!s_initialized) {
        s_gameWindow = FindGameWindow();
        if (s_gameWindow) {
            Input::Init(s_gameWindow);
            s_initialized = true;
            s_lastFrameTime = std::chrono::steady_clock::now();
        }
    }
    if (!s_initialized) return;

    // Try to find the already-loaded Gothic1RemakeCore.dll (loaded by the Nexus UI addon)
    if (!s_setValues) {
        HMODULE core = GetModuleHandleW(L"Gothic1RemakeCore.dll");
        if (core) {
            s_setValues = (SetValuesFn)GetProcAddress(core, "SetValues");
        }
        if (!s_setValues) return;
    }

    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - s_lastFrameTime).count();
    s_lastFrameTime = now;
    if (dt > 0.1f) dt = 0.1f;

    float wheelDelta = Input::ConsumeWheelDelta();
    if (wheelDelta != 0.0f) {
        s_fovOffset -= wheelDelta * s_fovStep;
        s_fovOffset = std::clamp(s_fovOffset, s_fovMin, s_fovMax);
    }

    if (Input::WasKeyPressed(VK_MBUTTON) ||
        (Input::IsKeyDown(VK_CONTROL) && Input::WasKeyPressed('R'))) {
        s_fovOffset = 0.0f;
    }

    s_currentSmoothed += (s_fovOffset - s_currentSmoothed) * (1.0f - std::exp(-s_smoothSpeed * dt));

    s_setValues(1, s_currentSmoothed);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        if (!reshade::register_addon(hModule))
            return FALSE;
        reshade::register_event<reshade::addon_event::reshade_present>(OnReshadePresent);
        break;

    case DLL_PROCESS_DETACH:
        if (s_setValues) s_setValues(1, 0.0f);
        Input::Shutdown();
        reshade::unregister_addon(hModule);
        break;
    }
    return TRUE;
}
