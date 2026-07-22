#!/usr/bin/env bash
#
# Build every SPL/miniloader .bin needed by src-tauri/src/loader_map.rs from
# official Rockchip sources (https://github.com/rockchip-linux/rkbin).
#
#   ./build-loaders.sh [outdir]        # default outdir: ./loader_binaries
#
# Requires: git, and an x86-64 Linux host (rkbin's tools/boot_merger is a
# statically linked x86-64 ELF). On arm64 Linux, prefix it with an emulator:
#   BOOT_MERGER="qemu-x86_64 ./tools/boot_merger" ./build-loaders.sh
#
set -euo pipefail

OUT="${1:-$PWD/loader_binaries}"
WORK="${WORK:-$PWD/.rkbin}"
BOOT_MERGER="${BOOT_MERGER:-./tools/boot_merger}"

# ---------------------------------------------------------------------------
# SoCs currently in loader_map.rs, mapped to their RKBOOT recipe.
# ---------------------------------------------------------------------------
# vid:pid  soc                RKBOOT ini
INIS="
2207:110a RV1108             RV110XMINIALL.ini
2207:110b RV1126             RV1126MINIALL.ini
2207:110c RV1106             RV1106MINIALL.ini
2207:110e RV1103B            RV1103BMINIALL.ini
2207:110f RV1126B            RV1126BMINIALL.ini
2207:180a RK1808             RK1808MINIALL.ini
2207:292c RK3026             RK302AMINIALL.ini
2207:300a RK3066             RK30MINIALL.ini
2207:300b RK3168             RK30BMINIALL.ini
2207:301a RK3036             RK3036MINIALL.ini
2207:310a RK3066B            RK310BMINIALL.ini
2207:310b RK3188             RK3188MINIALL.ini
2207:310c RK3126/RK3128      RK3128MINIALL.ini
2207:310d RK3126             RK3126MINIALL.ini
2207:320a RK3288             RK3288MINIALL.ini
2207:320b RK3228/RK3229      RK322XMINIALL.ini
2207:320c RK3328             RK3328MINIALL.ini
2207:330a RK3368             RK3368MINIALL.ini
2207:330c RK3399/OP1         RK3399MINIALL.ini
2207:330d PX30               PX30MINIALL.ini
2207:330e RK3308             RK3308MINIALL.ini
2207:350a RK3566/RK3568      RK3568MINIALL.ini
2207:350b RK3588/RK3582      RK3588MINIALL.ini
2207:350c RK3528             RK3528MINIALL.ini
2207:350d RK3562             RK3562MINIALL.ini
2207:350e RK3576             RK3576MINIALL.ini
2207:350f RK3506             RK3506MINIALL.ini
"
# Not buildable from rkbin at any point in its history - no DDR/usbplug/loader
# blobs were ever committed, and RK28*.ini are empty stubs:
#   RK2818 (2207:281a), RK2918 (2207:290a), RK2928 (2207:292a)

# The RK30-era recipes (RK3026/RK3066/RK3168/RK3066B) still ship in RKBOOT/ but
# reference root-level blobs that were deleted in 2015 ("rk tools: adjust bin
# for each rk plat"). They are recoverable from git history.
LEGACY_BLOBS="
3028A_DDR3_NEW_300MHz.bin
30_LPDDR2_300MHz_DDR3_300MHz_20130517.bin
3168_LPDDR2_300MHz_DDR3_300MHz_20130517.bin
3188_LPDDR2_300MHz_DDR3_300MHz_20130830.bin
rk30usbplug.bin
rkMiniLoaderAll.bin
"

# ---------------------------------------------------------------------------

if [ -d "$WORK/.git" ]; then
  echo ">> updating $WORK"
  git -C "$WORK" fetch --quiet origin
  git -C "$WORK" reset --hard --quiet origin/master
  git -C "$WORK" clean -fdq
else
  # Full clone (not --depth 1): the legacy blobs live only in history.
  echo ">> cloning rkbin into $WORK"
  git clone --quiet https://github.com/rockchip-linux/rkbin.git "$WORK"
fi

cd "$WORK"
echo ">> rkbin at $(git rev-parse --short HEAD) ($(git log -1 --format=%ad --date=short))"

echo ">> restoring legacy RK30-era blobs from history"
for f in $LEGACY_BLOBS; do
  [ -f "$f" ] && continue
  # newest commit that touched the path; if that commit deleted it, use its parent
  c=$(git log --all --full-history -1 --format=%H -- "$f")
  if [ -z "$c" ]; then echo "   !! $f not found in history"; continue; fi
  if git cat-file -e "$c:$f" 2>/dev/null; then
    git show "$c:$f" > "$f"
  else
    git show "$c^:$f" > "$f"
  fi
done

mkdir -p "$OUT"
fail=0
printf '\n%-14s %-22s %s\n' "VID:PID" "SOC" "LOADER"
while read -r id soc ini; do
  [ -z "${id:-}" ] && continue
  out=$(grep -m1 -i '^PATH=' "RKBOOT/$ini" | tr -d '\r' | sed 's/^PATH=//')
  rm -f "$out"
  if ! $BOOT_MERGER "RKBOOT/$ini" >/dev/null 2>&1 || [ ! -f "$out" ]; then
    printf '%-14s %-22s FAILED (%s)\n' "$id" "$soc" "$ini"
    fail=1
    continue
  fi
  mv -f "$out" "$OUT/$(basename "$out")"
  printf '%-14s %-22s %s\n' "$id" "$soc" "$(basename "$out")"
done <<< "$INIS"

echo
echo ">> $(ls -1 "$OUT" | wc -l) loaders in $OUT"
exit $fail
