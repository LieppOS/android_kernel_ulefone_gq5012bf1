# LieppOS Campaign 19 handoff (dev19)

Campaign 19 turned the preserved Campaign 18 source baseline into a
current-target, strict-modpost kernel/module build.  It is an offline source
and build milestone only.  Hardware validation remains 0; there is no
deployable image, boot image, or flashing instruction here.

## Revision contract

| Repository | Branch | Commit used for the dev19 build |
|---|---|---|
| Kernel `src` | `lieppos/dev19` | `a5084627a4d0b57acd751d0eee822d9d62fb235c` |
| Camera `mtkcam` | `lieppos/dev19` | `57c9f60e65d772844dd3cd4bdba3c4fb6704f953` |
| Mali `gpu` | `lieppos/dev19` | `35a788787cb10955fad86099d6a711686c84567f` |
| Connectivity | `lieppos/dev19` | `1a9a0c3231c7859eb8595a8236aa40214a7076c7` |
| MET v3 | `lieppos/dev19` | `c9b8a7d47a28d6e8362e667b629739adf79090e8` |
| imgsensor (nested) | `lieppos/dev19` | `d44461a5f43f1db7e8664179cb3248ebf84ce9d3` |

The documentation commit containing this file is later than the source commit
used for the Image.  The Image revision remains `a5084627...`; it does not
include documentation-only changes.

## Build identity

- Authorized output root:
  `/mnt/<reference-ssd>/lieppos-builds/c19-dev19/out`
- Canonical config: `staging/kernel/config.dk17` copied to the output, SHA-256
  `b73d52127559108bfd8a8ff8b253a6abc2e2107cedb5910327bd48bd30697093`
- Toolchain: Android clang/LLD 17.0.2,
  `clang-r487747c`, `.../prebuilts/clang/host/linux-x86/clang-r487747c/bin`
- `CONFIG_LOCALVERSION="-mt6878-gq5012bf1-dev19"`
- Generated release: `6.1.115-mt6878-gq5012bf1-dev19+`
- `CONFIG_GQ5012BF1_PENDING_BOARD_DT` is not set.
- TrustKernel is selected; Trustonic and Microtrust are not set.

### Target artifacts

| Artifact | Size / SHA-256 |
|---|---|
| `arch/arm64/boot/Image` | 30,116,352 bytes; `bd635881ab14d40390ea69da1aed0970866c979eaee5d536e47e05d1aa74deb0` |
| `vmlinux` | `b21956790bea532ec13318194dd5f5e44b2bae911b6bcd601e7b739484584ce7` |
| `System.map` | `da12d3d3fe9ddb36defca4762ea5cac610716f8898da2da12b0e593dfd1d9213` |
| `Module.symvers` | 19,759 entries; `52f560c115e5a742014f85bf687a0306328e35482e2077c50bede5d0173809cc` |
| In-tree modules | 708 `.ko` |
| External modules | 33 `.ko` (4 Mali, 9 camera/ISP, 9 connectivity, 11 MET) |
| Total module manifest | 741 `.ko`, all vermagic `6.1.115-mt6878-gq5012bf1-dev19+` |

The manifest is `staging/modules-targetC-dev19/MANIFEST.sha256`.  The three
identified test/helper modules are listed separately in
`staging/modules-targetC-dev19/TEST-OR-HELPER.txt`; the 754-module dev15
manifest was not overwritten or relabelled.

## Work-package results

### A. Revisions, build identity, ledger

- Created `lieppos/dev19` in the kernel, camera, Mali, connectivity, MET, and
  nested imgsensor repositories at the dev18 heads, then added only the
  source-owned fixes and recipes described below.
- The build uses an authorized ext4 output volume.  The Btrfs source workspace
  was not used for the full build; its metadata free remained about 5.86 GiB,
  below the 6 GiB restart gate.
- No `KBUILD_MODPOST_WARN`, warning suppression, fake CRC, stale dev15
  `Module.symvers`, or module copied forward from dev15 was used.

### B. Mali r44p1

- Root cause: the Campaign 18 failure was a build-input inconsistency, not a
  Mali source requirement for a non-empty KASAN shift.  `-DKASAN_SHADOW_SCALE_SHIFT=`
  is normal under `CONFIG_KASAN_HW_TAGS`; the canonical C15 object command has
  it empty too.  In the failing state the Make-side generated config selected
  HW tags/empty shift while the compiler-side headers selected the generic
  KASAN branch, and the compiler was host clang 23.  Restoring the canonical
  `config.dk17` and regenerating `auto.conf`/`autoconf.h` with Android clang 17
  fixed it.
- All four Mali modules compile, link, and pass strict modpost against the
  dev19 ABI:
  `mali_kbase_mt6878_r44.ko`, `mali_mgm_mt6878_r44.ko`,
  `mali_prot_alloc_mt6878_r44.ko`, `mali_dmabuf_test_mt6878_r44.ko`.
