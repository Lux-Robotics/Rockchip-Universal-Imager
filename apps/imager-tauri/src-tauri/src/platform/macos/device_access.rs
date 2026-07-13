use crate::platform::device_access::{Kind, Status};

pub fn query() -> Status {
    Status {
        kind: Kind::None,
        device_relevant: true,
        ready: true,
        detail: String::new(),
        error: String::new(),
    }
}
