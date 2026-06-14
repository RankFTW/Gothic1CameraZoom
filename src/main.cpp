#include <imgui.h>

#define RESHADE_ADDON_IMPL
#include <reshade.hpp>

#include "input.h"
#include "gamepad.h"

#include <safetyhook.hpp>

#include <Windows.h>
#include <Psapi.h>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <atomic>

// =============================================================================
// Debug logging to file
// =============================================================================
static FILE* s_logFile = nullptr;
static int s_logCount = 0;

static void LogInit() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) wcscpy(slash + 1, L"CameraZoom_log.txt");
    s_logFile = _wfopen(path, L"w");
    if (s_logFile) { fprintf(s_logFile, "=== CameraZoom v2.0 Log ===\n"); fflush(s_logFile); }
}

static void Log(const char* fmt, ...) {
    if (!s_logFile) return;
    if (s_logCount > 500) return; // cap log size
    va_list args;
    va_start(args, fmt);
    vfprintf(s_logFile, fmt, args);
    va_end(args);
    fprintf(s_logFile, "\n");
    fflush(s_logFile);
    s_logCount++;
}

// ============================================================================
// Camera Zoom Standalone - FOV hook via SafetyHook
// No dependency on Gothic1RemakeCore.dll
//
// Strategy:
// 1. Hook the FOV property setter (movss [rcx+0x26C], xmm1; ret) to capture
//    the camera object pointer (rcx) and the game's base FOV value.
// 2. Every frame in OnReshadePresent, write our modified FOV directly to the
//    camera object's memory. This overrides whatever the game stored, and since
//    we do it every frame the game can never override us back.
// ============================================================================

static bool s_initialized = false;
static HWND s_gameWindow = nullptr;
static std::chrono::steady_clock::time_point s_lastFrameTime;
static wchar_t s_configPath[MAX_PATH] = {};
static bool s_configLoaded = false;

// --- Defaults ---
#define DEF_STEP 70.0f
#define DEF_MIN -320.0f
#define DEF_MAX 600.0f
#define DEF_SMOOTH 11.0f
#define DEF_MOUSEWHEEL true
#define DEF_REPEAT 0.10f
#define DEF_CTRL_ZIN 0
#define DEF_CTRL_ZOUT 3
#define DEF_CTRL_MOD 0

// =============================================================================
// Keybind structure (same as companion addon)
// =============================================================================
struct Keybind {
    std::string display;
    bool hasCtrl = false;
    bool hasShift = false;
    bool hasAlt = false;
    int vkCode = 0;

    void Clear() { display.clear(); hasCtrl = false; hasShift = false; hasAlt = false; vkCode = 0; }
    bool IsBound() const { return vkCode != 0; }

    void BuildDisplay() {
        display.clear();
        if (vkCode == 0) return;
        if (hasCtrl) display += "Ctrl+";
        if (hasShift) display += "Shift+";
        if (hasAlt) display += "Alt+";
        display += VKToName(vkCode);
    }

    void Set(bool ctrl, bool shift, bool alt, int vk) {
        hasCtrl = ctrl; hasShift = shift; hasAlt = alt; vkCode = vk;
        BuildDisplay();
    }

    bool IsHeld() const {
        if (vkCode == 0) return false;
        if (hasCtrl && !Input::IsKeyDown(VK_CONTROL)) return false;
        if (hasShift && !Input::IsKeyDown(VK_SHIFT)) return false;
        if (hasAlt && !Input::IsKeyDown(VK_MENU)) return false;
        return Input::IsKeyDown(vkCode);
    }

    bool WasPressed() const {
        if (vkCode == 0) return false;
        if (hasCtrl && !Input::IsKeyDown(VK_CONTROL)) return false;
        if (hasShift && !Input::IsKeyDown(VK_SHIFT)) return false;
        if (hasAlt && !Input::IsKeyDown(VK_MENU)) return false;
        return Input::WasKeyPressed(vkCode);
    }

    void ParseFromString(const std::string& str) {
        Clear();
        if (str.empty()) return;
        std::string s = str;
        while (true) {
            if (s.rfind("Ctrl+", 0) == 0) { hasCtrl = true; s = s.substr(5); }
            else if (s.rfind("Shift+", 0) == 0) { hasShift = true; s = s.substr(6); }
            else if (s.rfind("Alt+", 0) == 0) { hasAlt = true; s = s.substr(4); }
            else break;
        }
        vkCode = NameToVK(s.c_str());
        if (vkCode != 0) display = str;
        else Clear();
    }

    static const char* VKToName(int vk) {
        switch (vk) {
        case VK_MBUTTON: return "Middle Mouse";
        case VK_XBUTTON1: return "Mouse 4";
        case VK_XBUTTON2: return "Mouse 5";
        case VK_CONTROL: return "Ctrl";
        case VK_SHIFT: return "Shift";
        case VK_MENU: return "Alt";
        case VK_BACK: return "Backspace";
        case VK_RETURN: return "Enter";
        case VK_SPACE: return "Space";
        case VK_PRIOR: return "Page Up";
        case VK_NEXT: return "Page Down";
        case VK_HOME: return "Home";
        case VK_END: return "End";
        case VK_DELETE: return "Delete";
        case VK_INSERT: return "Insert";
        case VK_TAB: return "Tab";
        case VK_OEM_3: return "Tilde";
        case VK_OEM_MINUS: return "Minus";
        case VK_OEM_PLUS: return "Plus";
        case VK_OEM_4: return "LBracket";
        case VK_OEM_6: return "RBracket";
        case VK_OEM_5: return "Backslash";
        case VK_OEM_1: return "Semicolon";
        case VK_OEM_7: return "Quote";
        case VK_OEM_COMMA: return "Comma";
        case VK_OEM_PERIOD: return "Period";
        case VK_OEM_2: return "Slash";
        case VK_UP: return "Up";
        case VK_DOWN: return "Down";
        case VK_LEFT: return "Left";
        case VK_RIGHT: return "Right";
        case VK_NUMPAD0: return "Num0";
        case VK_NUMPAD1: return "Num1";
        case VK_NUMPAD2: return "Num2";
        case VK_NUMPAD3: return "Num3";
        case VK_NUMPAD4: return "Num4";
        case VK_NUMPAD5: return "Num5";
        case VK_NUMPAD6: return "Num6";
        case VK_NUMPAD7: return "Num7";
        case VK_NUMPAD8: return "Num8";
        case VK_NUMPAD9: return "Num9";
        case VK_MULTIPLY: return "Num*";
        case VK_ADD: return "Num+";
        case VK_SUBTRACT: return "Num-";
        case VK_DECIMAL: return "Num.";
        case VK_DIVIDE: return "Num/";
        default:
            if (vk >= VK_F1 && vk <= VK_F24) { static char b[5]; snprintf(b, 5, "F%d", vk - VK_F1 + 1); return b; }
            if (vk >= '0' && vk <= '9') { static char b[2]; b[0] = (char)vk; b[1] = 0; return b; }
            if (vk >= 'A' && vk <= 'Z') { static char b[2]; b[0] = (char)vk; b[1] = 0; return b; }
            static char b2[10]; snprintf(b2, 10, "0x%02X", vk); return b2;
        }
    }

