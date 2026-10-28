# LieppOS Campaign 21 handoff (GQ5012BF1 / MT6878T)

Date: 2026-10-24.  This handoff is source/build/offline only.  It is not a
hardware-validation report and it does not contain a deployable Android image.

## Executive result

Campaign 21 completed the dev21 source/build closure that Campaign 20 left
open.  The kernel, all 708 in-tree modules and all 33 external modules were
built against one dev21 ABI.  The 741-module set passes strict modpost, has
zero unresolved module-dependency names and has no vermagic mismatch.

The Android boot chain is **not** declared ready.  The kernel-side config,
storage providers, device-mapper/filesystem support and trusted DT are
source-ready, but no authorized `boot.img`, `init_boot.img`,
`vendor_boot.img`, bootconfig, vendor-ramdisk `modules.load`, Android
system/vendor/userdata image or AVB/vbmeta chain exists in the workspace.
Consequently the Android first-stage module placement and exact boot order
remain unknown.

Historical certification is unchanged: strict stock certification `440/458`,
LieppOS OK `6/458`, hardware validation `0`.

## Final identities

| Item | Value |
|---|---|
| Release | `6.1.115-mt6878-gq5012bf1-dev21+` |
| Kernel source | `src` on `lieppos/dev21` |
| Kernel code HEAD | `d149703e5388032907dd17215a86ab17f6141504` |
| Camera source | `mtkcam` on `lieppos/dev21` |
| Camera HEAD | `7286106471dba28f8a4224e70a45be3c8d5505be` |
| Mali | `c7cb71b6a3a36c095f55e306d05e840bf81c1eb0` (unchanged) |
| Connectivity | `1a9a0c3231c7859eb8595a8236aa40214a7076c7` (unchanged) |
| MET | `c9b8a7d47a28d6e8362e667b629739adf79090e8` (unchanged) |
| Nested imgsensor | `d44461a5f43f1db7e8664179cb3248ebf84ce9d3` (unchanged) |
| Output root | `/mnt/<reference-ssd>/lieppos-builds/c21-dev21` |
| Toolchain | Android clang/LLD 17.0.2 (`clang-r487747c`) |

Kernel code commits:

- `16e01f73888dd7a91a5193bfdf89135fb6a70e7d` UFS teardown / block-tag state
- `1b073273aab7ce3114030df6a76a50079ca16168` RPMB HMAC failure handling
- `1dbc05e419afb2aa8f8a2d0eb4175b60c19b2132` Hynitron lifetime
- `9be7125364d659652ba24e5b330394ffe59e1fb2` thermal-camera tiny2c power/probe
- `015a306a538f2d1871fb53af32be6a78b464522e` vtdr6115 panel lifetime
- `d149703e5388032907dd17215a86ab17f6141504` dev21 release config

Camera build-recipe commit:
`7286106471dba28f8a4224e70a45be3c8d5505be` (ISP7SP provider closure).

## What changed in source

### Storage / UFS

`drivers/ufs/ufs-mediatek.c`, `ufs-mediatek-btag.c` and `ufs-mediatek.h` now:

- remove the manual-GC and NT sysfs entries before work teardown;
- cancel the manual-GC hrtimer and expire/cancel its work, then destroy the
  manual-GC workqueue;
- cancel delay-EH and runtime-PM workqueues during host/remove teardown;
- release the wakeup source and PM-QoS request;
- synchronize the asynchronous block-tag probe cookie so remove cannot race
  the provider's probe completion;
- order teardown before `ufshcd_remove()`.

This addresses module-unload/device-removal use-after-free windows.  It does
not activate new UFS hardware.

### RPMB

`drivers/char/rpmb/rpmb-mtk.c` now aborts a write request if request-HMAC
generation fails instead of transmitting a frame with an invalid MAC.  The
read path stops before comparing a bogus MAC, and the temporary key is wiped
with `memzero_explicit()` on the shared exit path.

Source-visible ioctl surface remains `PROGRAM_KEY=1` (rejected), `WRITE_DATA=3`
and `READ_DATA=4`.  `rpmb_ioctl_get_rw_size()` is an internal helper, not a
userspace ioctl; it wraps the generic `rpmb_get_rw_size()` and caps transfers
at 16 blocks / 4096 bytes.  The stock-facing identity of that name remains a
HOLD because no authenticated stock userspace ioctl declaration or caller is
available.  No ioctl number was invented.

### Display and touch

`panel-ky-vtdr6115-dphy-cmd.c` now releases parent/endpoint/remote DT node
references, drops the parsed backlight node reference, takes a device reference
for the backlight, unwinds DSI attach, panel extension, class and sysfs file
creation failures, calls `put_device()` for the backlight on remove, and makes
remove null-safe.

