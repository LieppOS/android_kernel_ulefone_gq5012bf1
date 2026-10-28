# LieppOS Campaign 21 continuation ledger (dev21)

This is the single resumable engineering ledger for Campaign 21.  It records
the actual implementation state, not a plan.  Campaign 20 remains the
historical baseline.

## Starting point and branches

Campaign 20 dev20 heads were preserved and used as the dev21 bases:

| Source | Branch | C20 base |
|---|---|---|
| Kernel | `lieppos/dev21` | `f3f4cb91171896b13d40ad40290d641a0c350832` |
| Mali | `lieppos/dev21` | `c7cb71b6a3a36c095f55e306d05e840bf81c1eb0` |
| Camera | `lieppos/dev21` | `ca950fd989f7ec0b0b9ae7ca76911271a2bccefb` |
| Connectivity | `lieppos/dev21` | `1a9a0c3231c7859eb8595a8236aa40214a7076c7` |
| MET | `lieppos/dev21` | `c9b8a7d47a28d6e8362e667b629739adf79090e8` |
| Nested imgsensor | `lieppos/dev21` | `d44461a5f43f1db7e8664179cb3248ebf84ce9d3` |

No dev20 branch was rewritten.  No source repo was pushed.

## Work package state

| WP | State | Actual result |
|---|---|---|
| WP01 | COMPLETE | Verified dev20 heads; created `lieppos/dev21` in kernel, Mali, camera, connectivity, MET and nested imgsensor. |
| WP02 | COMPLETE | Audited effective dev21 config against Android boot requirements. No boot-critical option was found missing. |
| WP03 | COMPLETE (no change justified) | No Kconfig change was made merely for appearance. The only config delta from dev20 is `CONFIG_LOCALVERSION` to dev21. |
| WP04 | COMPLETE | Audited UFS, SCSI block, UFS PHY, RPMB, DM, filesystems and encryption source/config. |
| WP05 | COMPLETE | Fixed MediaTek UFS host teardown: sysfs, manual-GC timer/workqueue, delay-EH work, runtime-PM work, wakeup source, PM QoS and async block-tag probe are now synchronized/released. |
| WP06 | COMPLETE | Verified built-in `dm-verity`, `dm-default-key`, `dm-crypt`, inline encryption, F2FS, EROFS, ext4, overlayfs and FUSE. |
| WP07 | COMPLETE | Established the source-visible RPMB ioctl surface and the internal meaning of `rpmb_ioctl_get_rw_size`. |
| WP08 | COMPLETE | Fixed HMAC-failure request transmission and wiped the temporary key; retained HOLD on the undocumented stock-facing ioctl identity. |
| WP09 | COMPLETE | Generated and audited the 741-module dependency graph; 0 unresolved names. |
| WP10 | PARTIAL | Link-level early chain is closed; Android first-stage placement is unknown because no vendor_boot/modules.load artifact exists. |
| WP11 | COMPLETE | Rebuilt scoped DT; trusted base and overlay hashes match the verified C20 values. |
| WP12 | COMPLETE (no numeric change) | Preserved trusted/pending separation. No numeric hardware property was added without evidence. |
| WP13 | NOT CHANGED | No additional source-backed clock/pinctrl provider defect was established in this pass. |
| WP14 | NOT CHANGED | No additional source-backed platform PM/IOMMU defect was established in this pass. |
| WP15 | COMPLETE | Fixed vtdr6115 panel probe/remove OF, backlight, attach/extension error and class/file ownership. |
| WP16 | COMPLETE | Fixed Hynitron input-device/workqueue/sysfs lifetime and probe unwind. |
| WP17 | COMPLETE (no source change) | Rebuilt all four Mali r44p1 modules against dev21; C20 lifetime fixes preserved. |
| WP18 | COMPLETE | Fixed ISP7SP recipe provider closure for `ccd_rpmsg` and `mtk_ccd_remoteproc`; rebuilt all nine camera/ISP modules. |
| WP19 | NOT CHANGED | No new charger/regulator defect was established; C18/C20 fixes preserved. |
| WP20 | COMPLETE | Fixed tiny2c thermal-camera power/probe/remove and Hynitron touch lifetime; no fingerprint/NFC hardware activation. |
| WP21 | COMPLETE | Established USB UVC/FM78100 transport for the thermal camera from authorized reconstructed stock-module evidence. |
| WP22 | PARTIAL | Implemented tiny2c reversible power/probe source; board UVC switch/VBUS/tiny2c bindings and userspace thermal HAL remain unavailable. |
| WP23 | COMPLETE | Finalized dev21 source config; effective config is byte-identical to the source defconfig. |
| WP24 | COMPLETE | Established built-in/module boundary: UFS core/DM/filesystems built-in; MediaTek UFS host, PHY and RPMB are modules. |
| WP25 | COMPLETE | Produced offline boot-readiness category evaluation (`staging/evidence/dev21/BOOT-READINESS.txt`). |
| WP26 | COMPLETE | Identified exact missing artifacts: boot/init_boot/vendor_boot, bootconfig, vendor ramdisk modules.load, Android init scripts, system/vendor/userdata images and AVB/vbmeta chain. |
| WP27 | COMPLETE | Targeted builds of changed UFS, RPMB, Hynitron, tiny2c and panel modules succeeded. |
| WP28 | COMPLETE | Full dev21 Image, vmlinux and 708 in-tree modules built. |
| WP29 | COMPLETE | 33/33 external modules rebuilt against dev21, including Mali, camera, connectivity and MET. |
| WP30 | COMPLETE | Strict modpost success, 741/741 vermagic match, 0 unresolved module names. |
| WP31 | COMPLETE | Pending DT remains disabled; trusted overlay applies. |
| WP32 | COMPLETE | 741/738 manifests and 741-row module accounting written under `staging/evidence/dev21/`. |
| WP33 | BLOCKED | Canonical research-repo access is outside the current hard boundary. C19/C20 patches were not applied or claimed. |
| WP34 | COMPLETE (staged) | C21 research patch staged for review; no canonical research commit or push. |
| WP35 | COMPLETE | This ledger plus the C21 handoff and evidence set. |
| WP36 | COMPLETE | Exact continuation state and commands below. |

