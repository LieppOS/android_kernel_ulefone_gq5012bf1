#!/usr/bin/env bash
# Reproducible GKI kernel Image build for Ulefone GQ5012BF1 / MediaTek MT6878T.
#
# Required inputs:
#   - a clean checkout of android_kernel_ulefone_gq5012bf1
#   - configs/c44-reference.config (tracked)
#   - an Android clang 17.0.2 / LLD 17.0.2 toolchain
#
# Optional:
#   REFERENCE_DIR=<C44 reference build root>  enables the debug-prefix maps that
#   are required for bit-identical Image/vmlinux hashes. Without it the code and
#   data are still reproducible, but DWARF paths and the GNU build-id differ.
#
# Everything is relocatable. No developer home directory is hard-coded.
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(git -C "$script_dir" rev-parse --show-toplevel)
kernel_src=${KERNEL_SRC:-$repo_root}
out=${OUT:-$kernel_src/out/c48}
jobs=${JOBS:-$(nproc)}
config=${CONFIG:-$kernel_src/configs/c44-reference.config}
toolchain_dir=${TOOLCHAIN_DIR:-}
reference_dir=${REFERENCE_DIR:-}

if [[ -n $toolchain_dir ]]; then
	export PATH="$toolchain_dir/bin:$PATH"
fi
for tool in clang ld.lld llvm-ar llvm-nm llvm-objcopy pahole; do
	if ! command -v "$tool" >/dev/null 2>&1; then
		printf 'missing required tool: %s\n' "$tool" >&2
		printf 'set TOOLCHAIN_DIR to an Android clang r487747c tree or put it on PATH\n' >&2
		exit 2
	fi
done
if [[ ! -f $config ]]; then
	printf 'missing kernel config: %s\n' "$config" >&2
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

mkdir -p "$out"
if [[ ! -f $out/.config ]]; then
	cp "$config" "$out/.config"
fi

# CONFIG_MODULE_SIG_KEY names certs/mtk_signing_key.pem. The repository
# intentionally carries only the public X.509 certificate; substitute it for
# the build and remove the temporary copy on exit. No private key exists here.
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

make_args=(-C "$kernel_src" O="$out" ARCH=arm64
	DEVICE_MODULES_PATH="$kernel_src" LLVM=1 LLVM_IAS=1 MTK_PLATFORM=mt6878
	-j"$jobs")

printf '== olddefconfig ==\n'
make "${make_args[@]}" olddefconfig
printf '== Image ==\n'
make "${make_args[@]}" Image

printf '\n== kernel artifacts ==\n'
for f in arch/arm64/boot/Image vmlinux System.map Module.symvers modules-only.symvers; do
	if [[ -f $out/$f ]]; then
		sha256sum "$out/$f"
	fi
done
printf 'note: Module.symvers/modules-only.symvers are produced by build-modules.sh\n'
