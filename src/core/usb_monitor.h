#pragma once

#include <functional>

namespace rui {

// Invoked from the libusb event thread whenever a matching device arrives or
// leaves. `present` is true on arrival, false on departure; vid/pid come
// straight from the USB device descriptor (no rkdeveloptool transfer).
// Note: the descriptor cannot distinguish Maskrom from a running loader on
// these parts, so mode is determined separately via rfi, not here.
using UsbChangeCallback =
    std::function<void(bool present, unsigned short vid, unsigned short pid)>;

// Starts libusb hotplug monitoring filtered to Rockchip's vendor ID
// (0x2207), replacing the old periodic `rkdeveloptool ld` poll. The callback
// also fires once for each already-connected matching device at startup
// (LIBUSB_HOTPLUG_ENUMERATE). Returns false if libusb or hotplug support is
// unavailable on this platform - the caller can then decide how to degrade.
bool start_usb_monitor(UsbChangeCallback on_change);

// Stops monitoring and tears down the libusb context. Safe to call even if
// start_usb_monitor() failed or was never called.
void stop_usb_monitor();

} // namespace rui