`hynitron.c` now allocates and destroys the report workqueue correctly,
cancels work before destroying the queue, unregisters the input device instead
of freeing a registered one, releases the sysfs kobject, and unwinds
sysfs/input state on probe failure.

Trusted display and touch nodes remain disabled.  No panel or touch hardware
was enabled or tested.

### Thermal camera

`drivers/misc/mediatek/yft_usb/yft_tiny2c_usb.c` now requests rails as
outputs, validates required rails, validates the chip ID, unwinds GPIO/I2C/
sysfs/private state on probe failure, avoids unregistering its own I2C client
from remove, and releases resources on remove.

Authorized reconstructed stock-module evidence establishes the image
transport as **USB UVC** (FM78100 thermal camera), not CSI/MIPI/SPI.  The
Type-C path uses the extcon/UVC switch and VBUS control described in
`extcon-mtk-usb.c`; the tiny2c I2C device is the power/mode sideband.  The
trusted DTS still withholds the UVC switch/VBUS and tiny2c board bindings, so
the generic UVC driver is present but the board path is not enabled.

### Camera build recipe

`mtkcam/camsys/build-isp7sp.sh` now includes the `ccd_rpmsg` and
`mtk_ccd_remoteproc` `Module.symvers` files in the aggregate ISP7SP provider
set.  Without them strict modpost failed on `mtk_get_client_msgdevice` and
related rpmsg exports; the dev21 aggregate camera module now links.

## Build and ABI result

| Artifact | Size | SHA-256 |
|---|---:|---|
| `Image` | 30,116,352 | `9285fbacfe20bc6a0f63d1c1780c40d808ea4eb7fde7ce2a923935491ec26475` |
| `vmlinux` | 340,956,328 | `f541258219131c7ac961e27500768e43baa97c38b42ba488f15665f800dc30be` |
| `System.map` | 4,895,496 | `04d8ec4ad0d9df36aff610249f5f19f8340572e4736ec348ccaee26a1cd29d2b` |

ABI inputs:

| File | Entries | SHA-256 |
|---|---:|---|
| `out/Module.symvers` | 19,759 | `6179ad70a8a9bd5aa5ae25e901b71ff2d4dedca55ee6c188d2246f5de5ce14a2` |
| `out/vmlinux.symvers` | 15,188 | `b9b173aee754d8b1d7085e004c44c9132fcf8f0c19554969e460065914b9e5e9` |
| `out/mixed/vmlinux.symvers` | 15,194 | `11787dc18644b1afbaee8c913dda1206a9964f6ed52b033025ae21a58d4f05b3` |

The mixed tree is the target `vmlinux.symvers` plus six authenticated
`mtk_ccuv` exports.  No CRC, symbol, provider or vermagic was fabricated.

Module totals:

- 708 in-tree modules + 33 external modules = **741**;
- **738** production/optional + **3** test/helper;
- test/helper: `kheaders.ko`, `cam-ut.ko`,
  `mali_dmabuf_test_mt6878_r44.ko`;
- 741/741 exact dev21 vermagic;
- 1,184 direct dependency edges, 221 distinct dependency names,
  **0 unresolved module names**;
- final strict modpost diagnostics: **0**.

The 33 external modules are four Mali, nine camera/ISP, nine connectivity and
eleven MET modules.  All were rebuilt against dev21.  Required provider
`Module.symvers` inputs were supplied explicitly where the external recipes
need them.

Config:

- source `arch/arm64/configs/gq5012bf1_defconfig`
- staged `staging/kernel/config.dk21`
- SHA-256 `45c80fdea3fdd904df6e58684ff571230d3987a00d4c5375c37f6eeb1734efc1`
- effective config is byte-identical to the source defconfig after
  `olddefconfig`;
- only delta from dev20 is
  `CONFIG_LOCALVERSION="-mt6878-gq5012bf1-dev21"`.

## Android kernel-side contract

Enabled and audited:

- Binder/binderfs, vendor hooks, cgroups, PSI;
- DMA-BUF heaps (`DEVICE_MODULES_DMABUF_HEAPS_SYSTEM=m`);
- `BLK_DEV_DM`, `DM_VERITY`, `DM_DEFAULT_KEY`, `DM_CRYPT`, inline encryption;
- F2FS, EROFS, ext4, overlayfs, FUSE;
- SELinux/securityfs, tracing, initrd.

Deliberate non-gaps: `DEVTMPFS` is unset as in the reference Android profile;
`DMABUF_HEAPS_CMA` is unset; `USER_NS` is unset.  No option was enabled just
to make this table look complete.  Kernel command line and bootconfig remain
external inputs; no value was invented.

