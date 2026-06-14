#include "input.h"
#include <mutex>

namespace Input {

static HWND s_gameWindow = nullptr;
static WNDPROC s_originalWndProc = nullptr;
static std::atomic<float> s_wheelDelta{0.0f};
static bool s_keyState[256] = {};
static bool s_keyPrevState[256] = {};
static std::mutex s_mutex;

static LRESULT CALLBACK HookedWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_MOUSEWHEEL: {
        float delta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA;
        s_wheelDelta.store(s_wheelDelta.load() + delta);
        break;
    }
    case WM_KEYDOWN:
        if (wParam < 256) {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_keyState[wParam] = true;
        }
        break;
    case WM_KEYUP:
        if (wParam < 256) {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_keyState[wParam] = false;
        }
        break;
    case WM_MBUTTONDOWN: {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_keyState[VK_MBUTTON] = true;
        break;
    }
    case WM_MBUTTONUP: {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_keyState[VK_MBUTTON] = false;
        break;
    }
    case WM_XBUTTONDOWN: {
        std::lock_guard<std::mutex> lock(s_mutex);
        int btn = GET_XBUTTON_WPARAM(wParam);
        if (btn == XBUTTON1) s_keyState[VK_XBUTTON1] = true;
        if (btn == XBUTTON2) s_keyState[VK_XBUTTON2] = true;
        break;
    }
    case WM_XBUTTONUP: {
        std::lock_guard<std::mutex> lock(s_mutex);
        int btn = GET_XBUTTON_WPARAM(wParam);
        if (btn == XBUTTON1) s_keyState[VK_XBUTTON1] = false;
        if (btn == XBUTTON2) s_keyState[VK_XBUTTON2] = false;
        break;
    }
    }

    return CallWindowProcW(s_originalWndProc, hwnd, msg, wParam, lParam);
}

void Init(HWND gameWindow) {
    s_gameWindow = gameWindow;
    s_originalWndProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(gameWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(HookedWndProc))
    );
}

void Shutdown() {
    if (s_gameWindow && s_originalWndProc) {
        SetWindowLongPtrW(s_gameWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(s_originalWndProc));
        s_originalWndProc = nullptr;
        s_gameWindow = nullptr;
    }
}

float ConsumeWheelDelta() {
    return s_wheelDelta.exchange(0.0f);
}

bool IsKeyDown(int vk) {
    if (vk < 0 || vk >= 256) return false;
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_keyState[vk];
}

bool WasKeyPressed(int vk) {
    if (vk < 0 || vk >= 256) return false;
    std::lock_guard<std::mutex> lock(s_mutex);
    bool pressed = s_keyState[vk] && !s_keyPrevState[vk];
    s_keyPrevState[vk] = s_keyState[vk];
    return pressed;
}

} // namespace Input
