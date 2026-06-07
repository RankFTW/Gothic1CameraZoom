// Force ImGui symbols to be imported (resolved from ReShade at load time)
#pragma once
#define IMGUI_API __declspec(dllimport)
#define IMGUI_DISABLE_DEMO_WINDOWS
#define IMGUI_DISABLE_DEBUG_TOOLS
#include <imgui.h>