    static int NameToVK(const char* name) {
        if (!name || !name[0]) return 0;
        if (name[1] == 0) {
            char c = name[0];
            if (c >= 'A' && c <= 'Z') return c;
            if (c >= 'a' && c <= 'z') return c - 32;
            if (c >= '0' && c <= '9') return c;
        }
        if (name[0] == 'F' && name[1] >= '1' && name[1] <= '9') {
            int n = atoi(name + 1);
            if (n >= 1 && n <= 24) return VK_F1 + n - 1;
        }
        if (_stricmp(name, "Middle Mouse") == 0) return VK_MBUTTON;
        if (_stricmp(name, "Mouse 4") == 0) return VK_XBUTTON1;
        if (_stricmp(name, "Mouse 5") == 0) return VK_XBUTTON2;
        if (_stricmp(name, "Ctrl") == 0) return VK_CONTROL;
        if (_stricmp(name, "Shift") == 0) return VK_SHIFT;
        if (_stricmp(name, "Alt") == 0) return VK_MENU;
        if (_stricmp(name, "Backspace") == 0) return VK_BACK;
        if (_stricmp(name, "Enter") == 0) return VK_RETURN;
        if (_stricmp(name, "Space") == 0) return VK_SPACE;
        if (_stricmp(name, "Page Up") == 0) return VK_PRIOR;
        if (_stricmp(name, "Page Down") == 0) return VK_NEXT;
        if (_stricmp(name, "Home") == 0) return VK_HOME;
        if (_stricmp(name, "End") == 0) return VK_END;
        if (_stricmp(name, "Delete") == 0) return VK_DELETE;
        if (_stricmp(name, "Insert") == 0) return VK_INSERT;
        if (_stricmp(name, "Tab") == 0) return VK_TAB;
        if (_stricmp(name, "Tilde") == 0) return VK_OEM_3;
        if (_stricmp(name, "Minus") == 0) return VK_OEM_MINUS;
        if (_stricmp(name, "Plus") == 0) return VK_OEM_PLUS;
        if (_stricmp(name, "LBracket") == 0) return VK_OEM_4;
        if (_stricmp(name, "RBracket") == 0) return VK_OEM_6;
        if (_stricmp(name, "Backslash") == 0) return VK_OEM_5;
        if (_stricmp(name, "Semicolon") == 0) return VK_OEM_1;
        if (_stricmp(name, "Quote") == 0) return VK_OEM_7;
        if (_stricmp(name, "Comma") == 0) return VK_OEM_COMMA;
        if (_stricmp(name, "Period") == 0) return VK_OEM_PERIOD;
        if (_stricmp(name, "Slash") == 0) return VK_OEM_2;
        if (_stricmp(name, "Up") == 0) return VK_UP;
        if (_stricmp(name, "Down") == 0) return VK_DOWN;
        if (_stricmp(name, "Left") == 0) return VK_LEFT;
        if (_stricmp(name, "Right") == 0) return VK_RIGHT;
        if (_stricmp(name, "Num0") == 0) return VK_NUMPAD0;
        if (_stricmp(name, "Num1") == 0) return VK_NUMPAD1;
        if (_stricmp(name, "Num2") == 0) return VK_NUMPAD2;
        if (_stricmp(name, "Num3") == 0) return VK_NUMPAD3;
        if (_stricmp(name, "Num4") == 0) return VK_NUMPAD4;
        if (_stricmp(name, "Num5") == 0) return VK_NUMPAD5;
        if (_stricmp(name, "Num6") == 0) return VK_NUMPAD6;
        if (_stricmp(name, "Num7") == 0) return VK_NUMPAD7;
        if (_stricmp(name, "Num8") == 0) return VK_NUMPAD8;
        if (_stricmp(name, "Num9") == 0) return VK_NUMPAD9;
        if (_stricmp(name, "Num*") == 0) return VK_MULTIPLY;
        if (_stricmp(name, "Num+") == 0) return VK_ADD;
        if (_stricmp(name, "Num-") == 0) return VK_SUBTRACT;
        if (_stricmp(name, "Num.") == 0) return VK_DECIMAL;
        if (_stricmp(name, "Num/") == 0) return VK_DIVIDE;
        if (name[0] == '0' && (name[1] == 'x' || name[1] == 'X')) {
            return (int)strtol(name, nullptr, 16);
        }
        return 0;
    }
};

// =============================================================================
// Settings
// =============================================================================
// Camera distance offset (primary zoom control)
static float s_zoomOffset = 0.0f;   // Added to TargetArmLength each frame
static float s_zoomTarget = 0.0f;   // Smoothing target
static float s_zoomStep = DEF_STEP;
static float s_zoomMin = DEF_MIN;   // Max zoom in (negative = closer)
static float s_zoomMax = DEF_MAX;   // Max zoom out (positive = further)
static float s_smoothSpeed = DEF_SMOOTH;
static bool s_mouseWheelEnabled = DEF_MOUSEWHEEL;
static float s_controllerRepeatTimer = 0.0f;
static float s_controllerRepeatDelay = DEF_REPEAT;
static bool s_hookActive = false;

