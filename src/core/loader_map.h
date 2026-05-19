#pragma once

#include <cstddef>

struct LoaderMapEntry {
    unsigned short vid;
    unsigned short pid;
    const char* filename;
};

constexpr LoaderMapEntry kLoaderMap[] = {
    {0x2207, 0x350b, "rk3588_spl_loader_v1.15.113.bin"},
};

constexpr size_t kLoaderMapSize = sizeof(kLoaderMap) / sizeof(kLoaderMap[0]);
