#pragma once
#include <Windows.h>
#include <atomic>

namespace Input {

// Initialize the input hook (WndProc subclass)
void Init(HWND gameWindow);

// Shutdown and restore original WndProc
void Shutdown();

// Get accumulated mouse wheel delta since last call (resets after read)
float ConsumeWheelDelta();

// Check if a key is currently held
bool IsKeyDown(int vk);

// Check if a key was just pressed (edge trigger)
bool WasKeyPressed(int vk);

} // namespace Input
