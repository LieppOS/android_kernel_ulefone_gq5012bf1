# LieppOS Campaign 20 handoff (dev20)

Campaign 20 turns the Campaign 19 dev19 baseline into a dev20 source and
offline build baseline with a reconciled module inventory, a source-owned
configuration, a scoped device-tree target, and targeted lifetime/ABI fixes.
It does not claim hardware boot or Android readiness.

## Revision contract

| Input | Branch | Commit |
|---|---|---|
| Kernel (source + docs) | `lieppos/dev20` | code commit `8d22fceb803a30bbf289e1c021bdfa0ebe103dc1`; this document is a later docs-only commit |
| Mali | `lieppos/dev20` | `c7cb71b6a3a36c095f55e306d05e840bf81c1eb0` |
| Camera / CCU | `lieppos/dev20` | `ca950fd989f7ec0b0b9ae7ca76911271a2bccefb` |
| Connectivity | `lieppos/dev20` | `1a9a0c3231c7859eb8595a8236aa40214a7076c7` (unchanged source) |
| MET | `lieppos/dev20` | `c9b8a7d47a28d6e8362e667b629739adf79090e8` (unchanged source) |
| imgsensor (nested) | `lieppos/dev20` | `d44461a5f43f1db7e8664179cb3248ebf84ce9d3` (unchanged source) |

The dev19 branches and commits are preserved.  No branch history was
rewritten and no generic build framework was added.

## Build identity

- Toolchain: Android clang/LLD 17.0.2, `clang-r487747c`.
- Output: `/mnt/<reference-ssd>/lieppos-builds/c20-dev20`.
- Config: `staging/kernel/config.dk20` and the source copy
  `arch/arm64/configs/gq5012bf1_defconfig`, both
  `343f058c395fe15b4862ab2c2d8b81e8ecba8a7edeaacaf392985025aa20385d`.
- Release: `6.1.115-mt6878-gq5012bf1-dev20+`.
- `Image`: 30,116,352 bytes,
  `d9e2d7378719850766ddb2fa4d878d5f1b7977fa60de66f075736a127096724a`.
- `vmlinux`:
  `47b507293715e210e127b4657d78fa5826630b9c90247777a318ca89a7ce311a`.
- `System.map`:
  `811ad1ab4e196cc71cb7b11f2b85ce3ddff68888fe6e0da745ecc906adcda101`.
- `Module.symvers`: 19,759 entries,
  `eb1007d74c2b7480731dcebc3f2811d9536d66fd89c48ea14c1807cae67e1fd7`.
- Modules: 708 in-tree + 33 external = 741, all dev20 vermagic; strict
  modpost clean; `sha256sum -c` passed from the output root.
- Production profile: 738 modules; three test/helper entries are separated.

## Module reconciliation result

The dev15 collection (754) and dev19 manifest (741) were reconciled by exact
normalized path plus an explicit identity pass for one rename and four
relocations.

- 736 exact path identities, all rebuilt (hashes differ by design).
- 18 exact path losses = 13 intentional removals + one WLAN rename + four
  Mali relocations.
- 5 exact path additions = the rename target + four Mali targets.
- Net change: 13, all explained by ISP7SP/TrustKernel provider selection.

The 13 removals are the legacy ISP7S/ISP7.1/ISP6S camera providers,
`camera_mem`, the legacy in-tree CCU, and the Trustonic GUD/TUI modules.  They
are not missing production modules.  The four Mali modules are present in both
manifests: three production and `mali_dmabuf_test` as a test/helper exporter.
See `LIEPPOS-C20-MODULE-ACCOUNTING.md` for the full table and evidence hashes.

## Source changes

### RPMB / UFS compatibility

`drivers/char/rpmb/rpmb-mtk.c` now computes the transfer size from the UFS RPMB
provider via `rpmb_get_rw_size()` and uses it for every legacy ioctl chunk,
capped at the 4096-byte request window.  This restores the stock module's
`rpmb_get_rw_size` import and avoids hard-coded one-block transfers.  The
rebuilt module imports exactly:

```text
rpmb_cmd_req
rpmb_get_rw_size
ufs_mtk_rpmb_get_raw_dev
```

and has `depends: ufs-mediatek-mod,rpmb`.  No Trustonic/mobicore symbol is
present.  No new userspace ioctl was invented.

### Tiny LCD lifecycle

`spi_tiny_co5300_lcd.c` now:

- initializes the TE delayed work before requesting the TE IRQ;
- tracks IRQ, misc-device, sysfs-attribute, and work ownership;
- cancels work and frees the IRQ/misc/sysfs/GPIO/buffer resources on remove
  and on every probe error path;
- returns a failed misc registration instead of reporting a successful probe.

### Charger lifetime

- `sc8571_charger.c` uses a non-devm power-supply registration so its explicit
  unregister is balanced; it also unregisters the charger device, removes its
  sysfs file, fixes the stale `ret` check after work-mode selection, and
  unwinds a failed class registration instead of passing an `ERR_PTR` class to
  `class_create_file`.
- `sc851x_charger.c` removes its sysfs file and no longer frees devres memory
  manually.

### CCU power unwind

`mtkcam/ccusys/mtk_ccu_isp71.c` releases the cammainpwr and device runtime-PM
references before returning `-ENODEV` when the SPM base is absent.

### Mali logbuffer

`mtk_platform_logbuffer.c` now puts the reserved-memory OF node, refuses to use
an undersized reserved region instead of underflowing its remaining size, and
clears mapping state after teardown.  `mtk_platform_common.c` clears the
GPUFREQ log-buffer pointer after freeing it.