## Final source identities

| Source | Branch | Final HEAD (before docs-only commit) |
|---|---|---|
| Kernel | `lieppos/dev21` | `d149703e5388032907dd17215a86ab17f6141504` |
| Mali | `lieppos/dev21` | `c7cb71b6a3a36c095f55e306d05e840bf81c1eb0` |
| Camera | `lieppos/dev21` | `7286106471dba28f8a4224e70a45be3c8d5505be` |
| Connectivity | `lieppos/dev21` | `1a9a0c3231c7859eb8595a8236aa40214a7076c7` |
| MET | `lieppos/dev21` | `c9b8a7d47a28d6e8362e667b629739adf79090e8` |
| Nested imgsensor | `lieppos/dev21` | `d44461a5f43f1db7e8664179cb3248ebf84ce9d3` |

Kernel code commits:

- `16e01f73888dd7a91a5193bfdf89135fb6a70e7d` ufs teardown/btag
- `1b073273aab7ce3114030df6a76a50079ca16168` rpmb HMAC failure
- `1dbc05e419afb2aa8f8a2d0eb4175b60c19b2132` hynitron lifetime
- `9be7125364d659652ba24e5b330394ffe59e1fb2` tiny2c thermal power/probe
- `015a306a538f2d1871fb53af32be6a78b464522e` vtdr6115 panel
- `d149703e5388032907dd17215a86ab17f6141504` dev21 config

Camera recipe commit: `7286106471dba28f8a4224e70a45be3c8d5505be`.

## Build identity

- Release: `6.1.115-mt6878-gq5012bf1-dev21+`
- Output: `/mnt/<reference-ssd>/lieppos-builds/c21-dev21`
- Image: 30,116,352 bytes, SHA-256
  `9285fbacfe20bc6a0f63d1c1780c40d808ea4eb7fde7ce2a923935491ec26475`
- vmlinux SHA-256:
  `f541258219131c7ac961e27500768e43baa97c38b42ba488f15665f800dc30be`
- System.map SHA-256:
  `04d8ec4ad0d9df36aff610249f5f19f8340572e4736ec348ccaee26a1cd29d2b`
- Module.symvers: 19,759 entries, SHA-256
  `6179ad70a8a9bd5aa5ae25e901b71ff2d4dedca55ee6c188d2246f5de5ce14a2`
- 708 in-tree + 33 external = 741 modules
- 738 production + 3 test/helper
  (`kheaders`, `cam-ut`, `mali_dmabuf_test_mt6878_r44`)
- Vermagic mismatches 0; final strict modpost diagnostics 0;
  unresolved module names 0.

Trusted DT:
- `mt6878.dtb` `b32509922b7cd6ef4eb53a7feb0f15d61ca1f50a9f1eae17c8cf7c866d8310db`
- `gq5012bf1-board.dtbo` `e9896d333cd3be163a890df6eed525fe6770dd73a6e0c4e635eb7994e8459c8a`
- `CONFIG_GQ5012BF1_PENDING_BOARD_DT` is not set.

## Storage safety

The build used `/mnt/<reference-ssd>` (authorized ext4), not Btrfs.  At final
validation `/home` had 113 GiB statfs free and approximately 4.74 GiB Btrfs
metadata free, below the 6 GiB heavy-build restart gate.  No heavy Btrfs
operation was attempted.  No other agent's output was deleted.

## Exact continuation

If a future session is authorized to integrate Android boot artifacts:

1. Read `staging/evidence/dev21/BOOT-READINESS.txt` first.
2. Do not rebuild for no reason; the 741-module dev21 set is already complete.
3. Obtain an authorized boot/init_boot/vendor_boot and modules.load artifact.
4. Compare its first-stage module names against the source-backed chain:
   `ufs-mediatek-mod -> ufs-mediatek-dbg, irq-dbg, blocktag, rpmb`,
   `ufs-mediatek-dbg -> aee_aed, irq-dbg, mrdump`, `blocktag -> mrdump`.
5. If placement is missing, record the exact missing name and do not invent a
   load order.
6. Before any board enablement, resolve trusted UVC switch/VBUS, tiny2c,
   panel/touch and other `[X]` properties from authorized evidence.

Next file to read: `src/LIEPPOS-C21-HANDOFF.md`.

Next command from the workspace root:

```sh
cd /home/<user>/kernel-work/noOEM09/dk12
cat staging/evidence/dev21/BOOT-READINESS.txt
cat staging/evidence/dev21/MODULE-DEPENDENCY-AUDIT.txt
```

Do not access `/home/<user>/<protected-tree>/`, the phone, ADB, fastboot,
RPMB/TEE hardware, or any flashing path.  Hardware validation remains 0.
