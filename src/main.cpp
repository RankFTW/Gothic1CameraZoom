#include <imgui.h>

#define RESHADE_ADDON_IMPL
#include <reshade.hpp>

#include "input.h"
#include "gamepad.h"

#include <Windows.h>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdio>

// ============================================================================
// Camera Zoom - Mouse Wheel + Controller companion for Gothic1RemakeCore
// ============================================================================

using SetValuesFn = void(__cdecl*)(int index, float value);
static SetValuesFn s_setValues = nullptr;

static bool s_initialized = false;
static HWND s_gameWindow = nullptr;
static std::chrono::steady_clock::time_point s_lastFrameTime;
static wchar_t s_configPath[MAX_PATH] = {};
static wchar_t s_pluginPath[MAX_PATH] = {};

// --- Default values ---
#define DEF_STEP 0.3f
#define DEF_MIN -1.0f
#define DEF_MAX 3.0f
#define DEF_SMOOTH 5.0f
#define DEF_REPEAT 0.1f
#define DEF_RESET_KEY 'R'
#define DEF_RESET_MOD VK_CONTROL
#define DEF_CTRL_ZIN 0
#define DEF_CTRL_ZOUT 3
#define DEF_CTRL_MOD 0

// --- Settings ---
static float s_fovOffset = 0.0f;
static float s_fovStep = DEF_STEP;
static float s_fovMin = DEF_MIN;static float s_fovMax = DEF_MAX;
static float s_smoothSpeed = DEF_SMOOTH;
static float s_currentSmoothed = 0.0f;
static float s_controllerRepeatTimer = 0.0f;
static float s_controllerRepeatDelay = DEF_REPEAT;
static int s_ctrlZoomInBtn = DEF_CTRL_ZIN;
static int s_ctrlZoomOutBtn = DEF_CTRL_ZOUT;
static int s_ctrlModifier = DEF_CTRL_MOD;
static int s_resetKey = DEF_RESET_KEY;
static int s_resetModifier = DEF_RESET_MOD;

static bool s_waitingForKey = false;
static bool s_configLoaded = false;

// --- Config file ---
static void GetConfigPath() {
    GetModuleFileNameW(nullptr, s_configPath, MAX_PATH);
    wchar_t* slash = wcsrchr(s_configPath, L'\\');
    if (slash) {
        wcscpy(s_pluginPath, s_configPath);
        wchar_t* slash2 = wcsrchr(s_pluginPath, L'\\');
        wcscpy(slash2 + 1, L"pluginsettings.ini");
        wcscpy(slash + 1, L"CameraZoom.ini");
    }
}

static void SaveConfig() {
    if (!s_configPath[0]) return;
    FILE* f = _wfopen(s_configPath, L"w");
    if (!f) return;
    fprintf(f, "[CameraZoom]\n");
    fprintf(f, "Step=%.3f\n", s_fovStep);
    fprintf(f, "Min=%.3f\n", s_fovMin);
    fprintf(f, "Max=%.3f\n", s_fovMax);
    fprintf(f, "Smooth=%.1f\n", s_smoothSpeed);
    fprintf(f, "Repeat=%.3f\n", s_controllerRepeatDelay);
    fprintf(f, "ResetKey=%d\n", s_resetKey);
    fprintf(f, "ResetMod=%d\n", s_resetModifier);
    fprintf(f, "CtrlZoomIn=%d\n", s_ctrlZoomInBtn);
    fprintf(f, "CtrlZoomOut=%d\n", s_ctrlZoomOutBtn);
    fprintf(f, "CtrlMod=%d\n", s_ctrlModifier);
    fclose(f);
}

static float s_baseDistance = 1.0f; // k4sh's default camera distance