// FOV zoom (secondary, optional)
static float s_fovZoomFactor = 1.0f;
static float s_fovZoomTarget = 1.0f;
static float s_fovOffset = 0.0f;  // Direct FOV offset in degrees

// KB/Mouse bindings
static Keybind s_scrollModifier;
static Keybind s_zoomInKey;
static Keybind s_zoomOutKey;
static Keybind s_resetKey;
static Keybind s_fpToggleKey;  // First-person toggle

// First-person toggle state
static bool s_fpActive = false;
static float s_fpSavedZoom = 0.0f;  // Saved 3rd person zoom offset

// Controller bindings
static int s_ctrlZoomIn = DEF_CTRL_ZIN;
static int s_ctrlZoomOut = DEF_CTRL_ZOUT;
static int s_ctrlMod = DEF_CTRL_MOD;
static int s_ctrlFPToggle = 6;  // Default: L3 / Left Stick (index 6)

// Controller button helpers
static const char* CtrlBtnNames[] = {"Y / Triangle", "B / Circle", "X / Square", "A / Cross", "D-pad Up", "D-pad Down", "L3 / Left Stick"};
static const int CTRL_BTN_COUNT = 7;
static const char* CtrlModNames[] = {"LB / L1", "RB / R1", "None"};
static const int CTRL_MOD_COUNT = 3;

static bool GetCtrlBtn(const Gamepad::State& gp, int btn) {
    switch (btn) {
    case 0: return gp.y;
    case 1: return gp.b;
    case 2: return gp.x;
    case 3: return gp.a;
    case 4: return gp.dpadUp;
    case 5: return gp.dpadDown;
    case 6: return gp.l3;
    default: return false;
    }
}

static bool GetCtrlMod(const Gamepad::State& gp, int mod) {
    switch (mod) {
    case 0: return gp.lb;
    case 1: return gp.rb;
    case 2: return true;
    default: return false;
    }
}

// SafetyHook
static SafetyHookMid s_fovHook{};
static SafetyHookMid s_fovGetterHook{};
static SafetyHookMid s_armLengthHook{};

// --- Captured from hook ---
// FOV hook: captures camera object for FOV manipulation
static std::atomic<uintptr_t> s_cameraObject{0};
static std::atomic<float> s_baseFOV{0.0f};
static constexpr size_t FOV_OFFSET = 0x26C;

// Camera distance hook: intercepts TargetArmLength write every frame
// At hook point: rcx = SpringArmComponent, xmm0 = TargetArmLength about to be stored
static constexpr size_t ARM_LENGTH_OFFSET = 0x230;
static constexpr size_t LATERAL_OFFSET = 0x244;   // SocketOffset.Y / TargetOffset.Y
static constexpr size_t HEIGHT_OFFSET = 0x24C;    // SocketOffset.Z / TargetOffset.Z
static float s_baseArmLength = 0.0f;  // Captured base value
static bool s_armHookActive = false;
static std::atomic<uintptr_t> s_springArmObj{0};  // SpringArmComponent pointer

// Lateral and height offsets (for 1st person centering)
static float s_lateralOffset = 3.0f;    // Default: UI 30 / 10
static float s_heightOffset = 0.10f;    // Default: UI 10 / 100
static float s_baseLateral = 0.0f;
static float s_baseHeight = 0.0f;
static bool s_baseLateralCaptured = false;
static bool s_firstPersonCenter = false;  // Disabled for tuning

static void ArmLengthHookCallback(SafetyHookContext& ctx) {
    // xmm0 contains the TargetArmLength the game wants to store at [rcx+0x230]
    // The game changes this value based on state (sprint, combat, etc.)
    // We ignore the game's value and always use our base + offset.
    float armLength;
    memcpy(&armLength, &ctx.xmm0, sizeof(float));

    // Store the SpringArmComponent pointer for lateral/height writes
    s_springArmObj.store(ctx.rcx, std::memory_order_relaxed);

    // Capture base on first fire (when we haven't zoomed yet)
    if (s_baseArmLength == 0.0f && armLength > 50.0f) {
        s_baseArmLength = armLength;
        Log("ARM HOOK first capture: armLength=%.2f", armLength);
    }

    // Override: always use base + our offset, ignoring game state changes
    float modified = s_baseArmLength + s_zoomOffset;
    memcpy(&ctx.xmm0, &modified, sizeof(float));

    // --- Lateral & Height direct write ---
    uintptr_t obj = ctx.rcx;
    if (obj != 0) {
        // Capture base lateral/height on first frame
        if (!s_baseLateralCaptured) {
            s_baseLateral = *(float*)((uint8_t*)obj + LATERAL_OFFSET);
            s_baseHeight = *(float*)((uint8_t*)obj + HEIGHT_OFFSET);
            s_baseLateralCaptured = true;
            Log("Captured base lateral=%.2f height=%.2f", s_baseLateral, s_baseHeight);
        }

        // 1st person centering: blend lateral/height when approaching max zoom in (-320)
        const float FP_ZOOM = -320.0f;
        const float FP_LATERAL = 0.0f;       // Centered
        const float FP_HEIGHT = -0.05f;      // UI -5 / 100
        float threshold = FP_ZOOM * 0.8f;    // Start blending at ~-256

        float lateralTarget = s_baseLateral + s_lateralOffset;
        float heightTarget = s_baseHeight + s_heightOffset;

        if (s_zoomOffset <= threshold) {
            float t = (s_zoomOffset - threshold) / (FP_ZOOM - threshold);
            t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
            lateralTarget = lateralTarget + (FP_LATERAL - lateralTarget) * t;
            heightTarget = heightTarget + (FP_HEIGHT - heightTarget) * t;
        }

        float* latPtr = (float*)((uint8_t*)obj + LATERAL_OFFSET);
        *latPtr = lateralTarget;

        float* htPtr = (float*)((uint8_t*)obj + HEIGHT_OFFSET);
        *htPtr = heightTarget;
    }
}

