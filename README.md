# Camera Zoom and First Person Mode - Mouse and Controller

Take control of your camera. Zoom out for a wider view of the world or zoom all the way in for a first-person perspective. Adjust camera distance, lateral offset, height, and FOV — all from the ReShade overlay. Mouse wheel and controller support with smooth, configurable transitions.

## v2.1 — Now Fully Standalone

k4sh's Gothic1RemakeCore.dll is **no longer required**. Camera Zoom now hooks the game directly. Just drop in the addon and go.

A companion version that works with k4sh's mod will also be made available later for those who prefer it.

## Requirements

- [ReShade](https://reshade.me/) with **full addon support** (not the regular version) — easy install with [RHI](https://www.nexusmods.com/site/mods/1710)

## Installation

1. Install ReShade with full addon support for Gothic 1 Remake
2. Copy **CameraZoom.addon64** to your game folder:
   ```
   Gothic 1 Remake\G1R\Binaries\Win64\
   ```
3. Launch the game — zoom is immediately active

## Upgrading from v1.x

If you previously used Camera Zoom v1.x with k4sh's mod, you can remove his files as they are no longer needed:

- Delete **Gothic1RemakeCore.dll** from the game folder
- Delete **Gothic1RemakeUI.addon64** from the game folder
- Delete **pluginsettings.ini** from the game folder (if present)
- Delete your old **CameraZoom.ini** — v2.0 uses different settings format

k4sh's mod may conflict with Camera Zoom v2.0+ as both hook camera distance. Do not run them together.

## Default Controls

### Keyboard & Mouse

| Action | Default |
|--------|---------|
| Zoom In/Out | Mouse Wheel |
| Reset | Ctrl+R |
| Toggle 1st/3rd Person | Shift+R |

- Zoom in/out can be bound to any key or mouse button (Mouse 4/5, etc.)
- Mouse wheel can be disabled for users who use scroll for weapon switching

### Controller (Xbox)

| Action | Default |
|--------|---------|
| Zoom In | LB + Y |
| Zoom Out | LB + A |
| Toggle 1st/3rd Person | LB + L3 |
| Reset | LB + LT + L3 |

### Controller (PlayStation / DualSense)

| Action | Default |
|--------|---------|
| Zoom In | L1 + Triangle |
| Zoom Out | L1 + Cross |
| Toggle 1st/3rd Person | L1 + L3 |
| Reset | L1 + L2 + L3 |

All controls are fully rebindable via the ReShade overlay. Works with both XInput (Xbox/Steam) and native DualSense (PS5 via USB/Bluetooth).

## Settings (ReShade Overlay)

Open the ReShade overlay (Home key) and click the **Camera Zoom** tab to configure:

### Camera Distance

| Setting | Default | Description |
|---------|---------|-------------|
| Max Zoom In | -320 | How close the camera can get (full first person) |
| Max Zoom Out | 600 | How far the camera can go |
| Scroll Step | 70 | Distance per scroll tick |
| Smoothing | 11 | Interpolation speed |

### Camera Offset

| Setting | Default | Description |
|---------|---------|-------------|
| Lateral Offset | 30 | Shift camera left/right |
| Height Offset | 10 | Shift camera up/down |

First-person centering: lateral and height automatically blend to centered when fully zoomed in.

### Field of View

| Setting | Default | Description |
|---------|---------|-------------|
| FOV Offset | 0 | Adjust field of view wider or narrower |

FOV requires one FOV change in game options to activate (the hook captures the camera on first call).

### Keyboard/Mouse Bindings

- Mouse wheel zoom toggle (enable/disable)
- Scroll modifier: hold a key while scrolling to prevent zoom in menus
- Zoom in — rebindable to any key or mouse button
- Zoom out — rebindable to any key or mouse button
- Reset — rebindable with modifier (default: Ctrl+R)
- 1st Person Toggle — rebindable (default: Shift+R)

### Controller Bindings

- Zoom in — dropdown: Y/Triangle, B/Circle, X/Square, A/Cross, D-pad Up, D-pad Down
- Zoom out — dropdown: same options
- 1st Person — dropdown: same options (default: L3/Left Stick)
- Modifier — dropdown: LB/L1, RB/R1, or None

Settings are saved to **CameraZoom.ini** in the game folder and persist between sessions.

## Features

- Camera distance locked — sprinting and drawing weapons no longer push the camera out
- Zoom in far enough for a full first-person view
- Instant 1st/3rd person toggle (keybind or controller button)
- Camera automatically centers (lateral and height) when entering first person
- Smooth transition into first person starts at 80% zoom depth
- Smooth interpolation for all zoom movement
- PS5 DualSense supported natively — no need for Steam Input or DS4Windows

## Known Issues

- Rarely the camera can get stuck after using the 1st person toggle — press reset (Ctrl+R or LB+LT+L3) to fix
- FOV slider requires one FOV change in game options to activate

## Compatibility

- [Analogue Movement mod](https://www.nexusmods.com/gothic1remake/mods/31) — unknown compatibility due to static camera (Camera Zoom locks the distance which may conflict with Analogue Movement's camera behaviour changes)

## Recommended Mods

- [Ultra+](https://www.nexusmods.com/gothic1remake/mods/19) — graphics and performance optimisation framework

## Credits

- **crosire** — ReShade and the addon API
- **cursey** — SafetyHook library

## Source Code

[https://github.com/RankFTW/Gothic1CameraZoom](https://github.com/RankFTW/Gothic1CameraZoom)
