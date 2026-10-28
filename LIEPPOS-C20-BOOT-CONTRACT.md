# Campaign 20 offline Android boot-readiness contract

This contract defines what is proven offline for the GQ5012BF1 / MT6878T
kernel.  It is **not** a boot claim, a flashing instruction, or a deployable
image.  Hardware validation remains **0**.

## Build identity

| Item | Value |
|---|---|
| Release | `6.1.115-mt6878-gq5012bf1-dev20+` |
| Kernel source commit | `8d22fceb803a30bbf289e1c021bdfa0ebe103dc1` |
| Kernel config | `staging/kernel/config.dk20`, SHA-256 `343f058c395fe15b4862ab2c2d8b81e8ecba8a7edeaacaf392985025aa20385d` |
| Source defconfig | `arch/arm64/configs/gq5012bf1_defconfig`, same SHA-256 |
| Output root | `/mnt/<reference-ssd>/lieppos-builds/c20-dev20` |
| `Image` | 30,116,352 bytes, SHA-256 `d9e2d7378719850766ddb2fa4d878d5f1b7977fa60de66f075736a127096724a` |
| `vmlinux` | SHA-256 `47b507293715e210e127b4657d78fa5826630b9c90247777a318ca89a7ce311a` |
| `System.map` | SHA-256 `811ad1ab4e196cc71cb7b11f2b85ce3ddff68888fe6e0da745ecc906adcda101` |
| `Module.symvers` | 19,759 entries, SHA-256 `eb1007d74c2b7480731dcebc3f2811d9536d66fd89c48ea14c1807cae67e1fd7` |
| Modules | 708 in-tree + 33 external = 741; all dev20 vermagic |
| Strict modpost | 0 diagnostics in the final in-tree mixed-tree log; external builds 0 modpost diagnostics |
| Module closure | Every `depends` entry resolves inside the 741 set or a built-in module name; 0 unresolved |
| Trusted DTBO | SHA-256 `e9896d333cd3be163a890df6eed525fe6770dd73a6e0c4e635eb7994e8459c8a` |
| Base DTB | `mt6878.dtb`, SHA-256 `b32509922b7cd6ef4eb53a7feb0f15d61ca1f50a9f1eae17c8cf7c866d8310db` |
| Manifest | 741 lines, SHA-256 `3226ae8fe64c35c34056b03c5b0560f5fafa4e65a647161ce10e59ea0fc9f1e5`; `sha256sum -c` passed |
| Production profile | 738 modules; 3 test/helper entries excluded |

## Android-facing configuration

The canonical configuration remains a vendor/GKI-style configuration; no
Android option was changed by guesswork.

| Area | State | Assessment |
|---|---|---|
| Binder | `CONFIG_ANDROID_BINDER_IPC=y`, `CONFIG_ANDROID_BINDERFS=y`, `CONFIG_ANDROID_BINDER_DEVICES="binder,hwbinder,vndbinder"` | Built-in and present |
| DMABUF heaps | `CONFIG_DMABUF_HEAPS=y`; system heap is `CONFIG_DEVICE_MODULES_DMABUF_HEAPS_SYSTEM=m` | Core built-in; system heap must be loaded before graphics |
| Storage FS | ext4, F2FS, EROFS built-in; dm-crypt, dm-verity, dm-default-key, inline encryption built-in | Required policy/FS pieces are built-in |
| Android security | SELinux, namespaces, cgroups, PSI, BPF syscall/JIT built-in | Present |
| Initramfs | `CONFIG_BLK_DEV_INITRD=y`, gzip and LZ4 ramdisk decompression | Present |
| Module loading | `CONFIG_MODULES=y`, `CONFIG_MODULE_UNLOAD=y`, `CONFIG_MODVERSIONS=y`; module signature not forced | Modules can load if the first-stage environment supplies them |
| Devtmpfs | `CONFIG_DEVTMPFS=n` | This is the preserved stock/vendor choice; Android userspace must provide its `/dev` setup |
| Kernel overlay | `CONFIG_OF_OVERLAY=n` | Not a blocker for this device: the bootloader applies the DTBO |

`CONFIG_KASAN=y` and `CONFIG_KASAN_HW_TAGS=y` remain part of the canonical
config.  They are a debug/performance risk, not a validated boot result.

## Boot-critical module closure

The 741-module set is dependency-closed, but the vendor_boot partition and
first-stage module load order are **not** present or evidenced offline.

| Chain | Modules in the dev20 set | Status |
|---|---|---|
| UFS host/provider | `ufs-mediatek-mod`, `ufs-mediatek-dbg`, `irq-dbg`, `blocktag`, `rpmb` | Present; `ufs-mediatek-mod` depends on the debug closure and `rpmb` |
| RPMB | `rpmb`, `rpmb-mtk` | Present; `rpmb-mtk` imports exactly `rpmb_cmd_req`, `rpmb_get_rw_size`, `ufs_mtk_rpmb_get_raw_dev` |
| TrustKernel / TEE | `tkcore`, `tkcore_drv`, `ffa_v10`, `tmem_ffa`, `microarray_fp_tee` | Present; `tkcore_drv` depends on `tkcore,ffa_v10`; `tmem_ffa` depends on `gz_tz_system,ffa_v10` |
| Platform clocks/power/pinctrl | `clk-mt6878`, `pinctrl-mt6878`, `mtk-pmic-wrap`, `mtk-scpsys`, regulators | Present as modules; first-stage load order is not evidenced |
| Display | `mediatek-drm`, `panel-ky-vtdr6115-dphy-cmd`, `leds-mtk-disp` | Present; the trusted panel node is deliberately disabled |
| Storage crypto | built-in dm-crypt/verity/default-key and inline encryption | Built-in; userspace key/policy not part of this repository |