// Getter hook for FOV: captures camera object pointer on first frame
static std::atomic<bool> s_getterCaptured{false};

static void FovGetterCallback(SafetyHookContext& ctx) {
    if (s_getterCaptured.load(std::memory_order_relaxed)) return;

    uintptr_t obj = ctx.rcx;
    if (obj == 0) return;

    float fov = *(float*)((uint8_t*)obj + FOV_OFFSET);
    if (fov > 30.0f && fov < 150.0f) {
        s_cameraObject.store(obj, std::memory_order_relaxed);
        s_baseFOV.store(fov, std::memory_order_relaxed);
        s_getterCaptured.store(true, std::memory_order_relaxed);
        Log("GETTER captured camera obj: rcx=0x%llX, FOV=%.2f", (unsigned long long)obj, fov);
    }
}

static void FovHookCallback(SafetyHookContext& ctx) {
    uintptr_t obj = ctx.rcx;
    s_cameraObject.store(obj, std::memory_order_relaxed);

    float fov;
    memcpy(&fov, &ctx.xmm1, sizeof(float));
    if (fov > 10.0f && fov < 170.0f) {
        s_baseFOV.store(fov, std::memory_order_relaxed);
        // Apply FOV offset
        float modified = fov + s_fovOffset;
        if (modified < 20.0f) modified = 20.0f;
        if (modified > 160.0f) modified = 160.0f;
        memcpy(&ctx.xmm1, &modified, sizeof(float));
    }
}

// --- Pattern scanning ---
static uint8_t* FindPattern(uint8_t* base, size_t size, const uint8_t* pattern, size_t patLen) {
    for (size_t i = 0; i + patLen <= size; i++) {
        bool match = true;
        for (size_t j = 0; j < patLen; j++) {
            if (base[i + j] != pattern[j]) { match = false; break; }
        }
        if (match) return base + i;
    }
    return nullptr;
}

static bool InstallHook() {
    HMODULE gameModule = GetModuleHandleW(nullptr);
    if (!gameModule) { Log("InstallHook: GetModuleHandle failed"); return false; }

    MODULEINFO mi = {};
    if (!GetModuleInformation(GetCurrentProcess(), gameModule, &mi, sizeof(mi))) {
        Log("InstallHook: GetModuleInformation failed");
        return false;
    }

    uint8_t* base = (uint8_t*)mi.lpBaseOfDll;
    size_t size = mi.SizeOfImage;
    Log("InstallHook: Scanning module base=0x%llX size=0x%llX", (unsigned long long)base, (unsigned long long)size);

    // === Camera Distance Hook (PRIMARY) ===
    // Pattern: addss xmm0,[rcx+0x334]; movss [rcx+0x230],xmm0
    // Bytes: F3 0F 58 81 34 03 00 00  F3 0F 11 81 30 02 00 00
    const uint8_t armPattern[] = {
        0xF3, 0x0F, 0x58, 0x81, 0x34, 0x03, 0x00, 0x00,  // addss xmm0,[rcx+0x334]
        0xF3, 0x0F, 0x11, 0x81, 0x30, 0x02, 0x00, 0x00   // movss [rcx+0x230],xmm0
    };

    uint8_t* armAddr = FindPattern(base, size, armPattern, sizeof(armPattern));
    if (armAddr) {
        // Hook at the movss store instruction (8 bytes into the pattern)
        uint8_t* hookAddr = armAddr + 8;
        Log("InstallHook: ArmLength pattern found at 0x%llX, hooking movss at 0x%llX",
            (unsigned long long)armAddr, (unsigned long long)hookAddr);

        auto armResult = safetyhook::MidHook::create(hookAddr, ArmLengthHookCallback);
        if (armResult) {
            s_armLengthHook = std::move(*armResult);
            s_armHookActive = true;
            Log("InstallHook: ArmLength hook installed successfully");
        } else {
            Log("InstallHook: ArmLength MidHook::create FAILED");
        }
    } else {
        Log("InstallHook: ArmLength pattern NOT FOUND");
    }

    // === FOV Setter Hook (optional) ===
    const uint8_t fovPattern[] = { 0xF3, 0x0F, 0x11, 0x89, 0x6C, 0x02, 0x00, 0x00, 0xC3 };
    uint8_t* fovAddr = FindPattern(base, size, fovPattern, sizeof(fovPattern));
    if (fovAddr) {
        Log("InstallHook: FOV setter found at 0x%llX", (unsigned long long)fovAddr);
        auto fovResult = safetyhook::MidHook::create(fovAddr, FovHookCallback);
        if (fovResult) {
            s_fovHook = std::move(*fovResult);
            Log("InstallHook: FOV setter hook installed");
        }
    }

    // === FOV Getter Hook (optional) ===
    const uint8_t getterPattern[] = { 0xF3, 0x0F, 0x10, 0x81, 0x6C, 0x02, 0x00, 0x00, 0xC3 };
    uint8_t* getterAddr = FindPattern(base, size, getterPattern, sizeof(getterPattern));
    if (getterAddr) {
        Log("InstallHook: FOV getter found at 0x%llX", (unsigned long long)getterAddr);
        auto getterResult = safetyhook::MidHook::create(getterAddr, FovGetterCallback);
        if (getterResult) {
            s_fovGetterHook = std::move(*getterResult);
            Log("InstallHook: FOV getter hook installed");
        }
    }

    return s_armHookActive;
}

