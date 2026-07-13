//! Thin JS-facing API. Keep product logic out of this file as it grows;
//! prefer modules like `rkdev`, `device_access`, `usb_monitor`.

use serde::Serialize;
use tauri::AppHandle;
use tauri_plugin_dialog::DialogExt;
use tauri_plugin_opener::OpenerExt;

use crate::logging;
use crate::paths;
use crate::platform::device_access;

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DependencyStatus {
    pub ok: bool,
    pub warning: String,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DeviceAccessInfo {
    pub kind: String,
    pub device_relevant: bool,
    pub ready: bool,
    pub detail: String,
    pub error: String,
}

#[derive(Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct FilePickResult {
    pub success: bool,
    pub path: String,
    pub error: String,
    pub size_bytes: u64,
}

#[tauri::command]
pub fn get_platform() -> String {
    paths::platform_name().to_string()
}

#[tauri::command]
pub fn get_dependency_status() -> DependencyStatus {
    match paths::rkdeveloptool_path() {
        Ok(_) => DependencyStatus {
            ok: true,
            warning: String::new(),
        },
        Err(msg) => DependencyStatus {
            ok: false,
            warning: msg,
        },
    }
}

#[tauri::command]
pub fn get_log_contents() -> String {
    logging::read_all()
}

#[tauri::command]
pub fn open_log_directory(app: AppHandle) -> Result<(), String> {
    let dir = logging::log_directory();
    std::fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
    app.opener()
        .open_path(dir.to_string_lossy(), None::<&str>)
        .map_err(|e| e.to_string())
}

#[tauri::command]
pub fn ui_ready() -> bool {
    // M3+: start USB monitor + device worker here.
    tracing::info!(target: "app", "ui_ready");
    true
}

#[tauri::command]
pub fn get_device_access_info() -> DeviceAccessInfo {
    let s = device_access::query();
    DeviceAccessInfo {
        kind: s.kind.as_str().to_string(),
        device_relevant: s.device_relevant,
        ready: s.ready,
        detail: s.detail,
        error: s.error,
    }
}

/// Blocking dialog on a worker thread (dialog plugin callback API).
#[tauri::command]
pub fn select_image_file(app: AppHandle) -> FilePickResult {
    let (tx, rx) = std::sync::mpsc::channel();
    app.dialog()
        .file()
        .add_filter("Disk Images", &["img"])
        .set_title("Select .img file")
        .pick_file(move |path| {
            let _ = tx.send(path);
        });

    match rx.recv() {
        Ok(Some(file_path)) => {
            let path = match file_path.as_path() {
                Some(p) => p.to_string_lossy().into_owned(),
                None => file_path.to_string(),
            };
            let size = std::fs::metadata(&path).map(|m| m.len()).unwrap_or(0);
            FilePickResult {
                success: true,
                path,
                error: String::new(),
                size_bytes: size,
            }
        }
        Ok(None) => FilePickResult {
            success: false,
            path: String::new(),
            error: "file picker canceled".into(),
            size_bytes: 0,
        },
        Err(_) => FilePickResult {
            success: false,
            path: String::new(),
            error: "file picker failed".into(),
            size_bytes: 0,
        },
    }
}
