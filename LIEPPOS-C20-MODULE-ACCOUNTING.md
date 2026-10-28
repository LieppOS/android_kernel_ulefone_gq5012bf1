# Campaign 20 module reconciliation (dev15 -> dev19 -> dev20)

This is an offline inventory reconciliation.  It is not a hardware-validation
claim, a boot image, or a staging instruction.

## Evidence

| Input | SHA-256 |
|---|---|
| `staging/modules-targetC-dev15-full/MANIFEST.sha256` | `eb0611fcae603c1fb321f29efbde7b21b5252fdb51783e24572fdbee13cbd252` |
| `staging/modules-targetC-dev19/MANIFEST.sha256` | `92af098ec375a3fb28732c4ebd3c53882706572c00d6edf1edb0f2f2e635f2ac` |
| `staging/evidence/dev20/MODULE-ACCOUNTING.tsv` | `a72242dc726e2178c66e0c518c9c4c4539a37bf5bff02fbc9107cc9ec0ee4ca4` |
| `staging/evidence/dev20/MANIFEST.sha256` | `3226ae8fe64c35c34056b03c5b0560f5fafa4e65a647161ce10e59ea0fc9f1e5` |
| `staging/evidence/dev20/PRODUCTION-MANIFEST.sha256` | `aff605b23b79e81eb356a2ed28d963ba2c4fa2b23e8efc33ad0c8ad6b6d0426c` |

The machine-readable `MODULE-ACCOUNTING.tsv` has one row for each of the 754
dev15 artifacts.  Paths are normalized only for the known staging-prefix
mapping (`./drivers/...` -> `out/drivers/...`, and the vendor repo prefixes).
The five dev19-only paths are represented by the corresponding renamed or
relocated dev15 rows.

## Exact accounting

| Measure | Count |
|---|---:|
| dev15 artifacts | 754 |
| dev19 artifacts | 741 |
| Net artifact difference | -13 |
| Exact normalized path identities (same path, rebuilt hash) | 736 |
| Exact path missing from dev19 | 18 |
| Exact new paths in dev19 | 5 |
| Intentional net removals | 13 |
| Same driver, renamed output | 1 |
| Same artifact, relocated staging path | 4 |

The 18 exact missing paths are the 13 net removals plus one WLAN output rename
and four Mali path relocations.  The 5 exact new paths are the rename target
and the four Mali relocation targets.  No dev15 artifact is unaccounted for,
and no module hash is reused as an identity claim: all 736 common identities
were rebuilt for dev19, so their hashes differ from the dev15 collection.

## The 13 intentional removals

These are target-selection deletions, not failed production builds.