// =============================================================================
// Safe memory write (SEH-wrapped, can't be in functions with C++ objects)
// =============================================================================
static bool WriteFovSafe(uintptr_t camObj, float value) {
    float* fovPtr = (float*)((uint8_t*)camObj + FOV_OFFSET);
    __try {
        *fovPtr = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static float ReadFovSafe(uintptr_t camObj) {
    float* fovPtr = (float*)((uint8_t*)camObj + FOV_OFFSET);
    __try {
        return *fovPtr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0.0f;
    }
}

// =============================================================================
// Config
// =============================================================================
static void GetConfigPath() {
    GetModuleFileNameW(nullptr, s_configPath, MAX_PATH);
    wchar_t* slash = wcsrchr(s_configPath, L'\\');
    if (slash) wcscpy(slash + 1, L"CameraZoom.ini");
}

static void SaveConfig() {
    if (!s_configPath[0]) return;
    FILE* f = _wfopen(s_configPath, L"w");
    if (!f) return;
    fprintf(f, "[CameraZoom]\n");
    fprintf(f, "Step=%.4f\n", s_zoomStep);
    fprintf(f, "Min=%.3f\n", s_zoomMin);
    fprintf(f, "Max=%.3f\n", s_zoomMax);
    fprintf(f, "Smooth=%.1f\n", s_smoothSpeed);
    fprintf(f, "Repeat=%.3f\n", s_controllerRepeatDelay);
    fprintf(f, "MouseWheel=%d\n", s_mouseWheelEnabled ? 1 : 0);
    fprintf(f, "ScrollMod=%s\n", s_scrollModifier.display.c_str());
    fprintf(f, "ZoomIn=%s\n", s_zoomInKey.display.c_str());
    fprintf(f, "ZoomOut=%s\n", s_zoomOutKey.display.c_str());
    fprintf(f, "Reset=%s\n", s_resetKey.display.c_str());
    fprintf(f, "FPToggle=%s\n", s_fpToggleKey.display.c_str());
    fprintf(f, "CtrlZoomIn=%d\n", s_ctrlZoomIn);
    fprintf(f, "CtrlZoomOut=%d\n", s_ctrlZoomOut);
    fprintf(f, "CtrlFPToggle=%d\n", s_ctrlFPToggle);
    fprintf(f, "CtrlMod=%d\n", s_ctrlMod);
    fprintf(f, "LateralOffset=%.2f\n", s_lateralOffset);
    fprintf(f, "HeightOffset=%.2f\n", s_heightOffset);
    fprintf(f, "FirstPersonCenter=%d\n", s_firstPersonCenter ? 1 : 0);
    fprintf(f, "FovOffset=%.1f\n", s_fovOffset);
    fclose(f);
}

static void LoadConfig() {
    if (!s_configPath[0]) return;
    FILE* f = _wfopen(s_configPath, L"r");
    if (!f) return;
    char line[256]; float fv; int iv;
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = 0;

        if (sscanf(line, "Step=%f", &fv) == 1) s_zoomStep = fv;
        else if (sscanf(line, "Min=%f", &fv) == 1) s_zoomMin = fv;
        else if (sscanf(line, "Max=%f", &fv) == 1) s_zoomMax = fv;
        else if (sscanf(line, "Smooth=%f", &fv) == 1) s_smoothSpeed = fv;
        else if (sscanf(line, "Repeat=%f", &fv) == 1) s_controllerRepeatDelay = fv;
        else if (sscanf(line, "MouseWheel=%d", &iv) == 1) s_mouseWheelEnabled = (iv != 0);
        else if (strncmp(line, "ScrollMod=", 10) == 0) s_scrollModifier.ParseFromString(line + 10);
        else if (strncmp(line, "ZoomIn=", 7) == 0) s_zoomInKey.ParseFromString(line + 7);
        else if (strncmp(line, "ZoomOut=", 8) == 0) s_zoomOutKey.ParseFromString(line + 8);
        else if (strncmp(line, "Reset=", 6) == 0) s_resetKey.ParseFromString(line + 6);
        else if (strncmp(line, "FPToggle=", 9) == 0) s_fpToggleKey.ParseFromString(line + 9);
        else if (sscanf(line, "CtrlZoomIn=%d", &iv) == 1) s_ctrlZoomIn = iv;
        else if (sscanf(line, "CtrlZoomOut=%d", &iv) == 1) s_ctrlZoomOut = iv;
        else if (sscanf(line, "CtrlFPToggle=%d", &iv) == 1) s_ctrlFPToggle = iv;
        else if (sscanf(line, "CtrlMod=%d", &iv) == 1) s_ctrlMod = iv;
        else if (sscanf(line, "LateralOffset=%f", &fv) == 1) s_lateralOffset = fv;
        else if (sscanf(line, "HeightOffset=%f", &fv) == 1) s_heightOffset = fv;
        else if (sscanf(line, "FirstPersonCenter=%d", &iv) == 1) s_firstPersonCenter = (iv != 0);
        else if (sscanf(line, "FovOffset=%f", &fv) == 1) s_fovOffset = fv;
    }
    fclose(f);
}

static void SetDefaults() {
    s_zoomStep = DEF_STEP; s_zoomMin = DEF_MIN; s_zoomMax = DEF_MAX;
    s_smoothSpeed = DEF_SMOOTH; s_controllerRepeatDelay = DEF_REPEAT;
    s_mouseWheelEnabled = DEF_MOUSEWHEEL;
    s_scrollModifier.Clear();
    s_zoomInKey.Clear();
    s_zoomOutKey.Clear();
    s_resetKey.Set(true, false, false, 'R'); // Ctrl+R
    s_fpToggleKey.Set(false, true, false, 'R'); // Shift+R
    s_ctrlZoomIn = DEF_CTRL_ZIN;
    s_ctrlZoomOut = DEF_CTRL_ZOUT;
    s_ctrlMod = DEF_CTRL_MOD;
}

// =============================================================================
// Per-frame logic
// =============================================================================
static HWND FindGameWindow() { return FindWindowW(L"UnrealWindow", nullptr); }

static void OnReshadePresent(reshade::api::effect_runtime*) {
    if (!s_initialized) {
        s_gameWindow = FindGameWindow();
        if (s_gameWindow) {
            Input::Init(s_gameWindow);
            Gamepad::Init();
            s_initialized = true;
            s_lastFrameTime = std::chrono::steady_clock::now();

            // Install the hook once we're in-game
            if (!s_hookActive) {
                s_hookActive = InstallHook();
                Log("OnPresent: hookActive=%d", s_hookActive ? 1 : 0);
            }
        }
    }
    if (!s_initialized) return;

    if (!s_configLoaded) {
        SetDefaults();
        LoadConfig();
        s_configLoaded = true;
    }

    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - s_lastFrameTime).count();
    s_lastFrameTime = now;
    if (dt > 0.1f) dt = 0.1f;

    // --- Mouse wheel ---
    float wheelDelta = Input::ConsumeWheelDelta();
    if (s_mouseWheelEnabled && wheelDelta != 0.0f) {
        bool modOk = !s_scrollModifier.IsBound() || Input::IsKeyDown(s_scrollModifier.vkCode);
        if (modOk) {
            // Wheel up = zoom in = decrease factor
            s_zoomTarget -= wheelDelta * s_zoomStep;
            s_zoomTarget = std::clamp(s_zoomTarget, s_zoomMin, s_zoomMax);
            s_fpActive = false;  // Manual scroll exits FP toggle
        }
    }

    // --- Keyboard zoom in (held) ---
    if (s_zoomInKey.IsHeld()) {
        s_zoomTarget -= s_zoomStep * dt * 5.0f;
        s_zoomTarget = std::clamp(s_zoomTarget, s_zoomMin, s_zoomMax);
        s_fpActive = false;
    }

    // --- Keyboard zoom out (held) ---
    if (s_zoomOutKey.IsHeld()) {
        s_zoomTarget += s_zoomStep * dt * 5.0f;
        s_zoomTarget = std::clamp(s_zoomTarget, s_zoomMin, s_zoomMax);
        s_fpActive = false;
    }

    // --- Keyboard reset (edge) ---
    if (s_resetKey.WasPressed()) {
        s_zoomTarget = 0.0f;
        s_fpActive = false;
    }

    // --- First-person toggle ---
    if (s_fpToggleKey.WasPressed()) {
        if (!s_fpActive) {
            // Enter first person: save current position, snap to -320
            s_fpSavedZoom = s_zoomTarget;
            s_zoomTarget = -320.0f;
            s_zoomOffset = -320.0f;  // Instant snap
            s_fpActive = true;
        } else {
            // Exit first person: restore saved position instantly
            s_zoomTarget = s_fpSavedZoom;
            s_zoomOffset = s_fpSavedZoom;  // Instant snap
            s_fpActive = false;
        }
    }

    // --- Controller ---
    Gamepad::State gp = Gamepad::Poll();
    if (gp.connected && GetCtrlMod(gp, s_ctrlMod)) {
        int dir = 0;
        if (GetCtrlBtn(gp, s_ctrlZoomIn)) dir = -1;
        else if (GetCtrlBtn(gp, s_ctrlZoomOut)) dir = 1;

        if (dir != 0) {
            s_controllerRepeatTimer += dt;
            if (s_controllerRepeatTimer >= s_controllerRepeatDelay) {
                s_zoomTarget += dir * s_zoomStep;
                s_zoomTarget = std::clamp(s_zoomTarget, s_zoomMin, s_zoomMax);
                s_controllerRepeatTimer = 0.0f;
                s_fpActive = false;
            }
        } else {
            s_controllerRepeatTimer = s_controllerRepeatDelay;
        }
        // Controller reset: modifier + other shoulder + R3
        // Controller reset: LB + LT + Left Stick Click
        if (gp.lb && gp.lt && gp.l3) { s_zoomTarget = 0.0f; s_zoomOffset = 0.0f; s_fpActive = false; }

        // Controller FP toggle: modifier + FP button (edge-triggered)
        static bool s_ctrlFPPrev = false;
        bool fpBtn = GetCtrlBtn(gp, s_ctrlFPToggle);
        if (fpBtn && !s_ctrlFPPrev) {
            if (!s_fpActive) {
                s_fpSavedZoom = s_zoomTarget;
                s_zoomTarget = -320.0f;
                s_zoomOffset = -320.0f;
                s_fpActive = true;
            } else {
                s_zoomTarget = s_fpSavedZoom;
                s_zoomOffset = s_fpSavedZoom;
                s_fpActive = false;
            }
        }
        s_ctrlFPPrev = fpBtn;
    } else {
        s_controllerRepeatTimer = s_controllerRepeatDelay;
    }

    // --- Smoothing ---
    // s_zoomOffset is read by ArmLengthHookCallback each frame
    s_zoomOffset += (s_zoomTarget - s_zoomOffset) * (1.0f - std::exp(-s_smoothSpeed * dt));

    // --- Per-frame FOV write (if we have an offset and a camera object) ---
    if (s_fovOffset != 0.0f) {
        uintptr_t camObj = s_cameraObject.load(std::memory_order_relaxed);
        float baseFov = s_baseFOV.load(std::memory_order_relaxed);
        if (camObj != 0 && baseFov > 10.0f) {
            float modFov = baseFov + s_fovOffset;
            if (modFov < 20.0f) modFov = 20.0f;
            if (modFov > 160.0f) modFov = 160.0f;
            WriteFovSafe(camObj, modFov);
        }
    }
}

