use crate::platform::device_access::{Kind, Status};

/// Stub until udev/pkexec port lands (M5).
pub fn query() -> Status {
    let path = "/etc/udev/rules.d/99-rockchip-universal-imager-rockchip.rules";
    let installed = std::path::Path::new(path).is_file();
    Status {
        kind: Kind::LinuxUdev,
        device_relevant: true,
        ready: installed,
        detail: if installed {
            "installed".into()
        } else {
            String::new()
        },
        error: if installed {
            String::new()
        } else {
            "udev rules: not installed — flashing may need root".into()
        },
    }
}
