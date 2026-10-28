# Campaign 17 source handoff

This is the source-owning handoff for the GQ5012BF1 / MT6878T kernel work.  It
records exact revisions and contracts; it is not a hardware-validation claim.

## Revisions

| Repository | Branch | Commit | Parent / base |
|---|---|---|---|
| `noOEM09/dk12/src` | `lieppos/dev17` | `c933d3ae32807025aaa88fdc8c2eafb22f66a135` | `446ad5e4ee5b31a8f9ae8b3d92c6a788e20f2a57` (`lieppos/dev16`) |
| `noOEM09/dk12/mtkcam` | `lieppos/dev17` | `710b46f44046b21afe3b13f60b06c8c809316e65` | `fa1e6f372940c47bcd0041824afdce7802dbec3d` plus camsys selection |
| `noOEM09/dk12/mtkcam/imgsensor/src-v4l2` | nested pin | `d44461a5f43f1db7e8664179cb3248ebf84ce9d3` | unchanged from Campaign 16 |

No push was performed.  No dev17 Image exists.

## Configuration fragment

`arch/arm64/configs/gq5012bf1.config` is the reproducible device fragment.
Apply it after `mgk_64_k61_defconfig`:

```sh
make O=out ARCH=arm64 LLVM=1 LLVM_IAS=1 \
  DEVICE_MODULES_PATH="$PWD/src" MTK_PLATFORM=mt6878 \
  mgk_64_k61_defconfig
scripts/kconfig/merge_config.sh -O out out/.config \
  src/arch/arm64/configs/gq5012bf1.config
make O=out ARCH=arm64 LLVM=1 LLVM_IAS=1 \
  DEVICE_MODULES_PATH="$PWD/src" MTK_PLATFORM=mt6878 olddefconfig
```

Key selections:

```text
CONFIG_TRUSTKERNEL_TEE_SUPPORT=m
CONFIG_TRUSTKERNEL_TEE_FP_SUPPORT=m
CONFIG_TRUSTKERNEL_TEE_RPMB_SUPPORT=m
CONFIG_TRUSTONIC_TEE_SUPPORT=n
CONFIG_TRUSTONIC_TRUSTED_UI=n
CONFIG_MICROTRUST_TEE_SUPPORT=n
CONFIG_MTK_TEE_GP_SUPPORT=n

CONFIG_MTK_CCU_RPROC=m
CONFIG_MTK_CCU_RPROC_LEGACY=n
CONFIG_MTK_CCU=n
CONFIG_MTK_V4L2_IMGSENSOR=m
CONFIG_MTK_IMGSENSOR=n
CONFIG_MTK_CAMERA_CAMSYS_7SP=y
CONFIG_MTK_CAMERA_CAMSYS_7S=n
CONFIG_MTK_CAMERA_CAMSYS_7_1=n
CONFIG_MTK_CAMERA_DPE_ISP7SP=m
CONFIG_MTK_CAMERA_DPE_ISP7S=n
CONFIG_MTK_CAMERA_DPE_ISP7=n
CONFIG_VIDEO_MTK_ISP_IMGSYS_7SP=m
CONFIG_VIDEO_MTK_ISP_IMGSYS_7S=n
CONFIG_MTK_CAMERA_MEM_SUPPORT=n
```

## RPMB / TEE contract

`rpmb-mtk.c` no longer requires Trustonic:

- Trustonic GP/DCI code and the netlink proxy are compiled only under
  `CONFIG_TRUSTONIC_TEE_SUPPORT`.
- The non-Security-OS `MAX_RPMB_REQUEST_SIZE` fallback is 4096 bytes, matching
  the stock `rpmb-mtk.ko` read-size bound.
- The stale eMMC non-multi-block `param->key` / `param->data` references are
  corrected to `keybytes` / `databytes`.

Focused build evidence:

| Item | Result |
|---|---|
| `rpmb-mtk.o` compile | 0 errors, 0 warnings |
| strict `scripts/mod/modpost -m -E` | pass against genuine C15 provider CRCs |
| module metadata | `depends: ufs-mediatek-mod,rpmb` |
| TEE imports | none (`mCrpModule`, `mobicore`, Trustonic, tkcore all absent) |
| genl imports | none |
| focused `.ko` SHA-256 | `d20030906552f2fd67de342e6821c1e0b55382c27190a598519cb2068ddb9092` |
| vermagic | `6.1.115-mt6878-gq5012bf1-dev15+` |

First-stage source contract: load `rpmb.ko`, then `ufs-mediatek-mod.ko`, then
`rpmb-mtk.ko`.  `tkcore.ko`, `tkcore_drv.ko` and `ffa_v10.ko` are the
TrustKernel stack and are not direct `rpmb-mtk` dependencies.  Do not add a
`mcDrvModule-ffa` module or dependency.

## Camera provider contract

Selected stock family:

- `mtkcam/ccusys` -> `mtk_ccuv.ko`
- `mtkcam/camsys/isp7sp` -> `mtk-cam-isp7sp.ko`
- `mtkcam/mtk-dpe/isp_7sp` -> `camera_dpe_isp7sp.ko`
- `mtkcam/imgsensor/src-v4l2` -> `imgsensor.ko`, `imgsensor-glue.ko`
- `mtkcam/cam_cal/src_v4l2/custom` -> `camera_eeprom_v4l2.ko`

