# SukiSU Ultra kernel integration

## Status

| Item | Value |
| --- | --- |
| Integration state | Source imported, built, and offline-validated |
| Boot state | **Not boot-tested**; no phone, partition, slot, or image write was performed |
| Canonical kernel repository | `android/kernel/ulefone/gq5012bf1` |
| Active development branch | `integration/sukisu-ultra-v4.3.0` |
| Source integration commit | `1aad25d144c071cfb1cc19bbae0a458fd6ac3b5b` |
| Branch base | `integration/c56-display-repair` at `b9d284691a53a9aaeaed9e0199b52ef2a5847399` |
| Kernel release | `6.1.115-mt6878-gq5012bf1-c29+` |
| SukiSU release | `v4.3.0`, version code `41100` |
| SukiSU hook model | kprobe / kretprobe and syscall tracepoint (`main` branch), built in |

## Provenance

- Upstream repository: `https://github.com/SukiSU-Ultra/SukiSU-Ultra`
- Branch: `main`
- Pinned tag: `v4.3.0`
- Pinned commit: `e67b14e6bbbbd3e67f24df658a01d3e73a64159c`
- Upstream commit date: 2026-10-12
- Imported source path: `drivers/kernelsu/`
- Imported UAPI path: `drivers/kernelsu/include/uapi/`
- Source file count: 187 files, approximately 1.4 MiB
- License: GNU GPL v3, as shipped by upstream; source headers retain their notices

The source is present as real files. It is not a symlink to an upstream checkout,
and it does not depend on the historical laboratory directory. The upstream
`setup.sh`, CI helper scripts and `.vscode/` were deliberately not imported.
The upstream `kernel/include/uapi` symlink pointed outside the imported subtree,
so the upstream root `uapi/` files were materialized under
`drivers/kernelsu/include/uapi/` instead.

The only change to upstream build logic is the pinned, offline version block in
`drivers/kernelsu/Kbuild`: `KSU_VERSION=41100`,
`KSU_VERSION_FULL=v4.3.0-e67b14e6@vendored`. This avoids a GitHub API query and
avoids deriving a version from an absent `.git` directory. The complete local
difference from upstream `Kbuild` is recorded in
`drivers/kernelsu/Kbuild.local.diff`.

## Why this is not called a booted SukiSU kernel

The preserved local history contains no SukiSU kernel source tree and no SukiSU
kernel boot record. The local KernelSU evidence is from the stock/device side
(for example, a `kernelsu` module symbol in a captured kernel log and an
`adb-kernelsu-sanity.txt` capture that only shows root), not a buildable SukiSU
kernel tree. The verified custom-kernel reference is C46, which booted Android
17 to `sys.boot_completed=1`; that reference is a non-SukiSU kernel.

Accordingly, the SukiSU integration is **offline-validated only**. A successful
compile is not a boot test, and the earlier C46 boot must not be presented as
proof that this SukiSU image boots.

## Integration details

- `drivers/Kconfig` sources `drivers/kernelsu/Kconfig`.
- `drivers/Makefile` contains `obj-$(CONFIG_KSU) += kernelsu/`.
- `CONFIG_KSU=y` builds SukiSU into the kernel image (`kernelsu_init` is present
  in `System.map`); there is no separate `kernelsu.ko`.
- `CONFIG_KSU_MANUAL_SU=y` is enabled explicitly.
- `CONFIG_KSU_DEBUG`, `CONFIG_KPM`, `CONFIG_KSU_DISABLE_MANAGER`, and
  `CONFIG_KSU_DISABLE_POLICY` are not enabled.
- Required and verified kernel options: `CONFIG_KPROBES=y`,
  `CONFIG_KRETPROBES=y`, `CONFIG_EXT4_FS=y`, `CONFIG_HAVE_SYSCALL_TRACEPOINTS=y`,
  `CONFIG_HEADERS_INSTALL=y`, `CONFIG_MODULES=y`, `CONFIG_MODVERSIONS=y`, and
  `CONFIG_CFI_CLANG=y`.
- The SukiSU manager package expected by the kernel is `com.sukisu.ultra`. The
  manager APK is not included in this kernel repository and was not installed on
  any device.
- No separate kernel-module repository is required for SukiSU because it is
  built in. The existing MediaTek vendor-module source and recipes are unchanged.

## Device-specific baseline

The integration is based on the protected C56 display-repair branch so that its
newer display/touch work is not discarded. The hardware-booted C46 reference is
`5418aa7c1e66890a6f1c1ff2f311bd0d1397a38b` in this repository (external C46
source `2c896fab1c641ee7577ba40ccc2137b660e94c7c`). The C46/C56
`drivers/mmc/host/mtk-mmc.c` and
`drivers/gpu/drm/panel/panel-ky-vtdr6115-dphy-cmd.c` files are byte-identical;
the SukiSU branch changes no device driver source outside `drivers/kernelsu/`
and the four registration/config files. C56 and C47 remain untouched.

## Build

Run from the canonical kernel checkout:

```bash
cd /home/armol/LieppOS-workspace/LieppOS_kernel/android/kernel/ulefone/gq5012bf1
JOBS=$(nproc) ./tools/ulefone/gq5012bf1/build-sukisu.sh
JOBS=$(nproc) ./tools/ulefone/gq5012bf1/build-sukisu-modules.sh
./tools/ulefone/gq5012bf1/verify-sukisu.sh
```

The wrapper resolves the canonical output directory
`build/sukisu/out` and the local toolchain
`artifacts/toolchains/clang-r487747c`. It does not read from the historical
laboratory directory. The explicit equivalent is:

```bash
KERNEL_SRC=$PWD \
OUT=/home/armol/LieppOS-workspace/LieppOS_kernel/build/sukisu/out \
TOOLCHAIN_DIR=/home/armol/LieppOS-workspace/LieppOS_kernel/artifacts/toolchains/clang-r487747c \
JOBS=$(nproc) ./tools/ulefone/gq5012bf1/build-kernel.sh
```

The build produces `arch/arm64/boot/Image`, `vmlinux`, `System.map`, the in-tree
modules and `Module.symvers` under `build/sukisu/out`. Boot-image and
vendor_boot packaging are not changed by this integration; the existing C46/C48
packaging instructions remain the reference, and no packaging output was
flashed.

## Remaining limitations

- No SukiSU boot, root-manager, SELinux, module-load, or device test was run.
- No SukiSU manager APK was included or installed.
- KPM is disabled; this is a stability choice, not a claim that KPM is broken.
- The branch base includes newer C56 display/touch changes that are themselves
  not proven by a fresh SukiSU boot.
- Kernel ABI/KMI was checked only by a complete module build and symbol
  inspection, not by loading the result on the device.
- The image was not compared bit-for-bit with a booted SukiSU image because no
  such local image exists.