// =============================================================================
// ImGui keybind capture
// =============================================================================
static int ImGuiKeyToVK(ImGuiKey key) {
    if (key >= ImGuiKey_A && key <= ImGuiKey_Z) return 'A' + (key - ImGuiKey_A);
    if (key >= ImGuiKey_0 && key <= ImGuiKey_9) return '0' + (key - ImGuiKey_0);
    if (key >= ImGuiKey_F1 && key <= ImGuiKey_F12) return VK_F1 + (key - ImGuiKey_F1);
    if (key >= ImGuiKey_Keypad0 && key <= ImGuiKey_Keypad9) return VK_NUMPAD0 + (key - ImGuiKey_Keypad0);
    switch (key) {
    case ImGuiKey_Space: return VK_SPACE;
    case ImGuiKey_Enter: return VK_RETURN;
    case ImGuiKey_Backspace: return VK_BACK;
    case ImGuiKey_Delete: return VK_DELETE;
    case ImGuiKey_Insert: return VK_INSERT;
    case ImGuiKey_Home: return VK_HOME;
    case ImGuiKey_End: return VK_END;
    case ImGuiKey_PageUp: return VK_PRIOR;
    case ImGuiKey_PageDown: return VK_NEXT;
    case ImGuiKey_Tab: return VK_TAB;
    case ImGuiKey_GraveAccent: return VK_OEM_3;
    case ImGuiKey_Minus: return VK_OEM_MINUS;
    case ImGuiKey_Equal: return VK_OEM_PLUS;
    case ImGuiKey_LeftBracket: return VK_OEM_4;
    case ImGuiKey_RightBracket: return VK_OEM_6;
    case ImGuiKey_Backslash: return VK_OEM_5;
    case ImGuiKey_Semicolon: return VK_OEM_1;
    case ImGuiKey_Apostrophe: return VK_OEM_7;
    case ImGuiKey_Comma: return VK_OEM_COMMA;
    case ImGuiKey_Period: return VK_OEM_PERIOD;
    case ImGuiKey_Slash: return VK_OEM_2;
    case ImGuiKey_UpArrow: return VK_UP;
    case ImGuiKey_DownArrow: return VK_DOWN;
    case ImGuiKey_LeftArrow: return VK_LEFT;
    case ImGuiKey_RightArrow: return VK_RIGHT;
    case ImGuiKey_KeypadMultiply: return VK_MULTIPLY;
    case ImGuiKey_KeypadAdd: return VK_ADD;
    case ImGuiKey_KeypadSubtract: return VK_SUBTRACT;
    case ImGuiKey_KeypadDecimal: return VK_DECIMAL;
    case ImGuiKey_KeypadDivide: return VK_DIVIDE;
    default: return 0;
    }
}

