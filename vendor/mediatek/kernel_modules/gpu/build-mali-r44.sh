#!/usr/bin/env bash
# Build the Mali r44p1 external module group against a prepared LieppOS target.
#
# This is a driver-specific recipe, not a separate kernel build system.  It
# deliberately uses the target output's .config, generated headers and
# Module.symvers, and refuses to run when those inputs disagree.
set -euo pipefail

: "${KERNEL_SRC:?set KERNEL_SRC to the kernel source tree}"
: "${OUT_DIR:?set OUT_DIR to the prepared kernel output directory}"
: "${TOOLCHAIN_BIN:?set TOOLCHAIN_BIN to the directory containing clang and ld.lld}"

GPU_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GPU_REL="$(realpath --relative-to="$KERNEL_SRC" "$GPU_ROOT")"
GPU_OUT="$(realpath -m "$OUT_DIR/$GPU_REL")"
MTK_PLATFORM="${MTK_PLATFORM:-mt6878}"
JOBS="${JOBS:-4}"
HOSTCC="${HOSTCC:-gcc}"

die() {
	echo "mali-r44: $*" >&2
	exit 1
}

export PATH="$TOOLCHAIN_BIN:$PATH"

[ -x "$TOOLCHAIN_BIN/clang" ] || die "missing $TOOLCHAIN_BIN/clang"
[ -f "$OUT_DIR/.config" ] || die "missing $OUT_DIR/.config"
[ -f "$OUT_DIR/include/config/auto.conf" ] || die "missing auto.conf"
[ -f "$OUT_DIR/include/generated/autoconf.h" ] || die "missing autoconf.h"
[ -f "$OUT_DIR/Module.symvers" ] || die "missing target Module.symvers"
[ -f "$OUT_DIR/include/config/kernel.release" ] || die "missing kernel.release"

# A stale compiler/KASAN mode in either the visible config or the generated
# config is what previously produced the misleading empty
# KASAN_SHADOW_SCALE_SHIFT.  Refuse mixed inputs rather than patching around it.
for f in .config include/config/auto.conf; do
	grep -qx 'CONFIG_CC_IS_CLANG=y' "$OUT_DIR/$f" ||
		die "$f is not a clang configuration"
done
grep -qx 'CONFIG_MODVERSIONS=y' "$OUT_DIR/.config" ||
	die "target does not enable MODVERSIONS"
grep -qx 'CONFIG_MODVERSIONS=y' "$OUT_DIR/include/config/auto.conf" ||
	die "generated config does not enable MODVERSIONS"

"$TOOLCHAIN_BIN/clang" --version | grep -q 'clang version' ||
	die "TOOLCHAIN_BIN/clang is not runnable"

echo "mali-r44: KERNEL_SRC=$KERNEL_SRC"
echo "mali-r44: OUT_DIR=$OUT_DIR"
echo "mali-r44: compiler=$("$TOOLCHAIN_BIN/clang" --version | head -1)"
echo "mali-r44: release=$(cat "$OUT_DIR/include/config/kernel.release")"

make -C "$KERNEL_SRC" O="$OUT_DIR" ARCH=arm64 LLVM=1 LLVM_IAS=1 \
	HOSTCC="$HOSTCC" DEVICE_MODULES_PATH="$KERNEL_SRC" \
	MTK_PLATFORM="$MTK_PLATFORM" -j"$JOBS" \
	M="$GPU_REL" BUILD_RULE=OOT \
	CONFIG_MALI_MEMORY_GROUP_MANAGER=y \
	CONFIG_MALI_PROTECTED_MEMORY_ALLOCATOR=y \
	CONFIG_DMA_SHARED_BUFFER_TEST_EXPORTER=y \
	modules

release="$(cat "$OUT_DIR/include/config/kernel.release")"
count=0
while IFS= read -r ko; do
	[ -n "$ko" ] || continue
	count=$((count + 1))
	vermagic="$(modinfo -F vermagic "$ko")"
	case "$vermagic" in
	"$release"*) ;;
	*) die "$ko has vermagic '$vermagic', expected '$release'" ;;
	esac
	echo "$(sha256sum "$ko")"
done < <(find "$GPU_OUT" -type f -name 'mali_*_mt6878_r44.ko' | sort)

[ "$count" -eq 4 ] || die "expected four Mali modules, found $count"
