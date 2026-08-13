#pragma once
#include <cstdint>

// Single source of truth for the USB vendor IDs an NXP i.MX board can show up
// under. Used to build the udev rule (mainwindow.cpp), the uuu-helper
// permission probe (helper/uuuhelper.cpp) and the "is this device flashable"
// filter (devicemonitor.cpp) — previously each of those hardcoded its own
// copy of this list, and they drifted out of sync (a VID present in the udev
// rule but missing from the permission probe meant a permission error on
// that VID went undetected and the flash just hung instead of erroring out).
//
// If a board needs a new VID recognized, add it once here.

struct VendorId {
    uint16_t    vid;
    const char* label; // for udev-rule comments / diagnostics, not parsed
};

// Every VID libuuu's built-in config matches against, across all phases and
// board vendors: NXP/Freescale recovery mode, the Linux USB gadget and
// Google fastboot IDs boards re-enumerate as during flashing, plus Boundary
// Devices' and STMicro's own SPL/fastboot IDs and SigmaTel's i.MX28 recovery
// ID. Used for: the udev rule text, and uuu-helper's pre-flight permission
// probe (which must cover every phase a board can be denied access on, not
// just the first).
inline constexpr VendorId kFlashVendorIds[] = {
    { 0x1fc9, "NXP (SDP/SDPS)" },
    { 0x15a2, "Freescale / old NXP (SDP)" },
    { 0x0525, "Linux USB gadget (SPL/fastboot)" },
    { 0x0483, "STMicroelectronics (fastboot)" },
    { 0x18d1, "Google (default fastboot VID)" },
    { 0x3016, "Boundary Devices (SPL)" },
    { 0x066f, "SigmaTel (i.MX28 recovery)" },
};

// Subset a board is expected to enumerate as in its initial recovery mode,
// before any flash has started — used by devicemonitor.cpp to decide what
// shows up in the "Connected Devices" list. Deliberately narrower than
// kFlashVendorIds: the fastboot/gadget IDs above belong to boards mid-flash,
// which are already tracked by their widget, not devices to offer flashing.
inline constexpr uint16_t kSdpVendorIds[] = { 0x1fc9, 0x15a2 };