- The source-owned recipe is `gpu/build-mali-r44.sh`; it validates compiler and
  generated-config consistency and checks vermagic.

### C. Camera AOV/seninf symbol versions

The five warnings were exports of `mtk-cam-isp7sp.ko`, not missing imports:
`mtk_cam_seninf_s_aov_param`, `mtk_cam_seninf_aov_runtime_suspend`,
`mtk_cam_seninf_aov_runtime_resume`, `mtk_cam_seninf_aov_reset_sensor`, and
`mtk_cam_seninf_aov_sensor_set_mclk`.

The stale `isp7sp/cam/mtk-cam-isp7sp.mod` listed only the aggregate object while
its `.mod.cmd` listed all 44 objects.  Modpost therefore saw no per-object
`#SYMVER` records and emitted zero CRCs.  Regenerating the aggregate metadata
produced the source-generated CRCs `0x46748b89`, `0xf1e362fe`, `0x98571bf7`,
`0x72be2734`, and `0x095e66b0`.  The source-owned recipe
`mtkcam/camsys/build-isp7sp.sh` detects this exact stale shape, filters provider
symbols already present in the target `Module.symvers`, and verifies that the
five CRCs are non-zero.

The camera provider closure is current-target: `mtk_ccuv`, `imgsensor`,
`imgsensor-glue`, `camera_dpe_isp7sp`, `camera_eeprom_v4l2`, `ccd_rpmsg`,
`mtk_ccd_remoteproc`, `mtk-cam-isp7sp`, plus the `cam-ut` test module.  The main
module's vermagic is dev19 and its AOV CRCs match the regenerated metadata.

### D. Source fixes

- `src`: `geniezone/tz_system.c` now compiles the optional GlobalPlatform
  TEE-client bridge only for Trustonic/Microtrust.  GQ5012BF1 selects
  TrustKernel, whose headers do not define the TEEC context/session API.  This
  removed the `tee_client_api.h` failure without changing the selected TEE
  provider.  Commit `a5084627...`.
- `mtkcam`: `ccusys/mtk_ccu_isp71.c` releases the `of_find_device_by_node`
  cammainpwr reference on probe/remove and clears `poweron` when IRQ setup
  fails.  Commit `dead8ab6...`; the recipe/output work is in `57c9f60...`.
- `gpu`: no Mali C source change was required after correcting the build inputs;
  the recipe is commit `35a7887...`.
- `connectivity`: `wlan/core/gen4m/nic/nic_rx.c` declares the RFB cursor only
  for `CFG_RFB_TRACK`.  Commit `1a9a0c3...`.
- The earlier Campaign 18 RPMB/UFS, fingerprint, DRM/MSYNC2, MT6375, and pwrap
  fixes remain in the baseline and were not redone.

### E. Full target build

The exact effective command used after preparing the canonical output was:

```sh
make -C src O=/mnt/<reference-ssd>/lieppos-builds/c19-dev19/out \
  ARCH=arm64 LLVM=1 LLVM_IAS=1 HOSTCC=gcc \
  DEVICE_MODULES_PATH="$PWD/src" MTK_PLATFORM=mt6878 \
  -j8 Image modules
```

Important build-input facts:

- `DEVICE_MODULES_PATH` is mandatory for this vendor tree; without it,
  `mt-plat/aee.h` and `mtk-sp-spk-amp.h` disappear from vendor include paths.
- The first in-tree modpost stops on the two legitimate consumers of the
  external CCU provider, `mtk-vmm-isp71-regulator.ko` and
  `mtk-mmdvfs-ccu.ko`.
- `mtk_ccuv.ko` was built against the new vmlinux export set.  A strict
  bootstrap modpost over the other 706 in-tree modules generated source CRCs;
  duplicate vmlinux lines were filtered from that provider input.
- The final in-tree modules were built with `KBUILD_MIXED_TREE` pointing at a
  staging directory containing the target vmlinux exports plus the freshly
  generated external CCU exports.  This is a provider input, not warning
  suppression.  Strict modpost returned 0 with no diagnostics.
- `make Image` then linked `vmlinux`, `System.map`, and `Image`.

The global `make dtbs` target was not used: it still reaches unrelated mt8192
board sources and is not a blocker for this device target.  The trusted DTBO
was rebuilt directly instead.

### F. Connectivity, camera, MET, and external modules

- Connectivity build uses the stock project inputs
  `WLAN_CHIP_ID=6878`, `MTK_COMBO_CHIP=CONNAC2X2_SOC7_0`,
  `MTK_ANDROID_WMT=y`, and `CONFIG_MTK_COMBO_WIFI_HIF=axi`.  Without them the
  tree silently selects the generic MT7961/PCIe/non-WMT feature set and fails
  on unrelated code paths.
