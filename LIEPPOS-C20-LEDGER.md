# Campaign 20 task ledger — dev20

Status values: `DONE`, `DONE (offline)`, `BLOCKED`, `NOT STARTED`.
No task in this ledger constitutes hardware validation.

| ID | Work item | Owner | Status | Evidence / next action |
|---|---|---|---|---|
| C20-01 | Create dev20 branches from dev19 heads | kernel | DONE | `lieppos/dev20` in `src`, `mtkcam`, `gpu`, `connectivity`, `met_drv_v3`, nested imgsensor |
| C20-02 | Reconcile dev15 754 with dev19 741 | kernel | DONE | `staging/evidence/dev20/MODULE-ACCOUNTING.tsv`; 736 rebuilt identities, 13 removals, 1 rename, 4 relocations |
| C20-03 | Verify all four Mali artifacts | Mali | DONE (offline) | `MALI-VERIFICATION.txt`; 3 production + 1 test/helper, all dev20 vermagic |
| C20-04 | Restore RPMB provider transfer size | kernel | DONE (offline) | `rpmb_get_rw_size` import, 4096-byte cap; `rpmb-mtk.ko` rebuild |
| C20-05 | TinyLCD teardown/IRQ-work ownership | kernel | DONE (offline) | `spi_tiny_co5300_lcd.c`; compile + full modules pass |
| C20-06 | sc8571/sc851x charger lifecycle | kernel | DONE (offline) | power-supply, charger-device, sysfs and class error unwind |
| C20-07 | CCU power-error unwind | camera | DONE (offline) | `mtk_ccu_get_power()` releases refs; external module rebuild |
| C20-08 | Mali logbuffer lifetime/bounds | Mali | DONE (offline) | OF-node put, undersized-region guard, fallback subtraction, mapping reset |
| C20-09 | Trusted/scoped DT build | kernel | DONE (offline) | `make dtbs-gq5012bf1`; base DTB + trusted DTBO; overlay applies |
| C20-10 | Source-owned dev20 configuration | kernel | DONE | `arch/arm64/configs/gq5012bf1_defconfig`, `staging/kernel/config.dk20` |
| C20-11 | Full external-output `Image` build | kernel | DONE (offline) | dev20 `Image`, `vmlinux`, `System.map`, 19,759-symbol `Module.symvers` |
| C20-12 | In-tree strict mixed-provider modpost | kernel | DONE (offline) | 708 modules; final log zero modpost diagnostics |
| C20-13 | Camera/ISP provider closure | camera | DONE (offline) | 9 modules including ISP7SP aggregate and `cam-ut` test/helper |
| C20-14 | Connectivity closure | connectivity | DONE (offline) | 9 modules, stock WLAN axi/WMT inputs |
| C20-15 | MET v3 closure | MET | DONE (offline) | 11 modules, dev20 vermagic |
| C20-16 | Rebuild full module manifest | kernel | DONE (offline) | 741 modules, 0 missing, 0 vermagic mismatch, `sha256sum -c` passed |
| C20-17 | Production/test module split | kernel | DONE | 738 production/optional; `kheaders`, `cam-ut`, Mali DMA-buf test excluded |
| C20-18 | Android/boot config audit | kernel | DONE (offline) | Binder, DMABUF, FS/AVB, SELinux/cgroups, module signing, UFS/RPMB, TEE closure |
| C20-19 | Thermal-camera audit | kernel | BLOCKED | UVC/USB host configured; no board binding/capture/hardware validation |
| C20-20 | Offline boot contract | kernel | DONE (offline) | `LIEPPOS-C20-BOOT-CONTRACT.md`; first-stage placement still absent |
| C20-21 | Research handoff | research | DONE (staged) | `KERNEL-DEVELOPMENT-20-STAGED.patch`; apply-check passed |
| C20-22 | Hardware validation | device | BLOCKED | Not authorized and not attempted; remains 0 |
| C20-23 | Global `make dtbs` | kernel | BLOCKED | Still reaches unrelated MT8192/MT8195 failures; scoped target works |
| C20-24 | Vendor_boot/module placement proof | release | NOT STARTED | No vendor_boot, first-stage load order, or device test |
| C20-25 | Deployable Android image/AVB | release | NOT STARTED | Intentionally out of scope; no flashing artifact |

## Reproduction gates

- Use Android clang/LLD 17 `clang-r487747c`.
- Pass `DEVICE_MODULES_PATH=<src>` and `MTK_PLATFORM=mt6878`.
- Build `Image` first, then the external `mtk_ccuv` provider.
- For in-tree modules use `KBUILD_MIXED_TREE` containing target
  `vmlinux.symvers` plus fresh `mtk_ccuv` exports and the target `System.map`.
- Build providers before consumers; do not use a module's own symvers or
  symbols already present in the target `Module.symvers`.
- Keep strict modpost; no `KBUILD_MODPOST_WARN`, warning suppression, or
  fabricated CRCs.
- Do not use dev15/dev19 `.ko` files as dev20 inputs.

## Build-output root

`/mnt/<reference-ssd>/lieppos-builds/c20-dev20`

The Btrfs source workspace remained below the 6 GiB metadata restart gate
(approximately 5.88 GiB free); all heavy work used the external ext4 output
volume.  Stock certification remains 440/458, LieppOS OK remains 6/458, and
hardware validation remains 0.
