#define RESHADE_ADDON_IMPL
#include <reshade.hpp>
#include "input.h"

#include <Windows.h>
#include <Xinput.h>
#include <chrono>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "xinput.lib")

// ============================================================================
// Camera Zoom - Mouse Wheel + Controller companion for Gothic1RemakeCore
// Requires: Gothic1RemakeCore.dll AND Gothic1RemakeUI.addon64 (from Nexus mod)
// The Nexus UI enables the fix. We send FOV values via mouse wheel or controller.
//
// Controller: Hold LB + Right Stick forward/back to zoom in/out
// When LB is held, right stick input is blocked from the game.
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

// Controller settings
static float s_controllerStep = 0.1f;  // Same step size as mouse wheel per button press

// XInput hook - intercept to block Y/A when LB is held
using XInputGetStateFn = DWORD(WINAPI*)(DWORD dwUserIndex, XINPUT_STATE* pState);
static XInputGetStateFn s_originalXInputGetState = nullptr;
static bool s_lbHeld = false;

static DWORD WINAPI HookedXInputGetState(DWORD dwUserIndex, XINPUT_STATE* pState) {
    DWORD result = s_originalXInputGetState(dwUserIndex, pState);
    if (result != ERROR_SUCCESS || !pState)
        return result;

    s_lbHeld = (pState->Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;

    // If LB is held, hide Y and A from the game
    if (s_lbHeld) {
        pState->Gamepad.wButtons &= ~(XINPUT_GAMEPAD_Y | XINPUT_GAMEPAD_A);
    }

    return result;
}

// Simple IAT hook for XInputGetState
static bool HookXInput() {
    HMODULE gameModule = GetModuleHandleW(nullptr);
    if (!gameModule) return false;

    // Parse PE import table to find XInputGetState
    auto dosHeader = (IMAGE_DOS_HEADER*)gameModule;
    auto ntHeaders = (IMAGE_NT_HEADERS*)((uint8_t*)gameModule + dosHeader->e_lfanew);
    auto importDir = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];

    if (importDir.Size == 0) return false;

    auto importDesc = (IMAGE_IMPORT_DESCRIPTOR*)((uint8_t*)gameModule + importDir.VirtualAddress);

    for (; importDesc->Name != 0; importDesc++) {
        const char* dllName = (const char*)((uint8_t*)gameModule + importDesc->Name);

        // Look for xinput DLLs (xinput1_4.dll, xinput1_3.dll, xinput9_1_0.dll, etc.)
        if (_strnicmp(dllName, "xinput", 6) != 0)
            continue;

        auto thunk = (IMAGE_THUNK_DATA*)((uint8_t*)gameModule + importDesc->FirstThunk);
        auto origThunk = (IMAGE_THUNK_DATA*)((uint8_t*)gameModule + importDesc->OriginalFirstThunk);

        for (; thunk->u1.Function != 0; thunk++, origThunk++) {
            // Check by name
            if (origThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG64)
                continue;

            auto importByName = (IMAGE_IMPORT_BY_NAME*)((uint8_t*)gameModule + origThunk->u1.AddressOfData);
            if (strcmp(importByName->Name, "XInputGetState") == 0) {
                // Found it - patch the IAT entry
                s_originalXInputGetState = (XInputGetStateFn)thunk->u1.Function;

                DWORD oldProtect;
                VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &oldProtect);
                thunk->u1.Function = (ULONGLONG)HookedXInputGetState;
                VirtualProtect(&thunk->u1.Function, sizeof(void*), oldProtect, &oldProtect);

                return true;
            }
        }
    }

    return false;
}