### Device tree and configuration

- Added `make dtbs-gq5012bf1`, which builds only `mt6878.dtb` and the trusted
  `gq5012bf1-board.dtbo`.
- Added the vendor `mediatek/foo.dtsi` include root required by the base board
  DTS; without it `mt6878.dts` could not find `mediatek/mt6685.dtsi`.
- Added `arch/arm64/configs/gq5012bf1_defconfig` and
  `staging/kernel/config.dk20`, preserving the dev19 option set with the dev20
  localversion.

The trusted DTBO hash is unchanged at
`e9896d333cd3be163a890df6eed525fe6770dd73a6e0c4e635eb7994e8459c8a`; the base
DTB is `b32509922b7cd6ef4eb53a7feb0f15d61ca1f50a9f1eae17c8cf7c866d8310db`.
The pending overlay remains disabled.

## Build procedure (offline, external output only)

1. Use the external ext4 output root; do not heavy-build in the Btrfs source
   workspace.
2. Build `Image` with the dev20 config, `DEVICE_MODULES_PATH=<src>`, and
   `MTK_PLATFORM=mt6878`.
3. Build the external `mtk_ccuv` provider against the new vmlinux exports.
4. Build in-tree modules with `KBUILD_MIXED_TREE` containing the target
   `vmlinux.symvers` plus the fresh `mtk_ccuv` exports, and `System.map`.
   This resolves the two legitimate CCU consumers without
   `KBUILD_MODPOST_WARN`.
5. Build the external groups in provider order: camera (ccuv, imgsensor, DPE,
   cam_cal, camsys, ISP7SP recipe), Mali, connectivity (with the stock WLAN
   inputs), then MET.
6. Verify all 741 vermagic values and `sha256sum -c`.

The first two plain `Image modules` attempts are retained in the evidence as
expected provider-bootstrap failures; the final mixed-tree build is the
authoritative one.  No module from dev15 or dev19 was copied forward.

## Evidence index

- `LIEPPOS-C20-LEDGER.md`
- `staging/evidence/dev20/FINAL-VALIDATION.txt`
- `staging/evidence/dev20/MODPOST-STRICT.txt`
- `staging/evidence/dev20/MANIFEST.sha256`
- `staging/evidence/dev20/PRODUCTION-MANIFEST.sha256`
- `staging/evidence/dev20/MODULE-ACCOUNTING.tsv`
- `staging/evidence/dev20/MALI-VERIFICATION.txt`
- `staging/evidence/dev20/THERMAL-CAMERA-AUDIT.txt`
- `staging/evidence/dev20/FINAL-AUDIT.txt`
- `staging/evidence/dev20/SHA256-VERIFY.txt`
- `staging/evidence/dev20/c20-in-tree-modules-mixed-2.log`
- `staging/evidence/dev20/c20-*.log`
- `staging/evidence/dev20/dt-merge/`
- `staging/kernel/config.dk20`
- `staging/evidence/dev20/KERNEL-DEVELOPMENT-20-STAGED.patch`,
  SHA-256 `fa941a3c2d3df49f71e2d2f04c0402058e7a2324fe764cb0c3b0ea7317b51de5`

## Mandatory Campaign 20 answers

**Did the 754 -> 741 difference hide a production regression?**
No.  It is 13 intentional provider/exclusion changes; one WLAN rename and
four Mali relocations account for the rest of the path differences.

**Are all four Mali modules staged?**
Yes.  Three production modules and one test/helper exporter; all four have
dev20 vermagic and passed the Mali recipe's modpost.

**Is the production count 741?**
No.  The engineering manifest is 741; the production profile is 738 after
excluding `kheaders.ko`, `cam-ut.ko`, and the Mali DMA-buf test exporter.

**Is the kernel built and dependency-closed?**
Yes, offline: Image + 741 modules, strict modpost, zero unresolved `depends`,
all matching vermagic, and manifest verification passed.

**Is the device-tree delivery correct?**
The scoped target builds the base MT6878 DTB and trusted overlay successfully;
the overlay applies cleanly.  Global `make dtbs` still fails on unrelated
MT8192/MT8195 reference sources and is reported, not hidden.

**Is Android boot-ready on hardware?**
No.  Hardware validation is 0.  The trusted display panel and most board
peripherals remain disabled, and there is no vendor_boot module placement,
userspace image, AVB chain, or deployable artifact.

**What remains for the RPMB/TEE contract?**
Source/ABI closure is complete: the stock import set is restored, the
TrustKernel modules are present, and dependencies resolve.  Runtime RPMB/TEE
behaviour against the phone remains unverified because no hardware operation is
authorized in this campaign.

**Is the thermal camera supported?**
The kernel has the standard UVC stack built in and the MT6878 USB host modules
are present, but there is no dedicated thermal-camera driver, board-specific
power/transport binding, userspace capture test, or hardware validation.  It
remains an open feature, not a claimed capability.

## Limits

- No phone access, ADB/fastboot, flashing, module loading, RPMB/TEE hardware
  operation, charging/power experiment, or hardware validation occurred.
- No canonical `LieppOS/ulefone-gq5012bf1-research` checkout was available;
  the research notes are staged as a verified patch, not claimed committed.
- The Btrfs metadata free space is about 5.85 GiB, below the 6 GiB heavy-build
  restart gate; all heavy work used the external ext4 output volume.
- Stock certification remains 440/458; LieppOS OK remains 6/458; hardware
  validation remains 0.
