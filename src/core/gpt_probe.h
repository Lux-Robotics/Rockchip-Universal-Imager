#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace hwhelper {

// Blocking read of `count` sectors starting at `begin_sector` off the
// connected device via rkdeveloptool. Returns nullopt if the read fails.
std::optional<std::vector<std::uint8_t>> read_sectors(std::uint64_t begin_sector, std::uint64_t count);

struct GptInfo {
    // Highest ending_lba across all defined partitions - i.e. the last
    // sector actually used by the flashed OS. Used to trim an eMMC backup to
    // just what's needed to restore the image, rather than dumping the
    // entire physical eMMC.
    //
    // Deliberately doesn't include the secondary/backup GPT mirrored at the
    // very end of the physical disk: writing that at its true offset would
    // extend the backup file's apparent length to the full disk size (and
    // on filesystems without sparse-file support, actually consume that much
    // real space), defeating the point of trimming. The trade-off is that
    // disk-repair tools (gdisk/parted) may warn about a missing backup GPT
    // if the restored image is inspected directly - harmless; nothing reads
    // it during normal boot/operation.
    std::uint64_t last_used_lba = 0;
};

// Reads the GPT (protective MBR + header + partition entries, LBA 0-33) off
// the connected device via rkdeveloptool. Returns nullopt if no valid GPT is
// found (e.g. device not ready, or a non-GPT layout).
std::optional<GptInfo> read_gpt_info();

// Best-effort check for "this eMMC has never been flashed / was just
// erased": samples the GPT region plus a few points spread across the disk
// and reports whether they're all a single uniform byte (the erased-flash
// readback pattern). Only meaningful to call after read_gpt_info() finds no
// valid GPT - a device with real data always has a non-uniform GPT region.
bool probe_emmc_appears_blank(std::uint64_t total_sectors);

} // namespace hwhelper
