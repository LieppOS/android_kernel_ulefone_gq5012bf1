#!/usr/bin/env bash
# Offline checks for the canonical SukiSU kernel build. This is not a boot test.
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(git -C "$script_dir" rev-parse --show-toplevel)
canonical_root=$(CDPATH= cd -- "$repo_root/../../../.." && pwd)
out=${OUT:-$canonical_root/build/sukisu/out}

for f in arch/arm64/boot/Image vmlinux System.map Module.symvers modules-only.symvers .config; do
	[[ -s $out/$f ]] || { printf 'missing build output: %s\n' "$out/$f" >&2; exit 1; }
done

grep -qx 'CONFIG_KSU=y' "$out/.config"
grep -qx 'CONFIG_KSU_MANUAL_SU=y' "$out/.config"
grep -qx 'CONFIG_KPROBES=y' "$out/.config"
grep -qx 'CONFIG_EXT4_FS=y' "$out/.config"
grep -qx 'CONFIG_HEADERS_INSTALL=y' "$out/.config"
! grep -qx 'CONFIG_KPM=y' "$out/.config"

grep -qE ' kernelsu_init$' "$out/System.map"
grep -qE ' ksu_' "$out/System.map"
[[ $(find "$out" -name '*.ko' | wc -l) -eq 702 ]]
[[ $(find "$out" -name '*kernelsu*.ko' | wc -l) -eq 0 ]]

printf 'SukiSU offline checks passed\n'
printf 'Image: %s\n' "$(sha256sum "$out/arch/arm64/boot/Image" | cut -d' ' -f1)"
printf 'vmlinux: %s\n' "$(sha256sum "$out/vmlinux" | cut -d' ' -f1)"
printf 'System.map: %s\n' "$(sha256sum "$out/System.map" | cut -d' ' -f1)"
printf 'in-tree modules: %s\n' "$(find "$out" -name '*.ko' | wc -l)"
