#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Offline GQ5012BF1 camera build. Preserve an explicitly selected working config;
# never fall back to the frozen C44 donor selection or replace a stale output.
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(git -C "$script_dir" rev-parse --show-toplevel)
export KERNEL_SRC=${KERNEL_SRC:-$repo_root}
: "${BASE_CONFIG:?set BASE_CONFIG to the verified working kernel config}"
: "${OUT:?set OUT to a task-owned output directory}"

fragment="$KERNEL_SRC/configs/gq5012bf1-camera.fragment"
selection=$(grep '^CONFIG_CUSTOM_KERNEL_IMGSENSOR=' "$fragment")
if [[ -z $selection || $selection == *$'\n'* ]]; then
	printf 'invalid camera profile: expected exactly one sensor-selection line\n' >&2
	exit 2
fi
candidate=$(mktemp)
trap 'rm -f "$candidate"' EXIT
awk -v selection="$selection" '
	/^CONFIG_CUSTOM_KERNEL_IMGSENSOR=/ { print selection; count++; next }
	{ print }
	END { if (count != 1) exit 2 }
' "$BASE_CONFIG" > "$candidate"

if [[ -e $OUT/.config ]]; then
	if ! cmp -s "$candidate" "$OUT/.config"; then
		printf 'output config differs from selected baseline + camera profile; use a fresh OUT\n' >&2
		exit 2
	fi
else
	mkdir -p "$OUT"
	cp "$candidate" "$OUT/.config"
fi
printf 'GQ5012BF1 camera profile: %s\n' "$selection"
if [[ ${PREPARE_ONLY:-0} == 1 ]]; then
	exit 0
fi

# Several reconstructed sensors require stock-derived register/mode headers.
# Keep those publication-held inputs outside the source checkout and Git.
: "${CAMERA_DATA_DIR:?set CAMERA_DATA_DIR to the authenticated stock-data headers}"
if [[ $CAMERA_DATA_DIR == *[[:space:]]* ]]; then
	printf 'CAMERA_DATA_DIR must not contain whitespace (Kbuild include flags)\n' >&2
	exit 2
fi
for name in imx989 imx989cts s5kgm2sp s5kgn1sp ov32b40 s5kjn1 s5kjn1cts s5kjn1main2 ov64b; do
	if [[ ! -s $CAMERA_DATA_DIR/${name}_stock_data.h ]]; then
		printf 'missing private camera input: %s_stock_data.h\n' "$name" >&2
		exit 2
	fi
done
python3 "$script_dir/prepare-camera-data.py" --source "$CAMERA_DATA_DIR" --output "$OUT/camera-data"
export CONFIG="$OUT/.config"
"$script_dir/build-kernel.sh"
"$script_dir/build-modules.sh"
# Only external camera builds need this private include directory.
export KCFLAGS="${KCFLAGS:-} -I$OUT/camera-data"
# Real provider outputs are required by imgsensor's external modpost step.
export ONLY=cam-cal-v4l2,mtk-dpe,ccusys,camsys-rpmsg,camsys-remoteproc,imgsensor
exec python3 "$script_dir/build-vendor-modules.py"
