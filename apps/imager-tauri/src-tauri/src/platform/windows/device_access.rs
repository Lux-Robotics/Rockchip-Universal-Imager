use crate::platform::device_access::{Kind, Status};

/// Stub until libwdi FFI / helper EXE lands (M5).
pub fn query() -> Status {
    Status {
        kind: Kind::WindowsDriver,
        device_relevant: false,
        ready: false,
        detail: String::new(),
        error: "Windows driver install not yet ported".into(),
    }
}
