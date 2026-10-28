#!/usr/bin/env bash
# Build the in-tree kernel modules after build-kernel.sh.
#
# The C44 reference used a mixed vmlinux.symvers containing the target kernel's
# exports plus six authenticated mtk_ccuv lines from the external mtkcam tree.
# Those six lines are tracked in configs/mixed-vmlinux.symvers.extra. They are
# build metadata only; no mtkcam source or binary is imported.
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(git -C "$script_dir" rev-parse --show-toplevel)
kernel_src=${KERNEL_SRC:-$repo_root}
out=${OUT:-$kernel_src/out/c48}
jobs=${JOBS:-$(nproc)}
toolchain_dir=${TOOLCHAIN_DIR:-}
reference_dir=${REFERENCE_DIR:-}
extra=${EXTRA_SYMVERS:-$script_dir/configs/mixed-vmlinux.symvers.extra}

if [[ -n $toolchain_dir ]]; then
	export PATH="$toolchain_dir/bin:$PATH"
fi
for tool in clang ld.lld llvm-ar llvm-nm llvm-objcopy pahole; do
	command -v "$tool" >/dev/null 2>&1 || {
		printf 'missing required tool: %s\n' "$tool" >&2
		exit 2
	}
done
if [[ ! -f $out/vmlinux.symvers || ! -f $out/System.map ]]; then
	printf 'missing %s/vmlinux.symvers or System.map; run build-kernel.sh first\n' "$out" >&2
	exit 2
fi
if [[ ! -f $extra ]]; then
	printf 'missing mixed symvers extra input: %s\n' "$extra" >&2
	exit 2
fi

export KBUILD_BUILD_USER=${KBUILD_BUILD_USER:-lieppos}
export KBUILD_BUILD_HOST=${KBUILD_BUILD_HOST:-lieppos-kernel}
export KBUILD_BUILD_TIMESTAMP=${KBUILD_BUILD_TIMESTAMP:-'Sun Oct 25 16:47:11 UTC 2026'}
export KBUILD_BUILD_VERSION=${KBUILD_BUILD_VERSION:-1}
export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-1792946831}
export ARCH=arm64
export LLVM=1
export LLVM_IAS=1
export MTK_PLATFORM=mt6878

cert_pem="$kernel_src/certs/mtk_signing_key.pem"
created_cert=0
if [[ ! -e $cert_pem ]]; then
	if ! command -v openssl >/dev/null 2>&1; then
		printf 'openssl is required to convert the public DER certificate\n' >&2
		exit 2
	fi
	openssl x509 -in "$kernel_src/certs/mtk_signing_key.x509" -inform DER \
		-out "$cert_pem"
	created_cert=1
fi
cleanup() {
	if [[ $created_cert == 1 ]]; then
		rm -f "$cert_pem"
	fi
}
trap cleanup EXIT

if [[ -n $reference_dir ]]; then
	debug_map="-fdebug-prefix-map=$kernel_src=$reference_dir/src/common"
	debug_map+=" -fdebug-prefix-map=$out=$reference_dir/out"
	debug_map+=" -fdebug-compilation-dir=$reference_dir/out"
	export KCFLAGS="${KCFLAGS:-} $debug_map"
	export KAFLAGS="${KAFLAGS:-} $debug_map"
	export KCPPFLAGS_COMPAT="${KCPPFLAGS_COMPAT:-} $debug_map"
fi

mkdir -p "$out/mixed"
cp "$out/vmlinux.symvers" "$out/mixed/vmlinux.symvers"
cat "$extra" >> "$out/mixed/vmlinux.symvers"
cp "$out/System.map" "$out/mixed/System.map"

make_args=(-C "$kernel_src" O="$out" ARCH=arm64
	DEVICE_MODULES_PATH="$kernel_src" LLVM=1 LLVM_IAS=1 MTK_PLATFORM=mt6878
	KBUILD_MIXED_TREE="$out/mixed" -j"$jobs")

printf '== modules (mixed tree) ==\n'
make "${make_args[@]}" modules

count=$(find "$out" -name '*.ko' -type f | wc -l)
printf '\nbuilt %s in-tree .ko files\n' "$count"
printf 'note: fpsgo.ko is intentionally absent; its private wrapper is excluded from publication.\n'
sha256sum "$out/Module.symvers" "$out/modules-only.symvers"
