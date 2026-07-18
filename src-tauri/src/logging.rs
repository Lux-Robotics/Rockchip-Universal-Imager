use std::fs::{self, OpenOptions};
use std::io::{Seek, SeekFrom, Write};
use std::path::PathBuf;
use std::sync::{Mutex, OnceLock};

use crate::paths;

struct LogState {
    path: PathBuf,
    last_was_progress: bool,
    file_end: u64,
    replace_offset: Option<u64>,
}

static LOG: Mutex<Option<LogState>> = Mutex::new(None);

/// Optional sink for live-log panel (set from lib once AppHandle is available).
static UI_SINK: OnceLock<Box<dyn Fn(String, bool) + Send + Sync>> = OnceLock::new();

pub fn set_ui_sink<F>(f: F)
where
    F: Fn(String, bool) + Send + Sync + 'static,
{
    let _ = UI_SINK.set(Box::new(f));
}

pub fn init() {
    let _ = tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| tracing_subscriber::EnvFilter::new("info")),
        )
        .with_target(true)
        .try_init();

    let path = next_log_path();
    if let Ok(mut guard) = LOG.lock() {
        *guard = Some(LogState {
            path,
            last_was_progress: false,
            file_end: 0,
            replace_offset: None,
        });
    }
    write_line(&format!(
        "[app] launched (portable={})",
        paths::is_portable_build()
    ));
}

/// Log directory is always under the OS user/system log location (portable and
/// installed builds alike). Portable only changes where the *binaries* live.
pub fn log_directory() -> PathBuf {
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
    // Last resort (unusual env without HOME / LOCALAPPDATA)
    paths::executable_dir().join("logs")
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

fn append_raw(line: &str, progress: bool) {
    tracing::info!("{line}");
    let replace = {
        let Ok(mut guard) = LOG.lock() else {
            return;
        };
        let Some(state) = guard.as_mut() else {
            return;
        };
        let Ok(mut f) = OpenOptions::new()
            .create(true)
            .append(true)
            .read(true)
            .open(&state.path)
        else {
            return;
        };
        let stamped = format!("{line}\n");
        let replace = progress && state.last_was_progress;
        if replace {
            if let Some(off) = state.replace_offset {
                let _ = f.set_len(off);
                let _ = f.seek(SeekFrom::Start(off));
                state.file_end = off;
            }
        }
        let write_at = state.file_end;
        if f.seek(SeekFrom::Start(write_at)).is_ok() && f.write_all(stamped.as_bytes()).is_ok() {
            if progress {
                state.replace_offset = Some(write_at);
            } else {
                state.replace_offset = None;
            }
            state.file_end = write_at + stamped.len() as u64;
            state.last_was_progress = progress;
        }
        replace
    };
    if let Some(sink) = UI_SINK.get() {
        sink(line.to_string(), replace);
    }
}

pub fn write_line(line: &str) {
    append_raw(line, false);
}

pub fn write_progress(category: &str, message: &str) {
    append_raw(&format!("[{category}] {message}"), true);
}

pub fn read_all() -> String {
    let path = LOG.lock().ok().and_then(|g| g.as_ref().map(|s| s.path.clone()));
    path.and_then(|p| fs::read_to_string(p).ok())
        .unwrap_or_default()
}
