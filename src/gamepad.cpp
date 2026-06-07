#include "gamepad.h"
#include <Xinput.h>
#include <hidsdi.h>
#include <SetupAPI.h>
#include <cstring>

#pragma comment(lib, "xinput.lib")
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")

namespace Gamepad {

// DualSense USB/BT vendor and product IDs
static constexpr USHORT SONY_VID = 0x054C;
static constexpr USHORT DUALSENSE_PID = 0x0CE6;
static constexpr USHORT DUALSENSE_EDGE_PID = 0x0DF2;

static HANDLE s_dsHandle = INVALID_HANDLE_VALUE;
static bool s_dsViaBluetooth = false;

static HANDLE OpenDualSense() {
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);

    HDEVINFO devInfo = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;

    SP_DEVICE_INTERFACE_DATA ifData = {};
    ifData.cbSize = sizeof(ifData);

    HANDLE result = INVALID_HANDLE_VALUE;

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, nullptr, &hidGuid, i, &ifData); i++) {
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, nullptr, 0, &needed, nullptr);

        auto detail = (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)malloc(needed);
        if (!detail) continue;
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        if (SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, detail, needed, nullptr, nullptr)) {
            HANDLE h = CreateFileW(detail->DevicePath, GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);

            if (h != INVALID_HANDLE_VALUE) {
                HIDD_ATTRIBUTES attrs = {};
                attrs.Size = sizeof(attrs);
                if (HidD_GetAttributes(h, &attrs)) {
                    if (attrs.VendorID == SONY_VID &&
                        (attrs.ProductID == DUALSENSE_PID || attrs.ProductID == DUALSENSE_EDGE_PID)) {
                        free(detail);
                        SetupDiDestroyDeviceInfoList(devInfo);

                        PHIDP_PREPARSED_DATA preparsed = nullptr;
                        if (HidD_GetPreparsedData(h, &preparsed)) {
                            HIDP_CAPS caps = {};
                            HidP_GetCaps(preparsed, &caps);
                            s_dsViaBluetooth = (caps.InputReportByteLength > 64);
                            HidD_FreePreparsedData(preparsed);
                        }

                        return h;
                    }
                }
                CloseHandle(h);
            }
        }
        free(detail);
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return INVALID_HANDLE_VALUE;
}

// DualSense USB report layout (report ID 0x01, 64 bytes):
// Byte 0: Report ID (0x01)
// Byte 1: Left stick X
// Byte 2: Left stick Y
// Byte 3: Right stick X
// Byte 4: Right stick Y
// Byte 5: L2 trigger
// Byte 6: R2 trigger
// Byte 7: Counter
// Byte 8: Buttons byte 0 - dpad(low nibble) + square/cross/circle/triangle(high nibble)
//          bit4=square, bit5=cross, bit6=circle, bit7=triangle
// Byte 9: Buttons byte 1 - L1/R1/L2btn/R2btn/create/options/L3/R3
//          bit0=L1, bit1=R1, bit2=L2, bit3=R2, bit4=create, bit5=options, bit6=L3, bit7=R3
// Byte 10: Buttons byte 2 - PS/touchpad/mute
static State ParseDualSense() {
    State s = {};
    if (s_dsHandle == INVALID_HANDLE_VALUE) return s;

    BYTE buf[64] = {};
    DWORD bytesRead = 0;

    // Try ReadFile (non-blocking with zero timeout via overlapped)
    OVERLAPPED ov = {};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    
    BOOL readResult = ReadFile(s_dsHandle, buf, 64, &bytesRead, &ov);
    if (!readResult && GetLastError() == ERROR_IO_PENDING) {
        // Wait briefly (1ms)
        if (WaitForSingleObject(ov.hEvent, 1) == WAIT_OBJECT_0) {
            GetOverlappedResult(s_dsHandle, &ov, &bytesRead, FALSE);
            readResult = TRUE;
        } else {
            CancelIo(s_dsHandle);
            CloseHandle(ov.hEvent);
            return s;
        }
    }
    CloseHandle(ov.hEvent);

    if (!readResult || bytesRead < 10) {
        // Try reopening
        CloseHandle(s_dsHandle);
        s_dsHandle = INVALID_HANDLE_VALUE;
        return s;
    }

    s.connected = true;

    int btnOffset = s_dsViaBluetooth ? 9 : 8;

    BYTE btn0 = buf[btnOffset];
    BYTE btn1 = buf[btnOffset + 1];

    s.a = (btn0 & 0x20) != 0;      // Cross (bit 5)
    s.y = (btn0 & 0x80) != 0;      // Triangle (bit 7)
    s.lb = (btn1 & 0x01) != 0;     // L1 (bit 0)
    s.rb = (btn1 & 0x02) != 0;     // R1 (bit 1)
    s.r3 = (btn1 & 0x80) != 0;     // R3 (bit 7)

    return s;
}

void Init() {
    s_dsHandle = OpenDualSense();
}

State Poll() {
    // Try XInput first
    XINPUT_STATE xstate = {};
    if (XInputGetState(0, &xstate) == ERROR_SUCCESS) {
        State s = {};
        s.connected = true;
        s.lb = (xstate.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
        s.rb = (xstate.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
        s.y = (xstate.Gamepad.wButtons & XINPUT_GAMEPAD_Y) != 0;
        s.a = (xstate.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0;
        s.r3 = (xstate.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0;
        return s;
    }

    // Fallback: try DualSense HID
    if (s_dsHandle == INVALID_HANDLE_VALUE) {
        // Try to find it again (might have been connected after init)
        s_dsHandle = OpenDualSense();
    }

    if (s_dsHandle != INVALID_HANDLE_VALUE) {
        return ParseDualSense();
    }

    return {};
}

void Shutdown() {
    if (s_dsHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(s_dsHandle);
        s_dsHandle = INVALID_HANDLE_VALUE;
    }
}

} // namespace Gamepad
