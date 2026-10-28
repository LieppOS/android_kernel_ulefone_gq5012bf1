# LieppOS MT6878 device-kernel source provenance

This tree is the source-owning Git worktree for the LieppOS Ulefone Armor 29 Pro
Thermal (GQ5012BF1, MediaTek MT6878T) device kernel.

## Base and donors

| Input | Immutable identity | Role |
|---|---|---|
| Android common kernel (ACK) | `6b18f0b574ab3267615ae6ce642d5a7c3c21ac09` (`android14-6.1-2024-12`, GKI 12901745) | tracked Git base |
| MediaTek device-module donor | `ca8b550b75e8b49f403f0097e8323c4a930bad79` (`kernel_device_modules-6.1`) | vendor device-module payload |
| NothingOSS MT6878 KDM (GPU/auxiliary) | `957dac185efe46cbf6336b0fff9516d84c8cd78f` | documented GPU/HAL donor for the Mali stack |
| NothingOSS MT6878 GPU modules | `580d08eac018657a993e56b1806407e41a3b9545` with the four-file `c3d070e6155c9f373224fa2b17027d07583c37bc` graft | Mali r44p1 source donor |
| NothingOSS MT6878 ALPS (GPU frequency) | `5e415e059253ce0d2e61c97108e5f5785d71f06f` | GED/gpufreq source donor |

The vendor payload was merged over the ACK base by path.  Where a device-module
path and an ACK path collide, the MediaTek device-module file is authoritative;
this is the same policy used to produce the Campaign 13/14 build tree.  The
Campaign 14-specific edits, including TEE/fingerprint wiring, camera/ISP
Makefiles, the ODM camera identity split, and the flashlight reconstruction,
are recorded in the research repository at
`reverse-engineering/kernel-source-reconstruction/KERNEL-DEVELOPMENT-14/`
(implementation commit `4e297748`, documentation commit `ac3dc23f`).

## What is committed here

Only source and source-required data are committed: C, headers, Kconfig,
Kbuild/Makefiles, DTS/DTSI, documentation, UAPI headers, and small
source-embedded firmware tables shipped by the vendor source.  Build products
(`*.o`, `*.ko`, `*.cmd`, `*.mod`, `*.dtb`, `*.dtbo`), output directories, and
the build `.config` are outside Git and are never accepted as source evidence.

External vendor-module trees are kept as sibling source repositories (or, until
their repositories are created, pinned by the SHA-256 manifest in the Campaign
15 research directory).  They are not silently copied from a mutable output
directory.

## Reproducing the source tree

1. Start from the ACK commit above.
2. Apply/import this repository's commits in order.  The first LieppOS commit is
   the authenticated device-module import; later commits are reviewed source
   integrations.
3. Use `out/` (or another explicit `O=`) for generated files.  Do not reuse a
   foreign output directory: stale `.o`/`.ko` files have no provenance.
4. Build with the kernel configuration and toolchain recorded by the Campaign
   14/15 research manifests.  A build is not evidence of hardware function.

## Campaign 16 source state (historical)

> Campaign 17 supersedes the board-profile part of this section.  The
> Campaign 16 offline-copy board values are preserved in
> `gq5012bf1-board-pending.dts`; the default `gq5012bf1-board.dts` is now the
> trusted profile.  See the Campaign 17 section below.

Campaign 16 starts from the Campaign 15 production commit
`1aa773e5feb354a36d0273da61c7fcf7873595c5` on `lieppos/dev16`; it does not
rewrite `dev15`.  The source changes in this campaign are:

- `gq5012bf1-board.dts` now uses the offline stock vendor_boot FDT / DTBO
  evidence for the real panel, SPI touch, fingerprint transport, YFT provider
  nodes and audio codec.  Reference-board peripherals that do not exist on
  GQ5012BF1 (FT3519 touch, Goodix fingerprint, SC8541/UPM6720 chargers, the
  hl5280 audio switch and the reference camera addresses) are disabled.
  `LIEPPOS-BOARD-EVIDENCE.md` records the per-node evidence class and the
  activation gate.
- `yft_devinfo.c` now removes duplicate registry entries from both lists before
  freeing them, checks the camera-info allocation it actually uses, handles a
  missing `bootargs` property, owns/frees the URXDO GPIO, and owns/cleans up its
  proc entries and device class on probe failure and remove.
- The focused `yft_devinfo.ko` rebuild has the same exported symbol set and the
  same `__crc_*` values as the Campaign 15 module; its vermagic is still
  `6.1.115-mt6878-gq5012bf1-dev15+`.  The ABI audit is in
  `staging/evidence/dev16/yft-devinfo-abi-audit.txt`.
- The stock Mali node (`mali@13000000`), its five interrupts, GMPU/protected
  memory allocator, GED supply and OPP table were compared against
  `mt6878.dts`; the source values already match the stock vendor_boot FDT, so
  no Mali DT change was needed.  The r44p1 source and page-migration graft
  remain the Campaign 15 integration.
