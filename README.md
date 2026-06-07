# Camera Zoom - Mouse Wheel & Controller Support for Gothic 1 Remake

Adds mouse wheel and controller camera zoom to Gothic 1 Remake via a ReShade addon companion for k4sh's camera fix mod.

## Requirements

- [ReShade](https://reshade.me/) with full addon support — easy install with [RHI](https://www.nexusmods.com/site/mods/1710)
- [Gothic 1 Remake Camera Fix by k4sh](https://www.nexusmods.com/gothic1remake/mods/36) (Gothic1RemakeCore.dll + Gothic1RemakeUI.addon64)

## Installation

1. Install ReShade with full addon support for Gothic 1 Remake
2. Install k4sh's camera fix mod from Nexus
3. Copy `CameraZoom.addon64` to your game folder:
   ```
   Gothic 1 Remake\G1R\Binaries\Win64\
   ```
4. Launch the game

## Setup (first time)

1. Open the ReShade overlay (Home key)
2. Enable the camera fix in k4sh's panel (tick the checkbox)
3. Close the overlay — zoom is now active

## Controls

### Keyboard & Mouse

| Input | Action |
|-------|--------|
| Mouse Wheel Up | Zoom In |
| Mouse Wheel Down | Zoom Out |
| Ctrl+R | Reset to default (rebindable) |

### Controller (Xbox / PlayStation)

| Xbox | PlayStation | Action |
|------|-------------|--------|
| LB + Y | L1 + Triangle | Zoom In |
| LB + A | L1 + Cross | Zoom Out |
| LB + RB + R3 | L1 + R1 + R3 | Reset to default |

Hold the button to keep zooming in steps. Works with both XInput (Xbox/Steam) and native DualSense (PS5 via USB/Bluetooth).

## Settings (ReShade Overlay)

Open the ReShade overlay (Home key) and click the **Camera Zoom** tab to configure:

- Max zoom in/out range
- Scroll step size and smoothing
- Controller repeat rate
- Reset key modifier (dropdown: None/Ctrl/Shift/Alt) and key (rebindable)
- Controller button bindings (cycle through options)
- Reset Zoom button
- Reset All Settings button (restores all defaults)

Settings are saved to `CameraZoom.ini` in the game folder and persist between sessions. The mod reads k4sh's camera distance value from `pluginsettings.ini` as the baseline so both mods work together seamlessly.

## Building

### Requirements
- CMake 3.20+
- Visual Studio 2022+ (MSVC Build Tools)

### Build
```cmd
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```

Output: `build/bin/Release/CameraZoom.addon64`

## Notes

- Zoom out goes beyond the default third-person camera distance
- Zoom in stops at the default camera position (won't clip through the character)
- Smooth interpolation for fluid camera movement
- PS5 DualSense is supported natively — no need for Steam Input or DS4Windows
- Compatible with [Analogue Movement mod](https://www.nexusmods.com/gothic1remake/mods/31)

## Credits

- **k4sh** — Gothic 1 Remake camera fix mod (Gothic1RemakeCore.dll) which handles the actual camera hooking
- **crosire** — ReShade and the addon API
