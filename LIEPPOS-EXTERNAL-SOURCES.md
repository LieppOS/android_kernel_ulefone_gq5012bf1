# LieppOS external kernel-module source pins

The Target C kernel is the Git worktree in this repository.  Four vendor-module
source trees are kept as sibling source repositories so that no build depends on
an unpinned mutable directory.

| Component | Source repository | Branch / commit | Contents |
|---|---|---|---|
| Mali r44p1 GPU | `../../gpu` | `lieppos/dev19` / `35a788787cb10955fad86099d6a711686c84567f` | NothingOSS `580d08eac018` + four-file `c3d070e6155c` graft; source-owned `build-mali-r44.sh` recipe |
| Connectivity | `../../connectivity` | `lieppos/dev19` / `1a9a0c3231c7859eb8595a8236aa40214a7076c7` | vendor subset plus non-WMT `nic_rx.c` build fix |
| Camera/ISP | `../../mtkcam` | `lieppos/dev19` / `57c9f60e65d772844dd3cd4bdba3c4fb6704f953` | Campaign 16/17 camera tree plus CCU cammainpwr lifetime fix and `camsys/build-isp7sp.sh` |
| Camera imgsensor subrepo | `../../mtkcam/imgsensor/src-v4l2` | `lieppos/dev19` / `d44461a5f43f1db7e8664179cb3248ebf84ce9d3` | nested source repository; ignored by the outer vendor `.gitignore`, explicitly pinned |
| MET v3 | `../../met_drv_v3` | `lieppos/dev19` / `c9b8a7d47a28d6e8362e667b629739adf79090e8` | authorized local MET v3 drop; no vendor Git metadata |

The source kernel used for the Campaign 19 `Image` is
`a5084627a4d0b57acd751d0eee822d9d62fb235c` on `lieppos/dev19`.  The documentation
commit that contains this table is intentionally later and is not a build
input.

## Mali build contract

The GPU source is built out of tree against this kernel with the source-owned
recipe:

```sh
KERNEL_SRC="$PWD/src" \
OUT_DIR=/mnt/<reference-ssd>/lieppos-builds/c19-dev19/out \
TOOLCHAIN_BIN=/home/<user>/kernel-work/platform-batch-04-cleancheck/ws/prebuilts/clang/host/linux-x86/clang-r487747c/bin \
  ../gpu/build-mali-r44.sh
```

The recipe refuses a visible `.config` and generated `auto.conf` with different
compiler/KASAN state, uses the canonical clang configuration, builds all four
modules against the target ABI, and checks their vermagic.

Campaign 19 established that the empty `-DKASAN_SHADOW_SCALE_SHIFT=` is normal
under `CONFIG_KASAN_HW_TAGS`; the canonical C15 object command also contains it.
The Campaign 18 failure came from mismatched generated build inputs: the Make
side had the HW-tags mode (empty shift) while the compiler side selected the
generic-KASAN branch, and the compiler was host clang 23 rather than the pinned
Android clang 17.  The repair was to regenerate both sides from
`staging/kernel/config.dk17`, not to hardcode a KASAN shift or disable warnings.

This produces `mali_kbase_mt6878_r44`, `mali_mgm_mt6878_r44`,
`mali_prot_alloc_mt6878_r44` and `mali_dmabuf_test_mt6878_r44`.  Their export
name sets match stock; their core-symbol import CRCs are generated from this
Target C configuration, not copied from the stock GKI build.

## Camera imgsensor headers

The outer `mtkcam` vendor repository ignores `imgsensor/`, so the nested
`imgsensor/src-v4l2` repository and the `imgsensor/inc` headers are separate
source inputs.  Campaign 16 restored `imgsensor-user.h` and
`kd_imgsensor_define_v4l2.h` from an authorized vendor checkout with the same
`133c6c1` tree and force-added them to `mtkcam`:

| File | SHA-256 |
|---|---|
| `imgsensor/inc/imgsensor-user.h` | `230cd5d1b9839b60df58f86c6797c732b2651448c95976b6590cff483a2933c2` |
| `imgsensor/inc/kd_imgsensor_define_v4l2.h` | `5eadff0c97586a049a5acfca6d380f3feea68dd5c19a991a32aa23d0912a9a7d` |

Without those two files the camera modules do not compile.  They are GPL-2.0
vendor headers and are now tracked instead of depending on a mutable build
overlay.

## Camera provider closure

Campaign 17 selects the stock GQ5012BF1 camera provider family only:

| Component | Source | Selected module |
|---|---|---|
| CCU | `mtkcam/ccusys` | `mtk_ccuv.ko` |
| camsys | `mtkcam/camsys/isp7sp` | `mtk-cam-isp7sp.ko` |
| depth engine | `mtkcam/mtk-dpe/isp_7sp` | `camera_dpe_isp7sp.ko` |
| imgsensor | `mtkcam/imgsensor/src-v4l2` | `imgsensor.ko`, `imgsensor-glue.ko` |
| EEPROM | `mtkcam/cam_cal/src_v4l2/custom` | `camera_eeprom_v4l2.ko` |

