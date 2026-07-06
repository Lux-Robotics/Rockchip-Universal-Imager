#include "core/gpt_probe.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "core/rkdeveloptool_runner.h"

namespace hwhelper {
namespace {

constexpr std::uint64_t kGptHeaderSignature = 0x5452415020494645ULL; // "EFI PART", little-endian
constexpr std::size_t kSectorSize = 512;
constexpr std::size_t kGptProbeSectors = 34; // protective MBR + header + 32 sectors of entries

std::uint64_t read_le_u64(const std::vector<std::uint8_t>& buf, std::size_t offset) {
    std::uint64_t value = 0;
    std::memcpy(&value, buf.data() + offset, sizeof(value));
    return value;
}

std::uint32_t read_le_u32(const std::vector<std::uint8_t>& buf, std::size_t offset) {
    std::uint32_t value = 0;
    std::memcpy(&value, buf.data() + offset, sizeof(value));
    return value;
}

bool is_all_zero(const std::vector<std::uint8_t>& buf, std::size_t offset, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
        if (buf[offset + i] != 0) {
            return false;
        }
    }
    return true;
}

} // namespace

std::optional<std::vector<std::uint8_t>> read_sectors(std::uint64_t begin_sector, std::uint64_t count) {
    static std::atomic<int> probe_id{0};
    const auto temp_path = std::filesystem::temp_directory_path() /
        ("hwhelper_sector_probe_" + std::to_string(probe_id.fetch_add(1)) + ".bin");

    std::mutex wait_mutex;
    std::condition_variable wait_cv;
    bool done = false;
    int exit_code = -1;

    auto task = rkdev::start_rkdeveloptool(
        {"rl", std::to_string(begin_sector), std::to_string(count), temp_path.string()},
        [](const std::string&) {},
        [&](const rkdev::ProcessResult& result) {
            exit_code = result.exit_code;
            {
                std::lock_guard<std::mutex> lock(wait_mutex);
                done = true;
            }
            wait_cv.notify_one();
        });

    {
        std::unique_lock<std::mutex> lock(wait_mutex);
        wait_cv.wait(lock, [&]() { return done; });
    }

    if (exit_code != 0) {
        std::error_code remove_ec;
        std::filesystem::remove(temp_path, remove_ec);
        return std::nullopt;
    }

    std::ifstream file(temp_path, std::ios::binary);
    if (!file) {
        std::error_code remove_ec;
        std::filesystem::remove(temp_path, remove_ec);
        return std::nullopt;
    }
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(count * kSectorSize));
    file.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
    file.close();
    std::error_code remove_ec;
    std::filesystem::remove(temp_path, remove_ec);

    return buf;
}

std::optional<GptInfo> read_gpt_info() {
    const auto maybe_buf = read_sectors(0, kGptProbeSectors);
    if (!maybe_buf) {
        return std::nullopt;
    }
    const auto& buf = *maybe_buf;

    if (buf.size() < kSectorSize * 2) {
        return std::nullopt;
    }

    // GPT header lives at LBA 1.
    const std::size_t header_offset = kSectorSize;
    const std::uint64_t signature = read_le_u64(buf, header_offset);
    if (signature != kGptHeaderSignature) {
        return std::nullopt;
    }

    const std::uint64_t entry_lba = read_le_u64(buf, header_offset + 72);
    const std::uint32_t entry_count = read_le_u32(buf, header_offset + 80);
    const std::uint32_t entry_size = read_le_u32(buf, header_offset + 84);

    if (entry_size == 0 || entry_count == 0) {
        return std::nullopt;
    }

    const std::size_t entries_offset = static_cast<std::size_t>(entry_lba) * kSectorSize;
    std::optional<std::uint64_t> max_ending_lba;

    for (std::uint32_t i = 0; i < entry_count; ++i) {
        const std::size_t entry_offset = entries_offset + static_cast<std::size_t>(i) * entry_size;
        if (entry_offset + entry_size > buf.size()) {
            // Later entries live beyond the 34 sectors we read (a full GPT
            // reserves room for 128 entries by default); everything actually
            // in use is almost always within the first handful, so treat
            // running out of buffer as "no more entries" rather than an error.
            break;
        }
        // partition_type_guid (16 bytes) all-zero means this slot is unused.
        if (is_all_zero(buf, entry_offset, 16)) {
            continue;
        }
        const std::uint64_t ending_lba = read_le_u64(buf, entry_offset + 40);
        if (!max_ending_lba || ending_lba > *max_ending_lba) {
            max_ending_lba = ending_lba;
        }
    }

    if (!max_ending_lba) {
        return std::nullopt;
    }

    GptInfo info;
    info.last_used_lba = *max_ending_lba;
    return info;
}

bool probe_emmc_appears_blank(std::uint64_t total_sectors) {
    // Sample the GPT region plus a few points spread across the disk. A
    // freshly-erased/never-flashed eMMC reads back as one uniform byte
    // (0xFF or 0x00 depending on controller) everywhere; requiring the same
    // byte across widely-separated samples - not just uniform within each -
    // rules out the contrived case of only the GPT region having been wiped
    // while real data remains elsewhere.
    std::vector<std::pair<std::uint64_t, std::uint64_t>> samples = {
        {0, kGptProbeSectors},
    };
    if (total_sectors > kGptProbeSectors) {
        for (double frac : {0.1, 0.5, 0.9}) {
            const auto sector = static_cast<std::uint64_t>(static_cast<double>(total_sectors) * frac);
            if (sector > kGptProbeSectors) {
                samples.push_back({sector, 1});
            }
        }
    }

    std::optional<std::uint8_t> uniform_byte;
    for (const auto& [begin, count] : samples) {
        const auto maybe_buf = read_sectors(begin, count);
        if (!maybe_buf || maybe_buf->empty()) {
            return false; // couldn't read; don't guess
        }
        for (const auto b : *maybe_buf) {
            if (!uniform_byte) {
                uniform_byte = b;
            } else if (b != *uniform_byte) {
                return false;
            }
        }
    }

    return true;
}

} // namespace hwhelper