| dev15 path | Current disposition | Evidence |
|---|---|---|
| `drivers/media/platform/mtk-isp/imgsys/mtk_imgsys_isp7s.ko` | Removed; ISP7S engine not selected | GQ5012BF1 uses ISP7SP (`CONFIG_MTK_CAMERA_CAMSYS_7SP=y`, `CONFIG_VIDEO_MTK_ISP_IMGSYS_7S=n`) |
| `drivers/misc/mediatek/camera_mem/camera_mem.ko` | Removed; provider disabled | No stock camera module imports `camera_mem`; stock module set has no `camera_mem.ko` |
| `drivers/misc/mediatek/cameraisp/dpe/camera_dpe_isp70.ko` | Removed; legacy in-tree DPE | Target uses external `camera_dpe_isp7sp.ko`; `CONFIG_MTK_CAMERA_DPE_ISP7=n` |
| `drivers/misc/mediatek/cameraisp/pda/isp_71/camera_pda.ko` | Removed; legacy ISP7.1 PDA | Target uses `pda_drv_mt6878.ko` |
| `drivers/misc/mediatek/ccu/src/isp6s/ccu.ko` | Removed; legacy ISP6S CCU | Target uses external `mtk_ccuv.ko` |
| `drivers/misc/mediatek/imgsensor/src/isp6s/imgsensor_isp6s.ko` | Removed; legacy in-tree imgsensor | Target uses external `imgsensor.ko` and `imgsensor-glue.ko` |
| `drivers/remoteproc/mtk_ccu.ko` | Removed; legacy in-tree CCU remoteproc | External `mtk_ccuv.ko` is the selected provider; `CONFIG_MTK_CCU_RPROC_LEGACY=n` |
| `drivers/tee/gud/600/MobiCoreDriver/mcDrvModule-ffa.ko` | Removed; Trustonic GUD not selected | Stock uses TrustKernel; `CONFIG_TRUSTONIC_TEE_SUPPORT=n` |
| `drivers/tee/gud/600/TlcTui/t-base-tui.ko` | Removed; Trustonic TUI not selected | `CONFIG_TRUSTONIC_TRUSTED_UI=n` |
| `mtkcam/camsys/isp7_1/cam/mtk-cam-isp.ko` | Removed; legacy ISP7.1 camsys | Target uses ISP7SP |
| `mtkcam/camsys/isp7_1/cam/mtk-cam-plat-util.ko` | Removed; legacy ISP7.1 helper | Target uses ISP7SP |
| `mtkcam/camsys/isp7s/cam_legacy/mtk-cam-isp7s.ko` | Removed; legacy ISP7S camsys | Target uses ISP7SP |
| `mtkcam/mtk-dpe/camera_dpe_isp7s.ko` | Removed; legacy ISP7S DPE | Target uses `camera_dpe_isp7sp.ko` |

## Rename and relocations

`wlan_drv_gen4m_6878.ko` was rebuilt as `wlan_6878_axi.ko` after applying the
stock WLAN inputs (`WLAN_CHIP_ID=6878`,
`MTK_COMBO_CHIP=CONNAC2X2_SOC7_0`, `MTK_ANDROID_WMT=y`,
`CONFIG_MTK_COMBO_WIFI_HIF=axi`).  The module is a production WLAN driver, not
a deletion.

The four Mali artifacts moved from the old flat `vendor/mali/` staging layout
to the full source path.  The basenames and roles are unchanged.

## Explicit Mali verification

All four Mali r44p1 modules are present in the dev19 and dev20 manifests.

| Module | Class | dev20 SHA-256 |
|---|---|---|
| `mali_kbase_mt6878_r44.ko` | production | `a015903c6d1bd6c4cfc341658926c11739b240be740f7a0db9e855f6bfabddf0` |
| `mali_mgm_mt6878_r44.ko` | production | `763c131bd5dd533231ca270f6f95555dda633c1913b9171512d71fc315d06854` |
| `mali_prot_alloc_mt6878_r44.ko` | production | `4307e33a9205b13cf247d38da01f5f4d36bc975b52691cd81340c599a52c60d9` |
| `mali_dmabuf_test_mt6878_r44.ko` | test/helper | `4bf71ae74d0f0ee15736c17cfa903a35c54b4347e941a832e2992f81747da731` |

All four carry `6.1.115-mt6878-gq5012bf1-dev20+` vermagic and passed the Mali
recipe's modpost.  The DMA-buf test exporter is not counted as a production
driver.  Mali was not omitted from either manifest; the dev15 flat path was a
staging-layout difference only.

## Production profile

The 741-artifact engineering manifest classifies as:

- 708 in-tree modules
- 33 external modules (4 Mali, 9 camera/ISP, 9 connectivity, 11 MET)
- 3 test/helper entries excluded from production:
  `kheaders.ko`, `cam-ut.ko`, `mali_dmabuf_test_mt6878_r44.ko`

The production view is therefore **738 modules**, not 741.  The test/helper
entries remain in the engineering manifest because they are reproducible build
outputs, but they are not production drivers and do not inflate the claimed
production count.

## Conclusion

The 754 -> 741 reduction is fully explained by target/provider selection.  No
legitimate production module was silently dropped, all four Mali modules were
present in both manifests, and no source-owned staging repair was required.
Campaign 20 rebuilt the complete 741-artifact set as dev20; the resulting
manifest and production manifest are in `staging/evidence/dev20/`.