`camsys/Kbuild` uses the kernel symbols
`CONFIG_MTK_CAMERA_CAMSYS_7SP`, `..._7S` and `..._7_1` with the deterministic
priority ISP7SP > ISP7S > ISP7.1.  GQ5012BF1 sets only `_7SP=y`.
`CONFIG_MTK_CCU_RPROC` remains the external `mtk_ccuv` enable; the competing
in-tree `mtk_ccu.ko` is gated by `CONFIG_MTK_CCU_RPROC_LEGACY` and stays off.

The selected external build is driven by the source-owned recipe after the
provider modules exist:

```sh
KERNEL_SRC="$PWD/src" \
OUT_DIR=/mnt/<reference-ssd>/lieppos-builds/c19-dev19/out \
TOOLCHAIN_BIN=/home/<user>/kernel-work/platform-batch-04-cleancheck/ws/prebuilts/clang/host/linux-x86/clang-r487747c/bin \
  ../mtkcam/camsys/build-isp7sp.sh
```

Build order is `ccusys`, `imgsensor`, `mtk-dpe`, `cam_cal`, then the
`camsys` aggregate.  The recipe reads provider `Module.symvers` from the
actual Kbuild output root, excludes the aggregate and disabled ISP7S/ISP7.1
families, and removes provider symbols already present in the target
`Module.symvers` so mixed-tree CCU exports are not passed twice.

Campaign 19 also repaired the stale aggregate `.mod` defect: the file listed
only the final `mtk-cam-isp7sp.o` while its `.mod.cmd` listed all 44 objects.
Modpost therefore saw no per-object `#SYMVER` records for the five AOV/seninf
exports and emitted zero CRCs.  The recipe detects that exact stale shape and
regenerates only the aggregate module metadata.  The resulting CRCs are
source-generated; they are not hand-edited or copied.

The full C15 `Module.symvers` contains module exports as well as built-in
exports; using it as an extra-symbols input to a narrowed external build can
produce duplicate-export diagnostics for unrelated net/nfc/rfkill symbols.
That is an input-set problem, not a camera provider conflict.  Do not silence
it with `KBUILD_MODPOST_WARN`.

## External module rebuilds

Connectivity, MET and camera modules are built with relative `M=` paths from the
kernel source directory.  With an `O=` directory outside the source tree, use
`M=../<vendor-repo>/<module>`: Kbuild reads source from the sibling repository
but writes objects and `Module.symvers` below the external output root.  The
source commits above are the inputs; output directories are never accepted as
source evidence.

Campaign 19 rebuilt 33 external modules against the dev19 target ABI: four
Mali, nine camera/ISP, nine connectivity, and eleven MET modules.  The build
order matters because `KBUILD_EXTRA_SYMBOLS` must point at freshly generated
provider `Module.symvers` files:

1. connectivity: `conninfra`, `connfem`, `wlan/adaptor/build/connac2x`,
   `wlan/core/gen4m`, `bt/mt66xx/6878`, GPS data-link/power/SCP, FM radio.
   The stock WLAN project inputs are
   `WLAN_CHIP_ID=6878 MTK_COMBO_CHIP=CONNAC2X2_SOC7_0 MTK_ANDROID_WMT=y
   CONFIG_MTK_COMBO_WIFI_HIF=axi`; without them the target silently selects
   the generic MT7961/PCIe/non-WMT feature set.  Connectivity modules that
   ship only `Kbuild` need an empty `Makefile` in the external output directory
   for the strict modpost include; this is generated output metadata, not a
   source or symbol substitute.
2. MET: `met_drv_v3`, then the ten `met_api/*` modules.
3. camera: `ccusys`, `imgsensor/src-v4l2`, `mtk-dpe`,
   `cam_cal/src_v4l2/custom`, then the `camsys` aggregate with provider
   symvers, then the recipe for `mtk-cam-isp7sp`.

Do not pass a module's own `Module.symvers` in `KBUILD_EXTRA_SYMBOLS`, and do
not pass symbols already present in the target `Module.symvers`.  Rebuild
against the new target ABI when the kernel release or provider CRCs change;
do not rely on source-tree dev15 `.ko` files as inputs.

The in-tree `Image` and module build additionally required
`DEVICE_MODULES_PATH="$PWD/src"`; without it vendor Makefiles lose
`mt-plat/aee.h` and `mtk-sp-spk-amp.h` include paths.  The in-tree CCU consumers
(`mtk-vmm-isp71-regulator.ko` and `mtk-mmdvfs-ccu.ko`) import the external
`mtk_ccuv` provider, so the final strict in-tree modpost used a mixed-tree
staging directory containing the target vmlinux exports plus the freshly
generated external CCU exports.  This is a provider input, not
`KBUILD_MODPOST_WARN`.
