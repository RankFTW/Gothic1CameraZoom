#pragma once
#include <Windows.h>

// Unified gamepad state - works with both XInput and DualSense HID
namespace Gamepad {

struct State {
    bool connected = false;
    bool lb = false;        // L1
    bool rb = false;        // R1
    bool y = false;         // Triangle
    bool a = false;         // Cross
    bool r3 = false;        // Right stick click
};

// Initialize (call once)
void Init();

// Poll current state (call each frame)
State Poll();

// Shutdown
void Shutdown();

} // namespace Gamepad