static int CaptureKeybind(Keybind& out) {
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) return -1;

    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; k++) {
        ImGuiKey key = (ImGuiKey)k;
        if (key == ImGuiKey_LeftCtrl || key == ImGuiKey_RightCtrl) continue;
        if (key == ImGuiKey_LeftShift || key == ImGuiKey_RightShift) continue;
        if (key == ImGuiKey_LeftAlt || key == ImGuiKey_RightAlt) continue;
        if (key == ImGuiKey_LeftSuper || key == ImGuiKey_RightSuper) continue;
        if (key >= ImGuiKey_MouseLeft && key <= ImGuiKey_MouseX2) continue;

        if (ImGui::IsKeyPressed(key, false)) {
            int vk = ImGuiKeyToVK(key);
            if (vk == 0) continue;
            bool ctrl = ImGui::IsKeyDown(ImGuiMod_Ctrl);
            bool shift = ImGui::IsKeyDown(ImGuiMod_Shift);
            bool alt = ImGui::IsKeyDown(ImGuiMod_Alt);
            out.Set(ctrl, shift, alt, vk);
            return 1;
        }
    }

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
        out.Set(ImGui::IsKeyDown(ImGuiMod_Ctrl), ImGui::IsKeyDown(ImGuiMod_Shift), ImGui::IsKeyDown(ImGuiMod_Alt), VK_MBUTTON);
        return 1;
    }
    if (ImGui::IsMouseClicked(3)) {
        out.Set(ImGui::IsKeyDown(ImGuiMod_Ctrl), ImGui::IsKeyDown(ImGuiMod_Shift), ImGui::IsKeyDown(ImGuiMod_Alt), VK_XBUTTON1);
        return 1;
    }
    if (ImGui::IsMouseClicked(4)) {
        out.Set(ImGui::IsKeyDown(ImGuiMod_Ctrl), ImGui::IsKeyDown(ImGuiMod_Shift), ImGui::IsKeyDown(ImGuiMod_Alt), VK_XBUTTON2);
        return 1;
    }

    return 0;
}

// =============================================================================
// Overlay UI
// =============================================================================
static int s_capturing = 0;

