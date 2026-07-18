//! Cross-platform USB presence API for Rockchip devices (VID 0x2207).
//!
//! - **macOS / Linux:** libusb hotplug (`platform/*/usb` → `libusb_hotplug`).
//! - **Windows:** native device notifications (`platform/windows/usb`); does not
//!   assume a libusb driver is already installed.

#[cfg(target_os = "linux")]
pub use crate::platform::linux::usb::{start, stop};

#[cfg(target_os = "macos")]
pub use crate::platform::macos::usb::{start, stop};

#[cfg(windows)]
pub use crate::platform::windows::usb::{start, stop};

// Re-exported for platform callers / future UI; not always referenced in-tree.
#[allow(unused_imports)]
#[cfg(target_os = "linux")]
pub use crate::platform::linux::usb::UsbCallback;
#[allow(unused_imports)]
#[cfg(target_os = "macos")]
pub use crate::platform::macos::usb::UsbCallback;
#[allow(unused_imports)]
#[cfg(windows)]
pub use crate::platform::windows::usb::UsbCallback;

#[cfg(not(any(target_os = "windows", target_os = "linux", target_os = "macos")))]
compile_error!("USB monitoring is only implemented for windows, linux, and macos");