Storage boundary:

- UFS core/SCSI block built-in;
- MediaTek UFS host `ufs-mediatek-mod.ko` is a module;
- UFS PHY and RPMB are modules;
- `ufs-mediatek-mod` depends on `ufs-mediatek-dbg, irq-dbg, blocktag, rpmb`;
- `ufs-mediatek-dbg` depends on `aee_aed, irq-dbg, mrdump`;
- `blocktag` depends on `mrdump`;
- `rpmb-mtk` depends on `ufs-mediatek-mod, rpmb`.

All names are present in the 741-module set, but there is no first-stage
`modules.load` or vendor ramdisk to prove Android placement/order.  Rootfs on
UFS cannot mount until the controller module and its providers are loaded.

## Boot-readiness states

| State | Result |
|---|---|
| `SOURCE_BUILT` | PASS |
| `MODULE_ABI_CLOSED` | PASS |
| `BOOT_CRITICAL_KERNEL_CONFIG_READY` | PASS (kernel mapping only) |
| `EARLY_PROVIDER_CLOSED` | PARTIAL (link closure only) |
| `ANDROID_PLACEMENT_EVIDENCED` | FAIL/UNKNOWN |
| `TRUSTED_BOARD_DESCRIPTION_COMPLETE` | FAIL |
| `BOOT_ARTIFACTS_COMPLETE` | FAIL |
| `OFFLINE_BOOT_INTEGRATION_READY` | FAIL |
| `HARDWARE_VALIDATED` | FAIL/0 |

Missing artifacts are specifically: boot image, init_boot, vendor_boot,
bootconfig, Android init scripts, vendor-ramdisk modules, system/vendor/
userdata images and the AVB/vbmeta chain.  None was fabricated.

## Device tree

Scoped `dtbs-gq5012bf1` succeeds:

- `mt6878.dtb`
  `b32509922b7cd6ef4eb53a7feb0f15d61ca1f50a9f1eae17c8cf7c866d8310db`
- `gq5012bf1-board.dtbo`
  `e9896d333cd3be163a890df6eed525fe6770dd73a6e0c4e635eb7994e8459c8a`
- `fdtoverlay` succeeds; merged FDT 339,846 bytes.

`CONFIG_GQ5012BF1_PENDING_BOARD_DT` is not set.  Trusted/pending separation is
preserved.  No trusted numeric wiring was added.  Global `make dtbs` still
fails on unrelated MT8192 label mismatches and MT8195 `CLK_TOP_*` identifiers;
that limitation is recorded, not hidden.

## Research repository status

The canonical research repository was not accessed in Campaign 21; access to
`/home/<user>/<protected-tree>/` is outside the current hard boundary.  C19/C20
research patches were therefore not applied or claimed.  A new C21 research
patch is staged for review under `staging/evidence/dev21/`; it was created
without a canonical-repo commit or push.

## Unresolved blockers

1. No Android boot/vendor_boot/modules.load/bootconfig evidence; first-stage
   UFS provider placement and load order remain unknown.
2. No deployable Android image or AVB/vbmeta chain.
3. Trusted display, touch, thermal-camera UVC switch/VBUS, tiny2c and many
   board peripherals remain disabled or provenance-pending.
4. Thermal-camera userspace UVC/thermal HAL and actual capture are absent.
5. RPMB stock-facing `rpmb_ioctl_get_rw_size` identity remains HOLD; no
   undocumented ABI was invented.
6. Global DT build has unrelated reference-board failures.
7. Btrfs metadata free is approximately 4.74 GiB, below the 6 GiB heavy-build
   gate; all heavy builds used the authorized external ext4 volume.

## Safety statement

No phone access, ADB, fastboot, flashing, module loading, RPMB/TEE hardware
operation, charging/power experiment or hardware-validation claim occurred.
No source Git push occurred.  The protected path
`/home/<user>/<protected-tree>/` was not accessed.  Hardware validation remains
`0`.

## Resume

Read in this order:

1. `src/LIEPPOS-C21-LEDGER.md` — work-package state and exact continuation;
2. `staging/evidence/dev21/BOOT-READINESS.txt` — independent readiness gates;
3. `staging/evidence/dev21/MODULE-DEPENDENCY-AUDIT.txt` — ABI/link closure;
4. `staging/evidence/dev21/STORAGE-CONTRACT.txt`;
5. `staging/evidence/dev21/RPMB-INTERFACE.txt`;
6. `staging/evidence/dev21/THERMAL-CAMERA.txt`.

Do not rebuild merely to reproduce the count: the dev21 741-module set is
already built and hashed.  The next useful work requires authorized Android
boot artifacts or new hardware evidence, not another generic build framework.
