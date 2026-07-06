#pragma once

#include <cstdlib>

namespace hwhelper {

// Board SKUs pair a fixed eMMC size with a fixed RAM size, so RAM can be
// inferred from the eMMC capacity reported by rkdeveloptool (there is no
// rockusb command that reports installed RAM directly).
struct EmmcRamSku {
    unsigned int emmc_gb;
    unsigned int ram_gb;
};

constexpr EmmcRamSku kEmmcRamSkus[] = {
    {32, 4},
    {64, 8},
    {128, 16},
    {256, 32},
};

constexpr size_t kEmmcRamSkuCount = sizeof(kEmmcRamSkus) / sizeof(kEmmcRamSkus[0]);

// Reported eMMC sizes are always somewhat below the nominal SKU size (flash
// overprovisioning, IDBlock/GPT overhead), so match to the closest SKU
// rather than requiring an exact hit.
inline unsigned int ram_gb_for_emmc(unsigned int emmc_gb) {
    unsigned int best_ram = 0;
    long best_diff = -1;
    for (size_t i = 0; i < kEmmcRamSkuCount; ++i) {
        const long diff = std::labs(static_cast<long>(emmc_gb) - static_cast<long>(kEmmcRamSkus[i].emmc_gb));
        if (best_diff == -1 || diff < best_diff) {
            best_diff = diff;
            best_ram = kEmmcRamSkus[i].ram_gb;
        }
    }
    return best_ram;
}

} // namespace hwhelper
