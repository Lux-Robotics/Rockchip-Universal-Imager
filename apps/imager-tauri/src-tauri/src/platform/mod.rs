//! OS-specific bodies. Prefer `unix` vs `windows`; split `linux`/`macos`
//! only when implementations diverge (e.g. device access).

pub mod device_access;

#[cfg(windows)]
pub mod windows;

#[cfg(unix)]
pub mod unix;

#[cfg(target_os = "linux")]
pub mod linux;

#[cfg(target_os = "macos")]
pub mod macos;
