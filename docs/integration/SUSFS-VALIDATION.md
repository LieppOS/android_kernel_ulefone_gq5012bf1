# SukiSU builtin + SUSFS v2.3.0 offline validation

Validation date: 2026-10-30 UTC. This is an offline source/build/image
validation. It is **not** a boot test.

## Source and build identity

| Item | Value |
| --- | --- |
| Branch | `integration/sukisu-susfs-v2.3.0` |
| Source integration commit | `1d21fdf557fcdb1e30fd3aae9aa88acf1c13e0b5` |
| Base | `608952fca0426c1ec7faf91c610f2fb1e1338578` |
| C56 base | `b9d284691a53a9aaeaed9e0199b52ef2a5847399` |
| SukiSU builtin | `b16e899a555e9b96aa1225f049cd7274820ca5b8`, version 41100 |
| SUSFS | `v2.3.0`, `ee155a1ad4219804a7c54cb39dd4fa1b5c2a3fa4` |
| Kernel release | `6.1.115-mt6878-gq5012bf1-c29+` |
| Toolchain | `artifacts/toolchains/clang-r487747c` |

## Build outputs

| Artifact | SHA256 |
| --- | --- |
| `Image` | `c72065ba0cb5e40d1133956184a3471a15ed2b4dcd57b14a5957b444bc912d46` |
| `vmlinux` | `93edd0aee4e68b07ece49871fab9d31ad79dd20a739e2baf1a45a1e190245f6d` |
| `System.map` | `63b5768f92bee3514b2c02f614729d275e7ab77c650a121a305986d646d08b65` |
| `Module.symvers` | `fad4c397a5d560b1c425bff5201ce415306ef254b29be87a9d1d3ff51ba6e44c` |
| `modules-only.symvers` | `664eea239871deb5554e9bf2b787e41f9d5cda87167608df2644b18b87dcab93` |
| `.config` | `eb3982f2e071c6c9aa95c80cbfc88b2e0cffb979e1a19356dda7c177bc2275f7` |

The kernel and 702 in-tree modules build successfully. `verify-susfs.sh` passes.
No `kernelsu.ko` or `susfs.ko` is produced. No SUSFS/KSU symbol is exported in
`Module.symvers`.

Selected built-in symbols in `System.map`:

```text
susfs_init
susfs_show_version
susfs_set_priority_uid_filter
susfs_set_avc_log_spoofing
ksu_extension_susfs_get
kernelsu_init
```

## Configuration

```text
CONFIG_KSU=y
CONFIG_KSU_FEATURE_ADBROOT=y
CONFIG_KSU_SUSFS=y
CONFIG_KSU_SUSFS_SUS_PATH=y
CONFIG_KSU_SUSFS_SUS_MOUNT=y
CONFIG_KSU_SUSFS_SUS_KSTAT=y
CONFIG_KSU_SUSFS_SPOOF_UNAME=y
CONFIG_KSU_SUSFS_ENABLE_LOG=y
CONFIG_KSU_SUSFS_HIDE_KSU_SUSFS_SYMBOLS=y
CONFIG_KSU_SUSFS_SPOOF_CMDLINE_OR_BOOTCONFIG=y
CONFIG_KSU_SUSFS_OPEN_REDIRECT=y
CONFIG_KSU_SUSFS_SUS_MAP=y
```

KPM is disabled. `CONFIG_KSU_MANUAL_SU` is not a builtin Kconfig option and is
not present. The SUSFS priority-UID and AVC-log interfaces are compiled without
separate Kconfig switches.

## Vendor module ABI

The C46 vendor_boot contains 195 first-stage modules. All 195 were extracted
and checked against the new kernel's `Module.symvers`:

- 8,849 symbol CRC references.
- 0 missing symbols.
- 0 CRC mismatches.
- All modules report vermagic
  `6.1.115-mt6878-gq5012bf1-c29+ SMP preempt mod_unload modversions aarch64`.

The C46 vendor_boot is therefore retained unchanged in the test package. This
is an offline ABI check, not a module-load test.

## Slot-B package

Path: `/home/armol/LieppOS-workspace/LieppOS_kernel/build/susfs-slot-b`.

| Partition | Image | Bytes | SHA256 |
| --- | --- | ---: | --- |
| `boot_b` | `images/boot_b-c58-susfs-v2.3.0.img` | 67,108,864 | `1dd80142dbfc3fee01cc3b625a440ce319705692b8e7ce9d7df8c3fb12bc624a` |
| `vendor_boot_b` | `images/vendor_boot_b-c46-mtk-mmc.img` | 67,108,864 | `fee66502411e57c958cff58021e7379f6a2f0cd05b061738ac0469974360bfb6` |
| `vbmeta_b` | `images/vbmeta_b-c46-mtk-mmc.img` | 8,388,608 | `29c29463bf8bed9cbe3efdf7d0d985c852d30acf4f22a098c190c1043e3ae9ea` |

Rollback set: C45 installed Slot-B images (`boot_b` C44 image
`f80b77e1…`, `vendor_boot_b` `329082ac…`, `vbmeta_b` `bdb86f8b…`). The full
rollback commands and hashes are in the package `README.md` and the C58
campaign README.

Image checks passed:

- New boot image is 64 MiB, boot header v4, zero ramdisk, zero boot signature.
- Kernel payload is legacy LZ4 (`02214c18`) and decompresses byte-exactly to the
  built `Image`.
- New boot AVB hash footer and `SHA256_RSA2048` signature verify.
- C46 root signature/flags/rollback index verify; boot CHAIN location 3 uses the
  existing development key; C46 vendor_boot hash descriptor verifies.
- `avbtool` verifies the complete C46 root chain, including the new boot image
  and all seven bound hash/hashtree children.
- No private key is present in the package.

## Protected state

- C56 worktree: `b9d284691a53a9aaeaed9e0199b52ef2a5847399`; only its pre-existing
  untracked `scripts/__pycache__/` remains.
- C47 worktree: detached at `a6b594803`; exactly the two protected modified files
  remain.
- C56/C47 device driver files are unchanged from C56; no device driver source
  was overwritten.
- Research worktree: the 16 pre-existing TODO deletions remain unstaged.
- No phone, ADB, fastboot, partition, slot, image write, reboot, push, reset,
  clean, or Git GC operation was performed.
- `/home/armol/androido_dalykai/` was not accessed.

## Limitations

- No boot, first-stage module load, SELinux policy, manager, root, or root-hiding
  runtime test was performed.
- The C46 vendor_boot/vbmeta pair is offline-validated but never booted.
- C45 is the last hardware-tested Slot-B attempt and it failed at the mtk_mmc
  probe after reaching first-stage init/MediaTek DRM.
- SukiSU manager/`ksud` compatibility with this kernel is source-level only.
