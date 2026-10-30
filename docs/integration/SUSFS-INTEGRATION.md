# SukiSU builtin + SUSFS v2.3.0 integration

## Status

| Item | Value |
| --- | --- |
| Integration state | Source imported, built, and offline-validated |
| Hardware state | **Not booted**; no phone, partition, slot, image, or manager operation was performed |
| Active branch | `integration/sukisu-susfs-v2.3.0` |
| Branch base | `integration/sukisu-ultra-v4.3.0` at `608952fca0426c1ec7faf91c610f2fb1e1338578` |
| Device baseline | Protected C56 `b9d284691a53a9aaeaed9e0199b52ef2a5847399` |
| Kernel release | `6.1.115-mt6878-gq5012bf1-c29+` |
| SukiSU | Builtin `b16e899a555e9b96aa1225f049cd7274820ca5b8`, version 41100 |
| SUSFS | `v2.3.0`, `ee155a1ad4219804a7c54cb39dd4fa1b5c2a3fa4` |
| Hook model | SukiSU builtin manual hooks; SUSFS built into `vmlinux` |

## Why builtin, not SukiSU main

SukiSU Ultra v4.3.0 `main` is the kprobe/kretprobe implementation. Its
`extension/susfs_extension.c` is only a status/advertisement stub. It does not
carry `CONFIG_KSU_SUSFS`, does not call `susfs_init()`, and does not contain the
manual hook points required by the SUSFS kernel patch.

The SukiSU `builtin` branch is the v4.3.0-era implementation that carries the
SUSFS Kconfig menu and manual hook integration. This branch is based on the
current SukiSU integration commit and replaces only the SukiSU source subtree
with that builtin snapshot. The C56 display/touch tree is retained; no device
driver source was changed.

## Compiled SUSFS features

The following are enabled in both `configs/c44-reference.config` and
`arch/arm64/configs/gq5012bf1_defconfig` and are present in the built `.config`:

| Config | Feature |
| --- | --- |
| `CONFIG_KSU_SUSFS` | Core SUSFS |
| `CONFIG_KSU_SUSFS_SUS_PATH` | Suspicious-path hiding |
| `CONFIG_KSU_SUSFS_SUS_MOUNT` | Suspicious-mount hiding |
| `CONFIG_KSU_SUSFS_SUS_KSTAT` | kstat spoofing |
| `CONFIG_KSU_SUSFS_SPOOF_UNAME` | uname spoofing |
| `CONFIG_KSU_SUSFS_ENABLE_LOG` | SUSFS kernel logging |
| `CONFIG_KSU_SUSFS_HIDE_KSU_SUSFS_SYMBOLS` | Hide KSU/SUSFS symbols from kallsyms |
| `CONFIG_KSU_SUSFS_SPOOF_CMDLINE_OR_BOOTCONFIG` | Boot command-line/bootconfig spoofing |
| `CONFIG_KSU_SUSFS_OPEN_REDIRECT` | Open-redirect hiding |
| `CONFIG_KSU_SUSFS_SUS_MAP` | Suspicious mmap hiding |

Two v2.3.0 interfaces are compiled without a separate Kconfig switch:
priority-UID filtering (`susfs_set_priority_uid_filter`) and AVC-log spoofing
(`susfs_set_avc_log_spoofing`). Their symbols are present in `System.map`.

## Not compiled / not available in this integration

- `CONFIG_KSU_SUSFS_SUS_OVERLAYFS` — no such v2.3.0 Kconfig option/source path.
- `CONFIG_KSU_SUSFS_SUS_SU` — no such v2.3.0 Kconfig option/source path; the
  old `CMD_SUSFS_SUS_SU` UAPI command is marked deprecated.
- `CONFIG_KSU_SUSFS_AUTO_ADD_SUS_KSU_DEFAULT_MOUNT`,
  `CONFIG_KSU_SUSFS_AUTO_ADD_SUS_BIND_MOUNT`,
  `CONFIG_KSU_SUSFS_AUTO_ADD_TRY_UMOUNT_FOR_BIND_MOUNT` — not present in this
  SUSFS/SukiSU snapshot. `CMD_SUSFS_ADD_TRY_UMOUNT` is marked deprecated.
- KPM (`CONFIG_KPM`) — disabled; this is not a SUSFS feature.
- SukiSU `main` kprobe/syscall-hook implementation — not compiled on this
  branch; the builtin manual-hook implementation is used instead.

## Userspace compatibility boundary

The SukiSU v4.3.0 release manager packages the `ksu_susfs_2.2.0` userspace
asset. The kernel side here is SUSFS v2.3.0 because SukiSU builtin `b16e899a`
requires its priority-UID/AVC interfaces. Common SUSFS commands are present,
but the v2.2.0 userspace asset may not expose every v2.3.0 feature. No manager
APK or userspace SUSFS binary is included, installed, or tested here.

## Build

From the canonical kernel checkout:

```bash
cd /home/armol/LieppOS-workspace/LieppOS_kernel/android/kernel/ulefone/gq5012bf1
JOBS=$(nproc) ./tools/ulefone/gq5012bf1/build-susfs.sh
JOBS=$(nproc) ./tools/ulefone/gq5012bf1/build-susfs-modules.sh
./tools/ulefone/gq5012bf1/verify-susfs.sh
```

Outputs are under `build/susfs/out`. The wrapper uses the canonical local
toolchain `artifacts/toolchains/clang-r487747c`; it does not read
`/home/armol/kernel-work/`.

## Boot-image boundary

SUSFS and SukiSU are built into the kernel `Image`. The test image packaging is
documented in `research/ulefone-gq5012bf1/campaigns/c58-susfs-integration/`.
The package uses the offline-validated C46 vendor_boot/vbmeta chain and a new
boot image signed with the existing C40 development boot key. The C45 images
are included only as the rollback set. No image was flashed.

## Limitations

- Compilation does not prove boot, first-stage module loading, SELinux policy
  acceptance, manager operation, root, or root-hiding effectiveness.
- The C46 vendor_boot/vbmeta images were offline-validated but never booted.
- The C45 baseline is the last hardware-tested Slot-B attempt; it reached
  first-stage init/MediaTek DRM and then hit the mtk_mmc probe fault.
- SUSFS v2.3.0 userspace compatibility is not proven with the v4.3.0 manager.