The complete dependency audit reports zero unresolved module dependencies.
This proves source/build closure only; it does not prove that a bootloader will
load the modules in a usable order.

## Device tree status

`make dtbs-gq5012bf1` is the scoped build target.  It builds:

- `arch/arm64/boot/dts/mediatek/mt6878.dtb`
- `arch/arm64/boot/dts/mediatek/gq5012bf1-board.dtbo`

The trusted overlay applies cleanly to the base DTB.  Its enabled board nodes
are `dsi0`, `mtkfb`, and `mtk_leds`; the panel node remains
`status = "disabled"` because its reset/power/timing wiring is not trusted.
The pending overlay remains disabled by default
(`CONFIG_GQ5012BF1_PENDING_BOARD_DT` is not set).

The global `make dtbs` target still fails on unrelated reference boards, not
on GQ5012BF1:

- MT8192 asurada references four `mt6359_*` labels that do not exist in the
  current MT6359 include, and its `chosen` node is not labelled for
  `mt6359.dtsi`'s `&chosen` reference.
- MT8195 references undefined `CLK_TOP_*` identifiers in its power-domain
  clocks, producing a dtc syntax error.

The scoped target avoids those unrelated failures without hiding them.

## Trusted board limitations

The trusted DT intentionally disables or leaves unresolved:

- Panel reset/power/timing (`panel1@0` disabled)
- Touch controller wiring (SPI3 and the reference FT3680 node disabled)
- Fingerprint transport (SPI1/reference nodes disabled)
- Camera reference nodes and flash
- YFT keys, tiny2c, camplight, and other board peripherals
- Charging/regulator values that are only available in the
  provenance-pending offline copy

Therefore this profile does not provide a working display, touch, fingerprint,
camera, or charging UI.  It is a kernel/source baseline, not an Android
device-ready configuration.

## Thermal camera

The 741-module set contains no dedicated thermal-camera or InfiRay/FLIR/Guide
driver.  The stock-compatible kernel path is standard USB Video Class, not a
private thermal driver: `CONFIG_USB_VIDEO_CLASS=y` and
`CONFIG_MEDIA_USB_SUPPORT=y` are built-in, and `mtu3.ko`,
`xhci-mtk-hcd-v2.ko`, `usb_offload.ko`, and `extcon-mtk-usb.ko` are present in
the module closure.  The base DTS enables the MT6878 `ssusb` controller and
its `xhci0` host; the trusted overlay does not disable them.

This proves a configured kernel UVC/USB path, not a working thermal camera:
there is no board-specific thermal-camera power/transport binding, no
userspace capture test, and no hardware validation.  The thermal-camera
feature remains open.  See
`staging/evidence/dev20/THERMAL-CAMERA-AUDIT.txt`.

## RPMB / TrustKernel finding

The earlier compatibility concern is resolved at the source/ABI level.  The
rebuilt `rpmb-mtk.ko`:

- imports `rpmb_cmd_req`, `rpmb_get_rw_size`, and
  `ufs_mtk_rpmb_get_raw_dev`;
- has `depends: ufs-mediatek-mod,rpmb`;
- contains no Trustonic/mobicore/netlink imports;
- uses a `rpmb_ioctl_get_rw_size()` helper to honour the provider's advertised
  transfer size and caps each legacy ioctl request at 4096 bytes.

No new userspace ioctl command or unverified RPMB protocol was invented.  The
remaining uncertainty is runtime behaviour against the phone's RPMB/TEE, which
has not been exercised.

## Offline boot-readiness gate

A build is **offline boot-contract complete** only when all of the following
are true:

1. The `Image`, `vmlinux`, `System.map`, `Module.symvers`, DTB, and trusted
   DTBO match the recorded hashes.
2. Every one of the 741 modules carries the matching release vermagic.
3. Final strict modpost has no unresolved-symbol or duplicate-export
   diagnostics.
4. All module `depends` entries resolve inside the set or built-ins.
5. The trusted DTBO, not the pending overlay, is selected.
6. The first-stage module environment supplies the UFS/RPMB/TEE/DM closure.

The current evidence satisfies 1-5.  Item 6 is **not evidenced**: there is no
vendor_boot image, module load order, or device test in this campaign.  There
is also no Android system/vendor image, AVB/vbmeta chain, or flashable
artifact.  Hardware validation remains **0**.

## Remaining blockers to a deployable Android boot

- No hardware validation or device boot.
- No vendor_boot/ramdisk module placement or load-order proof.
- Trusted display panel and most board peripherals are disabled pending
  authorized wiring/provenance evidence.
- The pending DT overlay's numeric values are still provenance-pending.
- No userspace Android images, AVB chain, or deployable image was produced.
- Global `make dtbs` remains red on unrelated MT8192/MT8195 reference boards.
