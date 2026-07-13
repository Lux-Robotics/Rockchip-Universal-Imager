//! Resolve app / companion / resource directories.
//!
//! Portable layout (v1 priority): a zip containing
//!   rockchip-universal-imager[.exe]
//!   rkdeveloptool[.exe]
//!   loader_binaries/   (optional)
//!   portable           (empty marker → logs next to the zip extract)
//!
//! Installer layouts can come later; companion_dir still prefers "next to exe".

use std::path::{Path, PathBuf};

pub fn platform_name() -> &'static str {
    if cfg!(target_os = "windows") {
        "windows"
    } else if cfg!(target_os = "macos") {
        "macos"
    } else {
        "linux"
    }
}

pub fn executable_dir() -> PathBuf {
    std::env::current_exe()
        .ok()
        .and_then(|p| p.parent().map(|d| d.to_path_buf()))
        .unwrap_or_else(|| PathBuf::from("."))
}

/// Directory for companions (rkdeveloptool, portable marker, optional libusb).
/// On a macOS .app this is the folder containing the .app; otherwise same as
/// executable_dir.
pub fn companion_dir() -> PathBuf {
    let exe_dir = executable_dir();
    // .../Foo.app/Contents/MacOS → folder containing Foo.app
    if cfg!(target_os = "macos") {
        if let (Some(contents), Some(macos)) = (
            exe_dir.parent(),
            exe_dir.file_name().and_then(|s| s.to_str()),
        ) {
            if macos == "MacOS" && contents.file_name().and_then(|s| s.to_str()) == Some("Contents")
            {
                if let Some(bundle) = contents.parent() {
                    if bundle.extension().and_then(|s| s.to_str()) == Some("app") {
                        if let Some(parent) = bundle.parent() {
                            return parent.to_path_buf();
                        }
                    }
                }
            }
        }
    }
    exe_dir
}

pub fn is_portable_build() -> bool {
    companion_dir().join("portable").is_file()
}

pub fn rkdeveloptool_name() -> &'static str {
    if cfg!(target_os = "windows") {
        "rkdeveloptool.exe"
    } else {
        "rkdeveloptool"
    }
}

pub fn rkdeveloptool_path() -> Result<PathBuf, String> {
    let name = rkdeveloptool_name();
    let candidates = [companion_dir().join(name), executable_dir().join(name)];
    for path in candidates {
        if path.is_file() {
            return Ok(path);
        }
    }
    Err(format!(
        "rkdeveloptool is missing - place {} next to the app (portable zip layout).",
        name
    ))
}

#[allow(dead_code)]
pub fn resource_dir() -> PathBuf {
    // Prefer Contents/Resources on macOS bundles; else next to executable.
    if cfg!(target_os = "macos") {
        let resources = executable_dir().parent().map(|p| p.join("Resources"));
        if let Some(ref r) = resources {
            if r.is_dir() {
                return r.clone();
            }
        }
    }
    executable_dir()
}

#[allow(dead_code)]
pub fn exists_beside(name: impl AsRef<Path>) -> bool {
    companion_dir().join(name).is_file()
}