static void UnhookXInput() {
    if (!s_originalXInputGetState) return;

    HMODULE gameModule = GetModuleHandleW(nullptr);
    if (!gameModule) return;

    auto dosHeader = (IMAGE_DOS_HEADER*)gameModule;
    auto ntHeaders = (IMAGE_NT_HEADERS*)((uint8_t*)gameModule + dosHeader->e_lfanew);
    auto importDir = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];

    if (importDir.Size == 0) return;

    auto importDesc = (IMAGE_IMPORT_DESCRIPTOR*)((uint8_t*)gameModule + importDir.VirtualAddress);

    for (; importDesc->Name != 0; importDesc++) {
        const char* dllName = (const char*)((uint8_t*)gameModule + importDesc->Name);
        if (_strnicmp(dllName, "xinput", 6) != 0)
            continue;

        auto thunk = (IMAGE_THUNK_DATA*)((uint8_t*)gameModule + importDesc->FirstThunk);
        auto origThunk = (IMAGE_THUNK_DATA*)((uint8_t*)gameModule + importDesc->OriginalFirstThunk);

        for (; thunk->u1.Function != 0; thunk++, origThunk++) {
            if (origThunk->u1.Ordinal & IMAGE_ORDINAL_FLAG64)
                continue;

            auto importByName = (IMAGE_IMPORT_BY_NAME*)((uint8_t*)gameModule + origThunk->u1.AddressOfData);
            if (strcmp(importByName->Name, "XInputGetState") == 0) {
                DWORD oldProtect;
                VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &oldProtect);
                thunk->u1.Function = (ULONGLONG)s_originalXInputGetState;
                VirtualProtect(&thunk->u1.Function, sizeof(void*), oldProtect, &oldProtect);
                s_originalXInputGetState = nullptr;
                return;
            }
        }
    }
}

static HWND FindGameWindow() {
    return FindWindowW(L"UnrealWindow", nullptr);
}

static float GetControllerZoomInput() {
    // Read directly from original XInput
    XINPUT_STATE state = {};
    if (s_originalXInputGetState) {
        if (s_originalXInputGetState(0, &state) != ERROR_SUCCESS)
            return 0.0f;
    } else {
        if (XInputGetState(0, &state) != ERROR_SUCCESS)
            return 0.0f;
    }

    // Need LB held
    if (!(state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER))
        return 0.0f;

    // LB + Y = zoom in (step), LB + A = zoom out (step)
    // Return -1 for zoom in, +1 for zoom out, 0 for nothing
    if (state.Gamepad.wButtons & XINPUT_GAMEPAD_Y)
        return -1.0f;
    if (state.Gamepad.wButtons & XINPUT_GAMEPAD_A)
        return 1.0f;

    return 0.0f;
}

static void OnReshadePresent(reshade::api::effect_runtime* runtime) {
    if (!s_initialized) {
        s_gameWindow = FindGameWindow();
        if (s_gameWindow) {
            Input::Init(s_gameWindow);
            HookXInput();
            s_initialized = true;
            s_lastFrameTime = std::chrono::steady_clock::now();
        }
    }
    if (!s_initialized) return;

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

    // Mouse wheel
    float wheelDelta = Input::ConsumeWheelDelta();
    if (wheelDelta != 0.0f) {
        s_fovOffset -= wheelDelta * s_fovStep;
        s_fovOffset = std::clamp(s_fovOffset, s_fovMin, s_fovMax);
    }

    // Controller (LB + Y = zoom in, LB + A = zoom out) - discrete steps
    static float s_controllerRepeatTimer = 0.0f;
    static float s_controllerRepeatDelay = 0.2f; // seconds between repeats when held
    float controllerInput = GetControllerZoomInput();
    if (controllerInput != 0.0f) {
        s_controllerRepeatTimer += dt;
        if (s_controllerRepeatTimer >= s_controllerRepeatDelay) {
            s_fovOffset += controllerInput * s_controllerStep;
            s_fovOffset = std::clamp(s_fovOffset, s_fovMin, s_fovMax);
            s_controllerRepeatTimer = 0.0f;
        }
    } else {
        s_controllerRepeatTimer = s_controllerRepeatDelay; // Trigger immediately on first press
    }

    // Reset - keyboard/mouse
    if (Input::WasKeyPressed(VK_MBUTTON) ||
        (Input::IsKeyDown(VK_CONTROL) && Input::WasKeyPressed('R'))) {
        s_fovOffset = 0.0f;
    }

    // Reset - controller: LB + RB + Right Stick Click
    if (s_originalXInputGetState) {
        XINPUT_STATE state = {};
        if (s_originalXInputGetState(0, &state) == ERROR_SUCCESS) {
            if ((state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) &&
                (state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) &&
                (state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB)) {
                s_fovOffset = 0.0f;
            }
        }
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
        UnhookXInput();
        Input::Shutdown();
        reshade::unregister_addon(hModule);
        break;
    }
    return TRUE;
}
