# GQ5012BF1 camera provider selection

This file records the Campaign 17 camera provider correction.  It is source
evidence, not a camera-validation claim.

## Stock provider evidence

The authorized stock module set contains exactly one camsys, one CCU and one
imgsensor provider family:

| Role | Stock module |
|---|---|
| CCU/CCUV | `mtk_ccuv.ko` |
| camsys | `mtk-cam-isp7sp.ko` |
| imgsys | `mtk_imgsys_isp7sp.ko`, `mtk_imgsys_v4l2.ko`, `mtk_imgsys_cmdq.ko` |
| depth engine | `camera_dpe_isp7sp.ko` |
| imgsensor | `imgsensor.ko`, `imgsensor-glue.ko` |
| camera EEPROM | `camera_eeprom_v4l2.ko` |
| AIE | `mtk_aie.ko`, `mtk_aie_debug_7sp_2.ko` |
| PDA | `pda_drv_mt6878.ko` |

The stock set has no `mtk_ccu.ko`, `ccu.ko`, `imgsensor_isp6s.ko`,
`mtk-cam-isp7s.ko`, `mtk-cam-isp.ko`, `mtk-imgsys_isp7s.ko`,
`camera_dpe_isp70.ko`, `camera_pda.ko` or `camera_mem.ko`.

## Root cause

The donor configuration enabled two competing provider families at once:

- `CONFIG_MTK_CCU=m` built the legacy in-tree `ccu.ko`, while
  `CONFIG_MTK_CCU_RPROC=m` built both the in-tree `mtk_ccu.ko` and the stock
  external `mtkcam/ccusys/mtk_ccuv.ko`.
- `CONFIG_VIDEO_MTK_ISP_CAMSYS=m` was used by every `mtkcam/camsys` family, so
  the external Kbuild built `mtk-cam-isp7.1`, `mtk-cam-isp7s` and
  `mtk-cam-isp7sp` together.
- `CONFIG_MTK_IMGSENSOR=m` built the legacy in-tree `imgsensor_isp6s.ko` while
  `CONFIG_MTK_V4L2_IMGSENSOR=m` built the stock external `imgsensor.ko`.
- `CONFIG_MTK_CAMERA_DPE_ISP7S=m` and `CONFIG_MTK_CAMERA_DPE_ISP7=m` built
  in-tree DPE variants in addition to the stock external ISP7SP DPE.
- `CONFIG_VIDEO_MTK_ISP_IMGSYS_7S=m` built the competing in-tree ISP7S engine.
- The in-tree PDA Makefile built a standalone `camera_pda.ko` even though the
  stock `pda_drv_mt6878.ko` links the platform `camera_pda.o` into the driver.

These are source/Kconfig composition defects, not missing symbols.

## Source correction

`arch/arm64/configs/gq5012bf1.config` now selects:

```text
CONFIG_MTK_CCU=n
CONFIG_MTK_CCU_RPROC=m
CONFIG_MTK_CCU_RPROC_LEGACY=n
CONFIG_MTK_CCU_MSSV=n
CONFIG_MTK_IMGSENSOR=n
CONFIG_MTK_V4L2_IMGSENSOR=m
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

Source changes that enforce the selection:

- `drivers/remoteproc/Kconfig` adds
  `CONFIG_MTK_CCU_RPROC_LEGACY`, default `n`.  The in-tree `mtk_ccu.ko` is
  gated by that symbol; `CONFIG_MTK_CCU_RPROC` remains the symbol consumed by
  the external `mtkcam/ccusys` Kbuild for `mtk_ccuv.ko`.
- `drivers/remoteproc/Makefile` uses `CONFIG_MTK_CCU_RPROC_LEGACY` for the
  in-tree `mtk_ccu.o`.
- `drivers/media/platform/mtk-isp/Kconfig` defines the three camsys family
  symbols.  `VIDEO_MTK_ISP_CAMSYS` remains the module-enable symbol.
- `mtkcam/camsys/Kbuild` now selects exactly one family with a deterministic
  priority: ISP7SP, then ISP7S, then ISP7.1.  GQ5012BF1 selects ISP7SP.
- `drivers/misc/mediatek/cameraisp/pda/Makefile` no longer builds the
  standalone `camera_pda.ko`; `pda_drv_mt6878.ko` continues to link
  `isp_7sp/camera_pda.o` and `isp_7sp/pda_mt6878.o`.
- The device config disables the legacy in-tree camera provider variants and
  camera_mem.

No compiled ELF `depends=` string was edited.  No stock module was copied into
the source tree.

## Build verification

After applying the config fragment and `olddefconfig`:

- `CONFIG_MTK_CAMERA_CAMSYS_7SP=y`, `_7S=n`, `_7_1=n`.
- `CONFIG_MTK_CCU_RPROC=m`, `CONFIG_MTK_CCU_RPROC_LEGACY=n`.
- `CONFIG_MTK_IMGSENSOR=n`, `CONFIG_MTK_V4L2_IMGSENSOR=m`.
- `CONFIG_VIDEO_MTK_ISP_IMGSYS_7SP=m`, `_7S=n`.
- `CONFIG_MTK_CAMERA_DPE_ISP7SP=m`, `_ISP7S=n`, `_ISP7=n`.

Focused external builds:

- `mtkcam/camsys` compiled and linked only
  `isp7sp/cam/mtk-cam-isp7sp.o`; the build log contains no ISP7.1 or ISP7S
  compile/link for the selected configuration.
- `mtkcam/mtk-dpe` generated `modules.order` containing only
  `camera_dpe_isp7sp.ko`; `camera_dpe_isp7s.ko` is not selected.

The normal narrowed external camsys build, when fed the bundled full C15
`Module.symvers` as extra symbols, reports duplicate exports for unrelated
net/nfc/rfkill/ieee802154 symbols.  That is an input-set problem, not a camera
provider conflict.  No `KBUILD_MODPOST_WARN` workaround was used.

A scoped strict modpost over the actual `mtk-cam-isp7sp.o` imports, using
`vmlinux.o` plus the genuine `mtk_ccuv`, `ccd_rpmsg` and needed external-module
export sets, completed with **0 errors**.  It resolved `mtk_ccu_rproc_ipc_send`
to `mtkcam/ccusys/mtk_ccuv` (CRC `0x8b2fd6a7`) and generated:

```text
MODULE_INFO(depends, "mtk_slbc,system_heap,mtk-cmdq-drv-ext,ccd_rpmsg,aee_aed,
mtk-smi-dbg,clk-common,mtk-mmdvfs-v3,gz_tz_system,mtk-smi,mtk_printk_ctrl,
iommu_debug,mtk-icc-core,mmqos-common,mtk_sec_heap,mtk_ccuv");
```

There were five modpost export-version warnings for AOV seninf symbols exported
by the module itself; no CRC was fabricated and no warning was suppressed.  A
full integrated build remains storage-gated.

## Remaining limits

- No camera hardware, sensor probe, firmware load, CSI, ISP or power test was
  performed.
- The stock four-sensor graph and power sequencing remain disabled in the
  trusted board profile.
- The stock `mtk-cam-isp7sp.ko` dependency metadata names `mtk_ccu_mssv` while
  the shipped provider file is `mtk_ccuv.ko`; the source build uses
  `mtk_ccuv` and does not invent a `mtk_ccu_mssv` provider.
