//! Unified device-access facade (Windows driver / Linux udev / macOS none).

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[allow(dead_code)] // variants used per-OS via cfg modules
pub enum Kind {
    None,
    WindowsDriver,
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

pub fn query() -> Status {
    #[cfg(target_os = "windows")]
    {
        return crate::platform::windows::device_access::query();
    }
    #[cfg(target_os = "linux")]
    {
        return crate::platform::linux::device_access::query();
    }
    #[cfg(target_os = "macos")]
    {
        return crate::platform::macos::device_access::query();
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
