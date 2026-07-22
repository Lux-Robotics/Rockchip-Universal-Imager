//! Rockchip VID/PID → SoC name and optional bundled SPL loader filename.

#[derive(Clone, Copy)]
pub struct LoaderMapEntry {
    pub vid: u16,
    pub pid: u16,
    pub soc: &'static str,
    pub filename: Option<&'static str>,
}

const LOADER_MAP: &[LoaderMapEntry] = &[
    LoaderMapEntry { vid: 0x2207, pid: 0x110a, soc: "RV1108", filename: None },
    LoaderMapEntry { vid: 0x2207, pid: 0x110b, soc: "RV1126", filename: None },
    LoaderMapEntry { vid: 0x2207, pid: 0x110c, soc: "RV1106", filename: None },
    LoaderMapEntry { vid: 0x2207, pid: 0x110e, soc: "RV1103B", filename: None },
    LoaderMapEntry { vid: 0x2207, pid: 0x110f, soc: "RV1126B", filename: None },
    LoaderMapEntry { vid: 0x2207, pid: 0x180a, soc: "RK1808", filename: None },
    LoaderMapEntry { vid: 0x2207, pid: 0x281a, soc: "RK2818", filename: None },
    LoaderMapEntry { vid: 0x2207, pid: 0x290a, soc: "RK2918", filename: None },
    LoaderMapEntry { vid: 0x2207, pid: 0x292a, soc: "RK2928", filename: None },
    LoaderMapEntry { vid: 0x2207, pid: 0x292c, soc: "RK3026", filename: None },
    LoaderMapEntry { vid: 0x2207, pid: 0x300a, soc: "RK3066", filename: None },
    LoaderMapEntry { vid: 0x2207, pid: 0x300b, soc: "RK3168", filename: None },
    LoaderMapEntry { vid: 0x2207, pid: 0x301a, soc: "RK3036", filename: None },
    LoaderMapEntry { vid: 0x2207, pid: 0x310a, soc: "RK3066B", filename: None },
    LoaderMapEntry { vid: 0x2207, pid: 0x310b, soc: "RK3188", filename: None },
    LoaderMapEntry { vid: 0x2207, pid: 0x310c, soc: "RK3126/RK3128", filename: None },
    LoaderMapEntry { vid: 0x2207, pid: 0x310d, soc: "RK3126", filename: None },
    LoaderMapEntry { vid: 0x2207, pid: 0x320a, soc: "RK3288", filename: None },
    LoaderMapEntry { vid: 0x2207, pid: 0x320b, soc: "RK3228/RK3229", filename: None },
    LoaderMapEntry { vid: 0x2207, pid: 0x320c, soc: "RK3328", filename: None },
    LoaderMapEntry { vid: 0x2207, pid: 0x330a, soc: "RK3368", filename: None },
    LoaderMapEntry { vid: 0x2207, pid: 0x330c, soc: "RK3399/OP1", filename: None },
    LoaderMapEntry { vid: 0x2207, pid: 0x330d, soc: "PX30", filename: None },
    LoaderMapEntry { vid: 0x2207, pid: 0x330e, soc: "RK3308", filename: None },
    LoaderMapEntry { vid: 0x2207, pid: 0x350a, soc: "RK3566/RK3568", filename: None },
    LoaderMapEntry { vid: 0x2207, pid: 0x350b, soc: "RK3588/RK3582", filename: Some("rk3588_spl_loader_v1.15.113.bin") },
    LoaderMapEntry { vid: 0x2207, pid: 0x350c, soc: "RK3528", filename: None },
    LoaderMapEntry { vid: 0x2207, pid: 0x350d, soc: "RK3562", filename: None },
    LoaderMapEntry { vid: 0x2207, pid: 0x350e, soc: "RK3576", filename: None },
    LoaderMapEntry { vid: 0x2207, pid: 0x350f, soc: "RK3506", filename: None },
];

pub fn entry_for(vid: u16, pid: u16) -> Option<&'static LoaderMapEntry> {
    LOADER_MAP.iter().find(|e| e.vid == vid && e.pid == pid)
}

pub fn soc_name(vid: u16, pid: u16) -> Option<&'static str> {
    entry_for(vid, pid).map(|e| e.soc)
}