- Nine connectivity modules built and passed strict modpost: conninfra,
  connfem, WLAN adaptor, WLAN core (`wlan_6878_axi.ko`), BT, GPS data-link,
  GPS power, GPS SCP, and FM radio.
- Camera, MET, and Mali modules were rebuilt from source against the dev19
  output; all 33 external modules carry dev19 vermagic and none comes from the
  source-tree dev15 artifacts.

### G/H/I/J. Runtime source areas

- TEE/GenieZone compile closure fixed as above.
- Camera CCU lifetime cleanup fixed as above; no hardware activation or
  runtime validation was attempted.
- Mali runtime source needed no additional unproven change after the build
  input correction.
- RPMB/UFS, DRM/MSYNC2, charger, and pwrap changes remain Campaign 18 source
  fixes; no hardware test was performed.
- Any further runtime claims require a separately authorized device test.

### K. Trusted device tree

The trusted DTBO was rebuilt with the output tree's `dtc`:

```text
sha256 e9896d333cd3be163a890df6eed525fe6770dd73a6e0c4e635eb7994e8459c8a
```

The pending board profile remains disabled.  The dtc unit-address warnings in
the trusted overlay source are pre-existing and do not alter the artifact hash.

### L. Storage and safety

- Btrfs metadata free at the end of the build remained about 5.86 GiB.
- External output free space was about 177 GiB at final validation.
- No balance, defrag, broad dedup, destructive cleanup, or protected-path
  access was performed.
- No phone, ADB/fastboot, flashing, module loading, TEE/RPMB hardware command,
  camera/display/touch/charger activation, or hardware validation was
  performed.

### M. Handoffs

#### Claude-owned build systems

- The dev19 kernel build remains compatible with the canonical config and does
  not require a new build engine.
- If the unified build engine is used, it must pass
  `DEVICE_MODULES_PATH=<kernel source>` and `MTK_PLATFORM=mt6878`.
- For in-tree modpost with external providers, stage a mixed vmlinux symvers
  input containing the target vmlinux exports plus the generated `mtk_ccuv`
  exports; do not use `KBUILD_MODPOST_WARN`.
- The source build contract and recipes are in
  `gpu/build-mali-r44.sh` and `mtkcam/camsys/build-isp7sp.sh`.

#### Codex-owned FPSGO/C2PS

- No FPSGO/C2PS source was edited in Campaign 19.
- The FPSGO/C2PS modules were left to the in-tree configuration and built from
  the same dev19 kernel ABI.
- Do not force-enable a kernel config or add a private module ABI to work
  around a separate LSM policy; any such change needs the owning session.

## Evidence

- `staging/evidence/dev19/c19-build-image-modules-2.log`: first full attempt,
  showing the missing `DEVICE_MODULES_PATH` include failure.
- `staging/evidence/dev19/c19-build-image-modules-3.log`: full build reaching
  the provider-dependent modpost stage.
- `staging/evidence/dev19/c19-geniezone-trustkernel-compile-1.log`: GenieZone
  fix compile.
- `staging/evidence/dev19/c19-in-tree-modules-mixed-1.log`: strict mixed-tree
  modpost and 708 module links.
- `staging/evidence/dev19/c19-image-build-1.log`: Image link.
- `staging/evidence/dev19/c19-vmlinux-modpost-1.log`: vmlinux export stage.
- `staging/evidence/dev19/c19-in-tree-bootstrap-modpost-1.log`: source-CRC
  bootstrap for the external CCU provider.
- `staging/evidence/dev19/external-logs/`: camera, Mali, connectivity, and MET
  build logs, including provider symvers and the final dev19 summaries.
- `staging/evidence/dev19/c19-trusted-dtbo-dtc-1.log`: trusted DTBO rebuild.
- `staging/evidence/dev19/FINAL-VALIDATION.txt`: authoritative summary of the
  final artifacts, counts, hashes, and which earlier logs are superseded.
- `staging/evidence/dev19/SHA256SUMS`: evidence inventory.
- `staging/modules-targetC-dev19/MANIFEST.sha256`: 741-module current-target
  manifest.

## Remaining limits

- No hardware validation; camera/display/touch/charger/TEE/RPMB activation was
  not attempted.
- No bootable or deployable image was produced.
- The global `dtbs` target still reaches unrelated platforms; only the trusted
  device DTBO was rebuilt.
- The source tree still contains old ignored dev15 module artifacts from prior
  validation.  They are not in the dev19 manifest and are not build inputs.
- The canonical research repository was not present in the accessible
  workspace; a verifiable Git-format research patch is staged at
  `staging/evidence/dev19/KERNEL-DEVELOPMENT-19-STAGED.patch`
  (SHA-256 `10c47d8f695464239119c501fa47b6e88ac52b29c1481f0deb13646a2d4007d4`)
  rather than claiming a canonical research commit.
