#!/bin/sh
# Run only host-side logic from real production translation units. No hardware.
set -eu
: "${CC:=clang}"
[ "$#" -eq 1 ] || { echo "usage: CC=<clang> sh tools/test-offline.sh <new-output-dir>" >&2; exit 2; }
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=$1
mkdir "$OUT"
cd "$ROOT"
for name in powerhal_ioctl powerhal_policy; do
  "$CC" -std=gnu11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -pthread \
    -Itests/include -Ipublic/drivers/misc/mediatek/performance/perf_ioctl \
    "tests/${name}_test.c" -o "$OUT/$name"
  ASAN_OPTIONS=detect_leaks=1 "$OUT/$name"
done
"$CC" -std=gnu11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Itests/cpu_policy_include \
  -Ipublic/drivers/misc/mediatek/performance/fpsgo_v3/base/include \
  tests/fpsgo_cpu_policy_test.c -o "$OUT/fpsgo_cpu_policy"
ASAN_OPTIONS=detect_leaks=1 "$OUT/fpsgo_cpu_policy"
