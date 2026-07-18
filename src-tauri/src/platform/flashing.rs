//! Shared types + dispatch for OS-specific **flash access** setup
//! (udev / libwdi — not the flash transfer itself).
//!
//! Per-OS detect + access live together in `platform/{os}/usb.rs`.

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[allow(dead_code)]
pub enum Kind {
    /// macOS: no install step required.
    None,
    /// Windows: libusb-win32 via libwdi (Zadig-style).
    WindowsDriver,
    /// Linux: udev rules for non-root Rockchip USB access.
    LinuxUdev,
}

impl Kind {
    pub fn as_str(self) -> &'static str {
        match self {
            Kind::None => "none",
            Kind::WindowsDriver => "windows_driver",
            Kind::LinuxUdev => "linux_udev",
        }
    }
}

#[derive(Debug, Clone)]
pub struct Status {
    pub kind: Kind,
    pub device_relevant: bool,
    pub ready: bool,
    pub detail: String,
    pub error: String,
}

#[derive(Debug, Clone, Default)]
pub struct InstallOptions {
    /// Windows libwdi device description override (unused on Linux/macOS).
    #[allow(dead_code)]
    pub device_name: String,
}

#[derive(Debug, Clone)]
pub struct InstallResult {
    pub success: bool,
    pub error_message: String,
}

pub fn query() -> Status {
    #[cfg(target_os = "windows")]
    {
        return crate::platform::windows::usb::query();
    }
    #[cfg(target_os = "linux")]
    {
        return crate::platform::linux::usb::query();
    }
    #[cfg(target_os = "macos")]
    {
        return crate::platform::macos::usb::query();
    }
    #[cfg(not(any(target_os = "windows", target_os = "linux", target_os = "macos")))]
    {
        Status {
            kind: Kind::None,
            device_relevant: true,
            ready: true,
            detail: String::new(),
            error: String::new(),
        }
    }
}

pub fn install(options: &InstallOptions) -> InstallResult {
    #[cfg(target_os = "linux")]
    {
        return crate::platform::linux::usb::install(options);
    }
    #[cfg(target_os = "windows")]
    {
        return crate::platform::windows::usb::install(options);
    }
    #[cfg(target_os = "macos")]
    {
        let _ = options;
        InstallResult {
            success: false,
            error_message: "device access setup is not required on this platform".into(),
        }
    }
    #[cfg(not(any(target_os = "windows", target_os = "linux", target_os = "macos")))]
    {
        let _ = options;
        InstallResult {
            success: false,
            error_message: "unsupported platform".into(),
        }
    }
}
