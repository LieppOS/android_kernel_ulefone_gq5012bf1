#!/usr/bin/env bash
# Build the selected ISP7SP camera module against a prepared LieppOS target.
#
# The provider order is explicit and intentionally limited to the GQ5012BF1
# source family: ccuv, imgsensor, DPE, camsys, then the aggregate camsys
# module.  Provider Module.symvers are collected only from external source
# directories; kernel exports remain the target output's own Module.symvers.
set -euo pipefail

: "${KERNEL_SRC:?set KERNEL_SRC to the kernel source tree}"
: "${OUT_DIR:?set OUT_DIR to the prepared kernel output directory}"
: "${TOOLCHAIN_BIN:?set TOOLCHAIN_BIN to the directory containing clang and ld.lld}"

CAM_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MTKCAM_ROOT="$(cd "$CAM_ROOT/.." && pwd)"
M_REL="$(realpath --relative-to="$KERNEL_SRC" "$CAM_ROOT")"
MTKCAM_OUT="$(realpath -m "$OUT_DIR/../mtkcam")"
MTK_PLATFORM="${MTK_PLATFORM:-mt6878}"
JOBS="${JOBS:-4}"
HOSTCC="${HOSTCC:-gcc}"
MODULE="mtk-cam-isp7sp"
MOD_DIR="$MTKCAM_OUT/camsys/isp7sp/cam"
MOD_FILE="$MOD_DIR/$MODULE.ko"

die() {
	echo "isp7sp: $*" >&2
	exit 1
}

export PATH="$TOOLCHAIN_BIN:$PATH"

[ -x "$TOOLCHAIN_BIN/clang" ] || die "missing $TOOLCHAIN_BIN/clang"
[ -f "$OUT_DIR/.config" ] || die "missing $OUT_DIR/.config"
[ -f "$OUT_DIR/include/config/auto.conf" ] || die "missing auto.conf"
[ -f "$OUT_DIR/include/generated/autoconf.h" ] || die "missing autoconf.h"
[ -f "$OUT_DIR/Module.symvers" ] || die "missing target Module.symvers"
grep -qx 'CONFIG_CC_IS_CLANG=y' "$OUT_DIR/.config" ||
	die "target .config is not a clang configuration"
grep -qx 'CONFIG_CC_IS_CLANG=y' "$OUT_DIR/include/config/auto.conf" ||
	die "generated config is not a clang configuration"
grep -qx 'CONFIG_VIDEO_MTK_ISP_CAMSYS=m' "$OUT_DIR/.config" ||
	die "target does not select external ISP camsys"
grep -qx 'CONFIG_MTK_CAMERA_CAMSYS_7SP=y' "$OUT_DIR/.config" ||
	die "target does not select ISP7SP"

# The aggregate module consumes exports from these source-owned providers.
provider_files=(
	"$MTKCAM_OUT/ccusys/Module.symvers"
	"$MTKCAM_OUT/camsys/Module.symvers"
	"$MTKCAM_OUT/camsys/rpmsg/Module.symvers"
	"$MTKCAM_OUT/camsys/remoteproc/Module.symvers"
	"$MTKCAM_OUT/imgsensor/src-v4l2/Module.symvers"
	"$MTKCAM_OUT/mtk-dpe/Module.symvers"
)

for f in "${provider_files[@]}"; do
	[ -f "$f" ] || die "missing provider symvers $f (build providers first)"
done

tmp="$(mktemp "${TMPDIR:-/tmp}/isp7sp-symvers.XXXXXX")"
trap 'rm -f "$tmp" "$tmp.uniq"' EXIT

: > "$tmp"
for f in "${provider_files[@]}"; do
	# Keep only external source providers.  Kernel exports are already in the
	# target's Module.symvers; importing kernel lines here duplicates exports.
	# Exclude the aggregate module itself and the disabled ISP7S/ISP7.1
	# families so a stale provider output cannot leak into this build.
	awk -F '\t' 'NF >= 4 && $3 ~ /^\.\.\/mtkcam\// &&
		$3 !~ /mtk-cam-isp7sp/ && $3 !~ /\/isp7s\// &&
		$3 !~ /\/isp7_1\// { print }' "$f" >> "$tmp"