static void LoadConfig() {
    // Read k4sh's camera distance from pluginsettings.ini
    if (s_pluginPath[0]) {
        FILE* f = _wfopen(s_pluginPath, L"r");
        if (f) {
            char line[256];
            while (fgets(line, sizeof(line), f)) {
                float fv;
                if (sscanf(line, "Camera distance=%f", &fv) == 1) s_baseDistance = fv;
            }
            fclose(f);
        }
    }

    // Read our settings from CameraZoom.ini
    if (!s_configPath[0]) return;
    FILE* f = _wfopen(s_configPath, L"r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        float fv; int iv;
        if (sscanf(line, "Step=%f", &fv) == 1) s_fovStep = fv;
        else if (sscanf(line, "Min=%f", &fv) == 1) s_fovMin = fv;
        else if (sscanf(line, "Max=%f", &fv) == 1) s_fovMax = fv;
        else if (sscanf(line, "Smooth=%f", &fv) == 1) s_smoothSpeed = fv;
        else if (sscanf(line, "Repeat=%f", &fv) == 1) s_controllerRepeatDelay = fv;
        else if (sscanf(line, "ResetKey=%d", &iv) == 1) s_resetKey = iv;
        else if (sscanf(line, "ResetMod=%d", &iv) == 1) s_resetModifier = iv;
        else if (sscanf(line, "CtrlZoomIn=%d", &iv) == 1) s_ctrlZoomInBtn = iv;
        else if (sscanf(line, "CtrlZoomOut=%d", &iv) == 1) s_ctrlZoomOutBtn = iv;
        else if (sscanf(line, "CtrlMod=%d", &iv) == 1) s_ctrlModifier = iv;
    }
    fclose(f);
}

// --- Helpers ---
static const char* VKName(int vk) {
    switch (vk) {
    case 0: return "None"; case VK_MBUTTON: return "Middle Mouse";
    case VK_XBUTTON1: return "Mouse 4"; case VK_XBUTTON2: return "Mouse 5";
    case VK_BACK: return "Backspace"; case VK_RETURN: return "Enter";
    case VK_SPACE: return "Space"; case VK_PRIOR: return "Page Up";
    case VK_NEXT: return "Page Down"; case VK_HOME: return "Home";
    case VK_DELETE: return "Delete"; case VK_CONTROL: return "Ctrl";
    case VK_SHIFT: return "Shift"; case VK_MENU: return "Alt";
    case VK_TAB: return "Tab"; case VK_OEM_3: return "Tilde";
    default:
        if (vk >= VK_F1 && vk <= VK_F12) { static char b[4]; snprintf(b,4,"F%d",vk-VK_F1+1); return b; }
        if (vk >= '0' && vk <= '9') { static char b[2]; b[0]=(char)vk; b[1]=0; return b; }
        if (vk >= 'A' && vk <= 'Z') { static char b[2]; b[0]=(char)vk; b[1]=0; return b; }
        static char b2[10]; snprintf(b2,10,"0x%02X",vk); return b2;
    }
}
static const char* BtnName(int b) {
    const char* n[] = {"Y / Triangle", "B / Circle", "X / Square", "A / Cross"};
    return (b >= 0 && b < 4) ? n[b] : "?";
}
static const char* ModName(int m) { return m == 0 ? "LB / L1" : "RB / R1"; }
static bool GetBtn(const Gamepad::State& gp, int b) {
    switch (b) { case 0: return gp.y; case 3: return gp.a; default: return false; }
}
static bool GetMod(const Gamepad::State& gp, int m) { return m == 0 ? gp.lb : gp.rb; }
static HWND FindGameWindow() { return FindWindowW(L"UnrealWindow", nullptr); }

