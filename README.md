# Camera Zoom - Mouse Wheel Control for Gothic 1 Remake

Adds mouse wheel camera zoom to Gothic 1 Remake.

## Requirements

- [ReShade](https://reshade.me/) with full addon support
- [Gothic 1 Remake Camera Fix by k4sh](https://www.nexusmods.com/gothic1remake/mods/36) (Gothic1RemakeCore.dll + Gothic1RemakeUI.addon64)

## Installation

1. Install ReShade with full addon support for Gothic 1 Remake
2. Install k4sh's camera fix mod from Nexus
3. Copy `CameraZoom.addon64` to the game's executable folder:
   `Gothic 1 Remake\G1R\Binaries\Win64\`
4. Launch the game

## Usage

1. Open ReShade overlay (Home key) and enable the camera fix in k4sh's UI
2. Close the overlay and play — mouse wheel or controller now controls zoom

### Keyboard & Mouse

| Input | Action |
|-------|--------|
| Mouse Wheel Down | Zoom Out |
| Mouse Wheel Up | Zoom In (back to default) |
| Middle Mouse | Reset to default |
| Ctrl+R | Reset to default |

### Controller (Xbox / PlayStation)

| Xbox | PlayStation | Action |
|------|-------------|--------|
| LB + Y | L1 + Triangle | Zoom In |
| LB + A | L1 + Cross | Zoom Out |
| LB + RB + R3 | L1 + R1 + R3 | Reset to default |

Hold the button to keep zooming in steps. Y/A (Triangle/Cross) are blocked from the game while LB (L1) is held.

## Notes

- Zoom out range goes beyond the default third-person camera
- Zoom in stops at the default camera position (won't clip through the character)
- Smooth interpolation for fluid camera movement

## Credits

- **k4sh** — Gothic 1 Remake camera fix mod (Gothic1RemakeCore.dll) which does the actual FOV hooking
- **crosire** — ReShade and the addon API
