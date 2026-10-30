#!/usr/bin/env bash
# Offline checks for the canonical SukiSU builtin + SUSFS kernel build.
# This is not a boot test and makes no runtime or root-hiding claim.
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(git -C "$script_dir" rev-parse --show-toplevel)
canonical_root=$(CDPATH= cd -- "$repo_root/../../../.." && pwd)
out=${OUT:-$canonical_root/build/susfs/out}

for f in arch/arm64/boot/Image vmlinux System.map Module.symvers modules-only.symvers .config; do
	[[ -s $out/$f ]] || { printf 'missing build output: %s\n' "$out/$f" >&2; exit 1; }
done

for opt in \
	CONFIG_KSU=y \
	CONFIG_KSU_FEATURE_ADBROOT=y \
	CONFIG_KSU_SUSFS=y \
	CONFIG_KSU_SUSFS_SUS_PATH=y \
	CONFIG_KSU_SUSFS_SUS_MOUNT=y \
	CONFIG_KSU_SUSFS_SUS_KSTAT=y \
	CONFIG_KSU_SUSFS_SPOOF_UNAME=y \
	CONFIG_KSU_SUSFS_ENABLE_LOG=y \
	CONFIG_KSU_SUSFS_HIDE_KSU_SUSFS_SYMBOLS=y \
	CONFIG_KSU_SUSFS_SPOOF_CMDLINE_OR_BOOTCONFIG=y \
	CONFIG_KSU_SUSFS_OPEN_REDIRECT=y \
	CONFIG_KSU_SUSFS_SUS_MAP=y; do
	grep -qx "$opt" "$out/.config" || { printf 'missing config: %s\n' "$opt" >&2; exit 1; }
done
! grep -qx 'CONFIG_KPM=y' "$out/.config"
! grep -qx 'CONFIG_KSU_MANUAL_SU=y' "$out/.config"

for sym in kernelsu_init susfs_init susfs_show_version susfs_set_priority_uid_filter susfs_set_avc_log_spoofing ksu_extension_susfs_get; do
	grep -qE " $sym$" "$out/System.map" || { printf 'missing symbol: %s\n' "$sym" >&2; exit 1; }
done
[[ $(find "$out" -name '*.ko' | wc -l) -eq 702 ]]
[[ $(find "$out" -name 'kernelsu.ko' -o -name '*susfs*.ko' | wc -l) -eq 0 ]]
! grep -qE '\b(susfs|ksu_)' "$out/Module.symvers"
grep -q 'SUSFS_VERSION "v2.3.0"' "$repo_root/include/linux/susfs.h"
grep -q 'v4.3.0-builtin-b16e899a@vendored' "$repo_root/drivers/kernelsu/Makefile"

printf 'SukiSU builtin + SUSFS offline checks passed\n'
printf 'Image: %s\n' "$(sha256sum "$out/arch/arm64/boot/Image" | cut -d' ' -f1)"
printf 'vmlinux: %s\n' "$(sha256sum "$out/vmlinux" | cut -d' ' -f1)"
printf 'System.map: %s\n' "$(sha256sum "$out/System.map" | cut -d' ' -f1)"
printf 'in-tree modules: %s\n' "$(find "$out" -name '*.ko' | wc -l)"