static void OnOverlay(reshade::api::effect_runtime*) {
    // --- Status ---
    if (s_armHookActive) {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Camera Distance Active");
        ImGui::SameLine();
        ImGui::Text("| Offset: %.0f", s_zoomOffset);
        if (s_baseArmLength > 0.0f) {
            ImGui::SameLine();
            ImGui::Text("| Base: %.0f | Effective: %.0f", s_baseArmLength, s_baseArmLength + s_zoomOffset);
        }
    } else if (s_hookActive) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "FOV only (camera distance pattern not found)");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.2f, 1.0f), "No hooks installed - AOB patterns not found");
        return;
    }

    bool changed = false;

    // --- Zoom Range ---
    ImGui::Spacing();
    ImGui::SeparatorText("Camera Distance");
    changed |= ImGui::SliderFloat("Max Zoom In", &s_zoomMin, -320.0f, 0.0f, "%.0f");
    changed |= ImGui::SliderFloat("Max Zoom Out", &s_zoomMax, 0.0f, 2000.0f, "%.0f");

    // --- Lateral & Height ---
    // UI values are scaled for usability:
    //   Lateral: UI range -40 to +40, actual = UI / 10
    //   Height:  UI range -100 to +100, actual = UI / 100
    ImGui::Spacing();
    ImGui::SeparatorText("Camera Offset");
    static float s_lateralUI = s_lateralOffset * 10.0f;
    static float s_heightUI = s_heightOffset * 100.0f;
    static bool s_offsetUIInit = false;
    if (!s_offsetUIInit) {
        s_lateralUI = s_lateralOffset * 10.0f;
        s_heightUI = s_heightOffset * 100.0f;
        s_offsetUIInit = true;
    }
    if (ImGui::SliderFloat("Lateral Offset", &s_lateralUI, -80.0f, 80.0f, "%.0f")) {
        s_lateralOffset = s_lateralUI / 10.0f;
        changed = true;
    }
    if (ImGui::SliderFloat("Height Offset", &s_heightUI, -200.0f, 200.0f, "%.0f")) {
        s_heightOffset = s_heightUI / 100.0f;
        changed = true;
    }
    // 1st Person Centering (disabled for tuning)
    // if (ImGui::Checkbox("1st Person Centering", &s_firstPersonCenter)) changed = true;

    // --- FOV ---
    ImGui::Spacing();
    ImGui::SeparatorText("Field of View");
    float baseFov = s_baseFOV.load(std::memory_order_relaxed);
    if (baseFov > 10.0f) {
        ImGui::Text("Base: %.0f | Effective: %.0f", baseFov, baseFov + s_fovOffset);
    } else {
        ImGui::TextDisabled("FOV not captured yet (change FOV in game options once)");
    }
    if (ImGui::SliderFloat("FOV Offset", &s_fovOffset, -40.0f, 40.0f, "%.0f")) changed = true;
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset##fov")) { s_fovOffset = 0.0f; changed = true; }

    // --- Speed ---
    ImGui::Spacing();
    ImGui::SeparatorText("Speed");
    changed |= ImGui::SliderFloat("Scroll Step", &s_zoomStep, 5.0f, 100.0f, "%.0f");
    changed |= ImGui::SliderFloat("Smoothing", &s_smoothSpeed, 1.0f, 20.0f, "%.0f");
    changed |= ImGui::SliderFloat("Repeat Rate", &s_controllerRepeatDelay, 0.05f, 0.5f, "%.2f s");

    // --- Keyboard / Mouse ---
    ImGui::Spacing();
    ImGui::SeparatorText("Keyboard / Mouse");

    if (ImGui::Checkbox("Mouse Wheel Zoom", &s_mouseWheelEnabled)) changed = true;

    if (s_mouseWheelEnabled) {
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10.0f);

        if (s_capturing == 4) {
            ImGui::TextColored(ImVec4(1, 1, 0.3f, 1), "Scroll Mod: [Press key... Esc=cancel]");
            Keybind temp;
            int result = CaptureKeybind(temp);
            if (result == 1) { s_scrollModifier = temp; s_capturing = 0; changed = true; }
            else if (result == -1) { s_capturing = 0; }
        } else {
            if (s_scrollModifier.IsBound()) {
                ImGui::Text("Hold: %s", s_scrollModifier.display.c_str());
            } else {
                ImGui::TextDisabled("Hold: (none)");
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Ctrl##sm")) { s_scrollModifier.Set(false, false, false, VK_CONTROL); changed = true; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Shift##sm")) { s_scrollModifier.Set(false, false, false, VK_SHIFT); changed = true; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Alt##sm")) { s_scrollModifier.Set(false, false, false, VK_MENU); changed = true; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Bind##scrollmod")) s_capturing = 4;
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear##scrollmod")) { s_scrollModifier.Clear(); changed = true; }
        }
    }

    // Keybind rows
    auto KeybindRow = [&](const char* label, int id, Keybind& bind) {
        ImGui::PushID(id);
        if (s_capturing == id) {
            ImGui::TextColored(ImVec4(1, 1, 0.3f, 1), "%s: [Press combo... Esc=cancel]", label);
            Keybind temp;
            int result = CaptureKeybind(temp);
            if (result == 1) { bind = temp; s_capturing = 0; changed = true; }
            else if (result == -1) { s_capturing = 0; }
        } else {
            if (bind.IsBound()) {
                ImGui::Text("%s: %s", label, bind.display.c_str());
            } else {
                ImGui::Text("%s:", label); ImGui::SameLine(); ImGui::TextDisabled("(none)");
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Bind")) s_capturing = id;
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear")) { bind.Clear(); changed = true; }
        }
        ImGui::PopID();
    };

    KeybindRow("Zoom In", 1, s_zoomInKey);
    KeybindRow("Zoom Out", 2, s_zoomOutKey);
    KeybindRow("Reset", 3, s_resetKey);
    KeybindRow("1st Person Toggle", 5, s_fpToggleKey);

    // --- Controller ---
    ImGui::Spacing();
    ImGui::SeparatorText("Controller");

    if (ImGui::Combo("Zoom In##ctrl", &s_ctrlZoomIn, CtrlBtnNames, CTRL_BTN_COUNT)) changed = true;
    if (ImGui::Combo("Zoom Out##ctrl", &s_ctrlZoomOut, CtrlBtnNames, CTRL_BTN_COUNT)) changed = true;
    if (ImGui::Combo("1st Person##ctrl", &s_ctrlFPToggle, CtrlBtnNames, CTRL_BTN_COUNT)) changed = true;
    if (ImGui::Combo("Modifier##ctrl", &s_ctrlMod, CtrlModNames, CTRL_MOD_COUNT)) changed = true;

    ImGui::Text("Reset: LB + LT + L3");

    // --- Save ---
    if (changed) SaveConfig();

    // --- Buttons ---
    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Reset Zoom")) {
        s_zoomTarget = 0.0f;
        s_zoomOffset = 0.0f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset All Settings")) {
        SetDefaults();
        s_zoomTarget = 0.0f;
        s_zoomOffset = 0.0f;
        SaveConfig();
    }
}

// =============================================================================
// DLL Entry
// =============================================================================
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        if (!reshade::register_addon(hModule)) return FALSE;
        LogInit();
        Log("DLL_PROCESS_ATTACH: addon registered");
        GetConfigPath();
        reshade::register_event<reshade::addon_event::reshade_present>(OnReshadePresent);
        reshade::register_overlay("Camera Zoom", OnOverlay);
        break;
    case DLL_PROCESS_DETACH:
        // Reset zoom offset so arm length hook writes clean values on remaining frames
        s_zoomOffset = 0.0f;
        Log("DLL_PROCESS_DETACH: cleaning up");
        s_armLengthHook = {};
        s_fovHook = {};
        s_fovGetterHook = {};
        Gamepad::Shutdown();
        Input::Shutdown();
        reshade::unregister_addon(hModule);
        if (s_logFile) { fclose(s_logFile); s_logFile = nullptr; }
        break;
    }
    return TRUE;
}
