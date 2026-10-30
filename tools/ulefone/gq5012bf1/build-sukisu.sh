#!/usr/bin/env bash
# Build the canonical SukiSU-enabled kernel from this workspace.
# The toolchain and output directory default to the canonical LieppOS paths.
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(git -C "$script_dir" rev-parse --show-toplevel)
canonical_root=$(CDPATH= cd -- "$repo_root/../../../.." && pwd)

export KERNEL_SRC=${KERNEL_SRC:-$repo_root}
export OUT=${OUT:-$canonical_root/build/sukisu/out}
export TOOLCHAIN_DIR=${TOOLCHAIN_DIR:-$canonical_root/artifacts/toolchains/clang-r487747c}

exec "$repo_root/tools/ulefone/gq5012bf1/build-kernel.sh" "$@"