done
awk -F '\t' '!seen[$2]++' "$tmp" > "$tmp.uniq"
# The target Module.symvers may already contain an external provider (for
# example mtk_ccuv, needed by in-tree consumers).  Do not pass that provider a
# second time; modpost treats the duplicate export as an error.
awk -F '\t' 'NR == FNR { have[$2] = 1; next } !have[$2]' \
	"$OUT_DIR/Module.symvers" "$tmp.uniq" > "$tmp.filtered"
[ -s "$tmp.filtered" ] || die "provider symvers set is empty"
mv "$tmp.filtered" "$tmp.uniq"

# This module was previously built with a one-line aggregate .mod file even
# though its .mod.cmd contained the full object list.  modpost then looked only
# at the aggregate link object and emitted zero CRCs for the five AOV exports.
# Detect that exact stale state and force only this module's metadata to be
# regenerated; no unrelated generated output is removed.
mod_cmd="$MOD_DIR/.$MODULE.mod.cmd"
mod_list="$MOD_DIR/$MODULE.mod"
if [ -f "$mod_list" ] && [ ! -f "$mod_cmd" ]; then
	echo "isp7sp: regenerating $MODULE metadata without a command record"
	rm -f "$mod_list" "$MOD_DIR/$MODULE.mod.c"
elif [ -f "$mod_cmd" ] && [ -f "$mod_list" ]; then
	expected="$(grep -oE '[A-Za-z0-9_./-]+\.o' "$mod_cmd" | wc -l)"
	actual="$(grep -cE '\.o$' "$mod_list" || true)"
	if [ "$expected" -gt 0 ] && [ "$actual" -lt "$expected" ]; then
		echo "isp7sp: regenerating stale $MODULE metadata ($actual < $expected objects)"
		rm -f "$mod_list" "$mod_cmd" "$MOD_DIR/$MODULE.mod.c"
	fi
fi

echo "isp7sp: KERNEL_SRC=$KERNEL_SRC"
echo "isp7sp: OUT_DIR=$OUT_DIR"
echo "isp7sp: M=$M_REL"
echo "isp7sp: provider symbols=$(wc -l < "$tmp.uniq")"
echo "isp7sp: release=$(cat "$OUT_DIR/include/config/kernel.release")"

make -C "$KERNEL_SRC" O="$OUT_DIR" ARCH=arm64 LLVM=1 LLVM_IAS=1 \
	HOSTCC="$HOSTCC" DEVICE_MODULES_PATH="$KERNEL_SRC" \
	MTK_PLATFORM="$MTK_PLATFORM" -j"$JOBS" \
	M="$M_REL" KBUILD_EXTRA_SYMBOLS="$tmp.uniq" \
	isp7sp/cam/$MODULE.ko

[ -f "$MOD_FILE" ] || die "module was not produced"
[ -f "$MOD_DIR/$MODULE.mod.c" ] || die "module metadata was not produced"

# The five AOV/seninf exports must all have real, source-generated CRCs.
for symbol in mtk_cam_seninf_s_aov_param \
	mtk_cam_seninf_aov_runtime_suspend \
	mtk_cam_seninf_aov_runtime_resume \
	mtk_cam_seninf_aov_reset_sensor \
	mtk_cam_seninf_aov_sensor_set_mclk; do
	grep -q "SYMBOL_CRC($symbol, 0x00000000," "$MOD_DIR/$MODULE.mod.c" &&
		die "$symbol still has a zero CRC"
done

release="$(cat "$OUT_DIR/include/config/kernel.release")"
vermagic="$(modinfo -F vermagic "$MOD_FILE")"
case "$vermagic" in
"$release"*) ;;
*) die "$MOD_FILE has vermagic '$vermagic', expected '$release'" ;;
esac

echo "isp7sp: $(sha256sum "$MOD_FILE")"