// --- Per-frame logic ---
static void OnReshadePresent(reshade::api::effect_runtime* runtime) {
    if (!s_initialized) {
        s_gameWindow = FindGameWindow();
        if (s_gameWindow) {
            Input::Init(s_gameWindow);
            Gamepad::Init();
            s_initialized = true;
            s_lastFrameTime = std::chrono::steady_clock::now();
        }
    }
    if (!s_initialized) return;

    if (!s_configLoaded) { LoadConfig(); s_configLoaded = true; }

    if (!s_setValues) {
        HMODULE core = GetModuleHandleW(L"Gothic1RemakeCore.dll");
        if (core) s_setValues = (SetValuesFn)GetProcAddress(core, "SetValues");
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

    // Controller
    Gamepad::State gp = Gamepad::Poll();
    if (gp.connected && GetMod(gp, s_ctrlModifier)) {
        int dir = 0;
        if (GetBtn(gp, s_ctrlZoomInBtn)) dir = -1;
        else if (GetBtn(gp, s_ctrlZoomOutBtn)) dir = 1;

        if (dir != 0) {
            s_controllerRepeatTimer += dt;
            if (s_controllerRepeatTimer >= s_controllerRepeatDelay) {
                s_fovOffset += dir * s_fovStep;
                s_fovOffset = std::clamp(s_fovOffset, s_fovMin, s_fovMax);
                s_controllerRepeatTimer = 0.0f;
            }
        } else {
            s_controllerRepeatTimer = s_controllerRepeatDelay;
        }
        bool otherShoulder = (s_ctrlModifier == 0) ? gp.rb : gp.lb;
        if (otherShoulder && gp.r3) s_fovOffset = 0.0f;
    } else {
        s_controllerRepeatTimer = s_controllerRepeatDelay;
    }

    // Keyboard reset
    bool modHeld = (s_resetModifier == 0) || Input::IsKeyDown(s_resetModifier);
    if (modHeld && Input::WasKeyPressed(s_resetKey)) s_fovOffset = 0.0f;

    s_currentSmoothed += (s_fovOffset - s_currentSmoothed) * (1.0f - std::exp(-s_smoothSpeed * dt));
    s_setValues(1, s_baseDistance + s_currentSmoothed);
}

// --- Overlay UI ---
static void OnOverlay(reshade::api::effect_runtime*) {
    if (s_setValues) {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Status: Active");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.2f, 1.0f), "Waiting for Gothic1RemakeCore.dll...");
        return;
    }

    ImGui::Text("Current Zoom: %.2f", s_currentSmoothed);

    ImGui::Spacing();
    ImGui::SeparatorText("Zoom Range");
    bool changed = false;
    changed |= ImGui::SliderFloat("Max Zoom In", &s_fovMin, -1.0f, 0.0f, "%.2f");
    changed |= ImGui::SliderFloat("Max Zoom Out", &s_fovMax, 1.0f, 5.0f, "%.1f");

    ImGui::Spacing();
    ImGui::SeparatorText("Speed");
    changed |= ImGui::SliderFloat("Scroll Step", &s_fovStep, 0.02f, 0.5f, "%.2f");
    changed |= ImGui::SliderFloat("Smoothing", &s_smoothSpeed, 1.0f, 30.0f, "%.0f");
    changed |= ImGui::SliderFloat("Repeat Rate", &s_controllerRepeatDelay, 0.05f, 0.5f, "%.2f s");

    ImGui::Spacing();
    ImGui::SeparatorText("Keybinds - Keyboard/Mouse");

    // Reset key rebind - dropdown for modifier, single key capture
    if (s_waitingForKey) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "Key: [Press a key... Esc=cancel]");
        for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; key++) {
            if (key == ImGuiKey_LeftCtrl || key == ImGuiKey_RightCtrl) continue;
            if (key == ImGuiKey_LeftShift || key == ImGuiKey_RightShift) continue;
            if (key == ImGuiKey_LeftAlt || key == ImGuiKey_RightAlt) continue;
            if (key == ImGuiKey_LeftSuper || key == ImGuiKey_RightSuper) continue;
            if (ImGui::IsKeyPressed((ImGuiKey)key, false)) {
                if (key == ImGuiKey_Escape) { s_waitingForKey = false; break; }
                int vk = 0;
                if (key >= ImGuiKey_A && key <= ImGuiKey_Z) vk = 'A' + (key - ImGuiKey_A);
                else if (key >= ImGuiKey_0 && key <= ImGuiKey_9) vk = '0' + (key - ImGuiKey_0);
                else if (key >= ImGuiKey_F1 && key <= ImGuiKey_F12) vk = VK_F1 + (key - ImGuiKey_F1);
                else if (key == ImGuiKey_Space) vk = VK_SPACE;
                else if (key == ImGuiKey_Enter) vk = VK_RETURN;
                else if (key == ImGuiKey_Backspace) vk = VK_BACK;
                else if (key == ImGuiKey_Delete) vk = VK_DELETE;
                else if (key == ImGuiKey_Home) vk = VK_HOME;
                else if (key == ImGuiKey_End) vk = VK_END;
                else if (key == ImGuiKey_PageUp) vk = VK_PRIOR;
                else if (key == ImGuiKey_PageDown) vk = VK_NEXT;
                else if (key == ImGuiKey_Tab) vk = VK_TAB;
                else if (key == ImGuiKey_GraveAccent) vk = VK_OEM_3;
                if (vk != 0) { s_resetKey = vk; s_waitingForKey = false; changed = true; break; }
            }
        }
        if (s_waitingForKey && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
            s_resetKey = VK_MBUTTON; s_waitingForKey = false; changed = true;
        }
    } else {
        // Modifier dropdown
        const char* modOptions[] = {"None", "Ctrl", "Shift", "Alt"};
        int modIdx = (s_resetModifier == VK_CONTROL) ? 1 : (s_resetModifier == VK_SHIFT) ? 2 : (s_resetModifier == VK_MENU) ? 3 : 0;
        if (ImGui::Combo("Modifier", &modIdx, modOptions, 4)) {
            s_resetModifier = (modIdx == 1) ? VK_CONTROL : (modIdx == 2) ? VK_SHIFT : (modIdx == 3) ? VK_MENU : 0;
            changed = true;
        }

        // Key display + rebind button
        ImGui::Text("Key: %s", VKName(s_resetKey));
        ImGui::SameLine();
        if (ImGui::SmallButton("Rebind##r")) s_waitingForKey = true;

        // Show full combo
        if (s_resetModifier)
            ImGui::Text("Reset: %s + %s", VKName(s_resetModifier), VKName(s_resetKey));
        else
            ImGui::Text("Reset: %s", VKName(s_resetKey));
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Keybinds - Controller");

    // Show what each button does
    ImGui::Text("Zoom In:  %s + %s", ModName(s_ctrlModifier), BtnName(s_ctrlZoomInBtn));
    ImGui::SameLine();
    if (ImGui::SmallButton("Cycle##zi")) { s_ctrlZoomInBtn = (s_ctrlZoomInBtn == 0) ? 3 : 0; changed = true; }

    ImGui::Text("Zoom Out: %s + %s", ModName(s_ctrlModifier), BtnName(s_ctrlZoomOutBtn));
    ImGui::SameLine();
    if (ImGui::SmallButton("Cycle##zo")) { s_ctrlZoomOutBtn = (s_ctrlZoomOutBtn == 0) ? 3 : 0; changed = true; }

    ImGui::Text("Modifier: %s", ModName(s_ctrlModifier));
    ImGui::SameLine();
    if (ImGui::SmallButton("Toggle##m")) { s_ctrlModifier = 1 - s_ctrlModifier; changed = true; }

    // Controller reset display
    const char* otherMod = (s_ctrlModifier == 0) ? "RB / R1" : "LB / L1";
    ImGui::Text("Reset:    %s + %s + R3", ModName(s_ctrlModifier), otherMod);

    // Save if anything changed
    if (changed) SaveConfig();

    // --- Buttons ---
    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Reset Zoom")) {
        s_fovOffset = 0.0f;
        s_currentSmoothed = 0.0f;
        if (s_setValues) s_setValues(1, s_baseDistance);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset All Settings")) {
        s_fovStep = DEF_STEP; s_fovMin = DEF_MIN; s_fovMax = DEF_MAX;
        s_smoothSpeed = DEF_SMOOTH; s_controllerRepeatDelay = DEF_REPEAT;
        s_resetKey = DEF_RESET_KEY; s_resetModifier = DEF_RESET_MOD;
        s_ctrlZoomInBtn = DEF_CTRL_ZIN; s_ctrlZoomOutBtn = DEF_CTRL_ZOUT;
        s_ctrlModifier = DEF_CTRL_MOD;
        SaveConfig();
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    switch (reason) {
    case DLL_PROCESS_ATTACH: {
        if (!reshade::register_addon(hModule))
            return FALSE;
        GetConfigPath();
        reshade::register_event<reshade::addon_event::reshade_present>(OnReshadePresent);
        reshade::register_overlay("Camera Zoom", OnOverlay);
        break;
    }
    case DLL_PROCESS_DETACH:
        if (s_setValues) s_setValues(1, s_baseDistance);
        Gamepad::Shutdown();
        Input::Shutdown();
        reshade::unregister_addon(hModule);
        break;
    }
    return TRUE;
}