- The camera build inputs are pinned in `LIEPPOS-EXTERNAL-SOURCES.md`: outer
  `mtkcam` `fa1e6f372940c47bcd0041824afdce7802dbec3d`, nested imgsensor
  `d44461a5f43f1db7e8664179cb3248ebf84ce9d3`, and the two restored GPL-2.0
  headers with their SHA-256 values.

The 33 external modules were rebuilt against the dev15 target ABI and staged
separately from the in-tree 721-module set.  A full Target C rebuild remains
deferred while the Btrfs metadata headroom is below the 6 GiB restart gate.
Consequently the kernel release string in the current output tree is still
`6.1.115-mt6878-gq5012bf1-dev15+`; the `dev16` branch name is not an Image or
module-version claim.

## Campaign 17 source state

Campaign 17 starts from `446ad5e4ee5b31a8f9ae8b3d92c6a788e20f2a57` on a
new `lieppos/dev17` branch.  It does not rewrite `dev15` or `dev16`.

Source changes in this campaign:

- **RPMB/TEE provider correction.**  `rpmb-mtk.c` no longer requires the
  Trustonic `mCrpModule-ffa` path.  The Trustonic GP/DCI and netlink proxy are
  compiled only with Trustonic support; the non-Security-OS
  `MAX_RPMB_REQUEST_SIZE` fallback is 4096 bytes and the stale eMMC
  `param->key`/`param->data` field references were corrected.  The new
  `arch/arm64/configs/gq5012bf1.config` fragment disables Trustonic,
  Microtrust and MTK TEE GP support while enabling the TrustKernel stack.
  See `LIEPPOS-RPMB-TEE.md`.
- **Camera provider consolidation.**  The GQ5012BF1 config selects only
  `mtk_ccuv`, ISP7SP camsys, ISP7SP imgsys/DPE and the external v4l2
  imgsensor.  The in-tree legacy CCU provider is gated by the new
  `CONFIG_MTK_CCU_RPROC_LEGACY` symbol; the external `mtkcam/camsys` Kbuild
  selects exactly one camsys family; the in-tree standalone `camera_pda.ko`
  is no longer built.  See `LIEPPOS-CAMERA-PROVIDERS.md`.
- **Board provenance separation.**  `gq5012bf1-board.dts` is now the trusted
  profile.  The Campaign 16 offline-copy values are retained in
  `gq5012bf1-board-pending.dts`, which is gated by
  `CONFIG_GQ5012BF1_PENDING_BOARD_DT` (default `n`).  The reference Goodix SPI
  node was renamed from `gffingerprint@spi1` to `gffingerprint@0`; the label is
  unchanged.  See `LIEPPOS-BOARD-EVIDENCE.md`.
- **External camera pin.**  `mtkcam` is pinned at
  `710b46f44046b21afe3b13f60b06c8c809316e65` (Campaign 16
  `fa1e6f372940c47bcd0041824afdce7802dbec3d` plus the camsys selection).
- The Campaign 16 `yft_devinfo` lifecycle fixes and their ABI audit are kept.

Focused build evidence:

- `rpmb-mtk.o` compiles with the TrustKernel configuration; strict manual
  modpost against the genuine C15 provider CRCs passes; the focused
  `rpmb-mtk.ko` has `depends: ufs-mediatek-mod,rpmb` and no TEE import.
- `mtkcam/camsys` compiles/links only the ISP7SP object for the selected
  config; `mtkcam/mtk-dpe` `modules.order` contains only
  `camera_dpe_isp7sp.ko`.  A scoped strict modpost resolves the CCU symbol to
  `mtk_ccuv` and generates a `...mtk_ccuv` dependency with 0 errors (five
  AOV seninf export-version warnings; no fabricated CRCs).
- The trusted DTBO is 3,480 B with SHA-256
  `e9896d333cd3be163a890df6eed525fe6770dd73a6e0c4e635eb7994e8459c8a` and
  applies offline to the source-built MT6878 DTB.  The pending DTBO is
  14,206 B with SHA-256
  `a34b20311d56a2a2c7387724e1e5ca857f7b0bdd19217e4b71276f0d9a546583`
  and is not in the default build.

The Btrfs metadata headroom remained below the 6 GiB full-build restart gate.
No dev17 Image was built, no dev17 release string exists, and the current
Image is still the Campaign 15 `6.1.115-mt6878-gq5012bf1-dev15+` build.

### Protected-path incident

Campaign 16's broad `/home/<user>` directory-name search emitted directory names
from inside the protected `/home/<user>/<protected-tree>/` tree once.  No file
contents were read or copied by that search.  This was not perfect compliance
with the no-traversal rule.  Campaign 17 used only explicitly scoped authorized
roots and did not repeat the broad search.  The offline stock DT copy remains
provenance-pending; moving or re-hashing it does not authorize its lineage.

## Limits

This tree records the source state that Campaign 15 built as
`6.1.115-mt6878-gq5012bf1-dev15+`.  It does not make the result stock-binary
equivalent, and it does not validate any driver on hardware.  The stock
certification remains `440/458`; LieppOS OK remains `6/458`; hardware
validation remains `0`.  Campaign 17 has not produced a new full Image, a new
release string, or a promoted 754-module release.