Excluded competing providers: in-tree `ccu.ko`, `mtk_ccu.ko`,
`imgsensor_isp6s.ko`, ISP7S/ISP7.1 camsys, in-tree ISP7S DPE/imgsys,
`camera_pda.ko`, `camera_mem.ko`.

Source enforcement:

- `CONFIG_MTK_CCU_RPROC` remains the external `mtk_ccuv` enable;
  `CONFIG_MTK_CCU_RPROC_LEGACY` gates the in-tree `mtk_ccu.o`.
- `mtkcam/camsys/Kbuild` selects exactly one family with priority
  ISP7SP > ISP7S > ISP7.1.
- `pda/Makefile` no longer builds the redundant standalone `camera_pda.ko`.

Focused evidence: the camsys build compiled/linked only the ISP7SP object; the
DPE `modules.order` contained only `camera_dpe_isp7sp.ko`.  A scoped strict
modpost over the actual `mtk-cam-isp7sp.o` imports resolved
`mtk_ccu_rproc_ipc_send` to `mtkcam/ccusys/mtk_ccuv` and generated
`depends: ...mtk_ccuv` with 0 errors.  It emitted five AOV seninf
export-version warnings; no CRC was fabricated and no warning was suppressed.
The normal narrowed build with the bundled full C15 `Module.symvers` still
reports unrelated duplicate exports; that input-set issue must be fixed by the
build owner, not by `KBUILD_MODPOST_WARN`.

## Board DT profiles

- `arch/arm64/boot/dts/mediatek/gq5012bf1-board.dts` - TRUSTED profile.
  Only independently authenticated compatibles/platform properties; every
  numeric path with unknown wiring is `disabled`.
- `arch/arm64/boot/dts/mediatek/gq5012bf1-board-pending.dts` -
  PROVENANCE_PENDING Campaign 16 offline-copy values.  Built only with
  `CONFIG_GQ5012BF1_PENDING_BOARD_DT=y` (default `n`).

Build evidence:

| Artifact | Size | SHA-256 |
|---|---|---|
| trusted DTBO | 3,480 B | `e9896d333cd3be163a890df6eed525fe6770dd73a6e0c4e635eb7994e8459c8a` |
| trusted merged DTB | 339,846 B | `b9161b2eb946123c1fd62599b354f08454e249df74e3e61891966431d5ffddd0` |
| pending DTBO | 14,206 B | `a34b20311d56a2a2c7387724e1e5ca857f7b0bdd19217e4b71276f0d9a546583` |
| pending merged DTB | 340,368 B | `a87d6f491fa26f932af0dc4c4c443368f5b25a1897314f48540808367a00a12c` |

Trusted compatibles independently read from stock module ELF OF tables: panel
`hx,vtdr6115,cmd,120hz,ky`; touch `focaltech,fts`; fingerprint
`mediatek,madev_finger`; YFT `mediatek,yft_finger`, `mediatek,yft_devices`,
`mediatek,yft_tpd`, `yft-gpio-keys`, `mediatek,yft_tiny2c_usb`,
`mediatek,yft_camplight`; speaker amplifier `mediatek,speaker_amp`.

All numeric GPIO/bus/regulator/camera/power values from the offline copy remain
`PROVENANCE_PENDING` and disabled.

## Build and storage state

- Full Target C build **not run**: Btrfs metadata free is 6,338,723,840 B
  (~5.90 GiB), below the 6 GiB restart gate.
- `out/Module.symvers` is restored to the C15 file, 19,812 lines,
  SHA-256 `f8cd5720381345f68ccc68d9609a976b36f581b2a58a8fca379084722e7d5b69`.
- Current Image is still the C15 artifact, 30,116,352 B,
  SHA-256 `6c36c5d78ca4ead996bed07e4bb89db68282f73ca1cbaf87a13703cc1f4e9885`;
  no dev17 Image or dev17 release string exists.
- The 754-module dev15-ABI staging set is preserved.  No dev14/dev15 object
  mixing and no prebuilt-module substitution occurred.

## Ownership boundaries

- Claude owns build/BTF/provenance infrastructure and offline boot packaging.
  This handoff supplies source facts; it does not change that infrastructure.
- Codex owns FPSGO/C2PS source.  The correct Codex config symbol is
  `CONFIG_NONLINEAR_FREQ_CTL`; do not substitute an invented symbol.
- Preserve the C16 external source pins, licenses, revisions and the 754-module
  staging set.
- No phone access, no module loading, no TEE/fingerprint secure-world run, no
  RPMB hardware command, no camera/panel/touch activation, no push and no
  deployable image.

## Open blockers

1. Full modpost/build closure waits for metadata headroom above 6 GiB.
2. The camsys external modpost input-set duplicate exports must be resolved by
   the build owner using a scoped symvers set, not by disabling modpost.
3. Board numeric values from the offline stock DT copy remain
   `PROVENANCE_PENDING` until the access lineage is authorized or the values
   are independently re-derived.
4. No hardware validation exists for display, touch, fingerprint/TEE, camera,
   charger/regulator, USB-PD or fuel gauge.
