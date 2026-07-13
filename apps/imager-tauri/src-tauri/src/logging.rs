use std::fs::{self, OpenOptions};
use std::io::Write;
use std::path::PathBuf;
use std::sync::Mutex;

use crate::paths;

static LOG_PATH: Mutex<Option<PathBuf>> = Mutex::new(None);

pub fn init() {
    let _ = tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| tracing_subscriber::EnvFilter::new("info")),
        )
        .with_target(true)
        .try_init();

    let path = next_log_path();
    if let Ok(mut guard) = LOG_PATH.lock() {
        *guard = Some(path);
    }
    write_line(&format!(
        "[app] launched (portable={})",
        paths::is_portable_build()
    ));
}

pub fn log_directory() -> PathBuf {
    if paths::is_portable_build() {
        return paths::companion_dir().join("logs");
    }
    if cfg!(target_os = "macos") {
        if let Some(home) = std::env::var_os("HOME") {
            return PathBuf::from(home).join("Library/Logs/RockchipUniversalImager");
        }
    } else if cfg!(target_os = "windows") {
        if let Some(local) = std::env::var_os("LOCALAPPDATA") {
            return PathBuf::from(local)
                .join("RockchipUniversalImager")
                .join("logs");
        }
    } else {
        if let Ok(xdg) = std::env::var("XDG_STATE_HOME") {
            return PathBuf::from(xdg).join("rockchip-universal-imager/logs");
        }
        if let Some(home) = std::env::var_os("HOME") {
            return PathBuf::from(home).join(".local/state/rockchip-universal-imager/logs");
        }
    }
    paths::executable_dir().join("log")
}

fn next_log_path() -> PathBuf {
    let dir = log_directory();
    let _ = fs::create_dir_all(&dir);
    let mut next = 1;
    if let Ok(entries) = fs::read_dir(&dir) {
        for entry in entries.flatten() {
            let name = entry.file_name();
            let name = name.to_string_lossy();
            if let Some(rest) = name
                .strip_prefix("log")
                .and_then(|s| s.strip_suffix(".txt"))
            {
                if let Ok(n) = rest.parse::<i32>() {
                    next = next.max(n + 1);
                }
            }
        }
    }
    dir.join(format!("log{next}.txt"))
}

pub fn write_line(line: &str) {
    tracing::info!("{line}");
    let path = LOG_PATH.lock().ok().and_then(|g| g.clone());
    if let Some(path) = path {
        if let Ok(mut f) = OpenOptions::new().create(true).append(true).open(path) {
            let _ = writeln!(f, "{line}");
        }
    }
}

pub fn read_all() -> String {
    let path = LOG_PATH.lock().ok().and_then(|g| g.clone());
    path.and_then(|p| fs::read_to_string(p).ok())
        .unwrap_or_default()
}
