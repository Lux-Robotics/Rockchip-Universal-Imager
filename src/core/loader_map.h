#pragma once

#include <cstddef>

struct LoaderMapEntry {
    unsigned short vid;
    unsigned short pid;
    const char* soc;       // SoC/chip name, e.g. "RK3588"
    const char* filename;  // bundled SPL loader, or nullptr if none shipped yet
};

// Rockchip parts all enumerate under VID 0x2207 in Maskrom mode; the PID
// identifies the SoC. The PID<->SoC mapping is taken from xboot/xrock's chip
// table (https://github.com/xboot/xrock), cross-checked against Rockchip's
// Rockusb documentation (0x350a=RK3568, 0x350b=RK3588).
//
// Entries with filename == nullptr are recognized SoCs we don't yet bundle a
// loader for: the device will be identified by name in the UI, but flashing
// it needs a loader. To enable one, drop the matching *_spl_loader_*.bin into
// loader_binaries/ and set filename here.
constexpr LoaderMapEntry kLoaderMap[] = {
    {0x2207, 0x110c, "RV1106", nullptr},
    {0x2207, 0x180a, "RK1808", nullptr},
    {0x2207, 0x281a, "RK2818", nullptr},
    {0x2207, 0x290a, "RK2918", nullptr},
    {0x2207, 0x292a, "RK2928", nullptr},
    {0x2207, 0x292c, "RK3026", nullptr},
    {0x2207, 0x300a, "RK3066", nullptr},
    {0x2207, 0x300b, "RK3168", nullptr},
    {0x2207, 0x301a, "RK3036", nullptr},
    {0x2207, 0x310a, "RK3066", nullptr},
    {0x2207, 0x310b, "RK3188", nullptr},
    {0x2207, 0x310c, "RK3128", nullptr},
    {0x2207, 0x320a, "RK3288", nullptr},
    {0x2207, 0x320b, "RK3228", nullptr},
    {0x2207, 0x320c, "RK3328", nullptr},
    {0x2207, 0x330a, "RK3368", nullptr},
    {0x2207, 0x330c, "RK3399", nullptr},
    {0x2207, 0x330d, "PX30",   nullptr},
    {0x2207, 0x330e, "RK3308", nullptr},
    {0x2207, 0x350a, "RK3568", nullptr},
    {0x2207, 0x350b, "RK3588", "rk3588_spl_loader_v1.15.113.bin"},
    {0x2207, 0x350d, "RK3562", nullptr},
    {0x2207, 0x350e, "RK3576", nullptr},
    {0x2207, 0x350f, "RK3506", nullptr},
};

constexpr size_t kLoaderMapSize = sizeof(kLoaderMap) / sizeof(kLoaderMap[0]);

// SoC name for a detected VID/PID, or nullptr if unrecognized. Cheap linear
// scan (the table is tiny and this is only hit on device-state changes).
inline const char* soc_name_for_vid(unsigned short vid, unsigned short pid) {
    for (size_t i = 0; i < kLoaderMapSize; ++i) {
        if (kLoaderMap[i].vid == vid && kLoaderMap[i].pid == pid) {
            return kLoaderMap[i].soc;
        }
    }
    return nullptr;
}
