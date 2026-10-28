# GQ5012BF1 RPMB / TEE source contract

This file records the Campaign 17 RPMB/TEE source correction.  It is source
evidence, not a hardware-validation claim.

## Stock architecture evidence

The authorized stock module set contains:

- `tkcore.ko`, `tkcore_drv.ko`, `ffa_v10.ko`, `tmem_ffa.ko`
- `rpmb.ko`
- `rpmb-mtk.ko`
- `ufs-mediatek-mod.ko`

`rpmb-mtk.ko` imports only:

```text
rpmb_cmd_req
rpmb_get_rw_size
ufs_mtk_rpmb_get_raw_dev
```

It has no `mCrpModule-ffa`, `mobicore`, Trustonic or genl/netlink imports.
Its stock `depends` metadata names `ufs-mediatek-mod-ise,rpmb`; the shipped UFS
provider is the plain `ufs-mediatek-mod.ko` variant, so that dependency name is
stale metadata in the stock artifact.  The actual symbol provider is
`ufs-mediatek-mod`.

No stock module contains `mCrpModule`, `mobicore` or Trustonic strings.  The
stock TEE modules are TrustKernel (`tkcore`, `tkcore_drv`, `ffa_v10`).  The
Trustonic GUD/mobicore path is therefore not part of the GQ5012BF1 stock
architecture.

## Source defect and correction

The donor `drivers/char/rpmb/rpmb-mtk.c` still enabled the Trustonic GP/DCI
path whenever `CONFIG_TRUSTONIC_TEE_SUPPORT` was set by the donor defconfig.
That path introduced the donor-only `mCrpModule-ffa` dependency even though the
device uses TrustKernel.

Campaign 17 made the RPMB source usable without the Trustonic provider:

1. `arch/arm64/configs/gq5012bf1.config` disables
   `CONFIG_TRUSTONIC_TEE_SUPPORT`, `CONFIG_TRUSTONIC_TRUSTED_UI`,
   `CONFIG_MICROTRUST_TEE_SUPPORT` and `CONFIG_MTK_TEE_GP_SUPPORT`, while
   keeping `CONFIG_TRUSTKERNEL_TEE_SUPPORT`,
   `CONFIG_TRUSTKERNEL_TEE_FP_SUPPORT` and
   `CONFIG_TRUSTKERNEL_TEE_RPMB_SUPPORT`.
2. `rpmb-mtk.c` defines the non-Security-OS reliable-write window
   `MAX_RPMB_REQUEST_SIZE` as 4096 bytes.  This matches the stock
   `rpmb-mtk.ko` `rpmb_ioctl_ufs` bound (`read_size <= 4096`, i.e. eight RPMB
   frames).
3. The netlink proxy is compiled only with Trustonic support.  The stock
   device module has no genl imports, so the proxy is not part of the
   TrustKernel RPMB path.
4. The eMMC GP/DCI executor is compiled only with Trustonic support.  It uses
   Trustonic `DCI_RPMB_CMD_*` identifiers and `rpmb_gp_dci`, which do not exist
   in the TrustKernel path.
5. The non-multi-block eMMC fallback uses the real `rpmb_ioc_param` member
   names `keybytes` and `databytes`; the donor source had stale `key`/`data`
   references that only built when the Trustonic multi-block macro suppressed
   the fallback branch.

No Trustonic symbol, CRC, stub or fake secure response was added.

## Focused build result

The source was compiled against the dev15 Target C ABI with the vendor TEE
configuration above:

- `rpmb-mtk.o` compiles with zero errors and zero warnings.
- The compiled object has no `mCrpModule`, `mobicore`, Trustonic, tkcore or
  genl imports.
- A strict `scripts/mod/modpost -m -E` run over `vmlinux.o`, `rpmb.o` and
  `rpmb-mtk.o`, with the genuine `ufs_mtk_rpmb_get_raw_dev` CRC
  (`0xda67dbeb`) from the C15 `Module.symvers`, completed with zero errors and
  zero warnings.
- The generated module metadata contains:
  `MODULE_INFO(depends, "ufs-mediatek-mod,rpmb");`.
- The focused `rpmb-mtk.ko` has SHA-256
  `d20030906552f2fd67de342e6821c1e0b55382c27190a598519cb2068ddb9092`
  and `depends: ufs-mediatek-mod,rpmb`.
- Its vermagic remains `6.1.115-mt6878-gq5012bf1-dev15+`; no release string was
  changed and no dev17 Image was produced.

The focused `.ko` is an engineering artifact, not a deployable module.  The
post-link ARM64 postlink step could not be re-run in the narrowed single-module
build because that make target expects the full out-of-tree layout; the object
compile and strict modpost validation above are the source evidence.

## First-stage source contract for Claude

For a non-Trustonic GQ5012BF1 module set:

1. `rpmb.ko` provides `rpmb_cmd_req`, `rpmb_get_rw_size` and the RPMB class.
2. `ufs-mediatek-mod.ko` provides `ufs_mtk_rpmb_get_raw_dev`.
3. `rpmb-mtk.ko` consumes both providers and has no TEE import.
4. `tkcore.ko`, `tkcore_drv.ko` and `ffa_v10.ko` are the TrustKernel stack.
   They are not direct dependencies of `rpmb-mtk.ko`.
5. Do not add a `mcDrvModule-ffa` dependency or first-stage module for this
   device.  Do not invent a `vendor_boot/modules.load` entry; Claude owns the
   offline placement analysis.

## Remaining limits

- No full Target C build was run: Btrfs metadata headroom was below the 6 GiB
  restart gate.
- No RPMB hardware command, TEE session, fingerprint secure-world operation or
  phone access was performed.
- The stock UFS provider metadata says `ufs-mediatek-mod-ise`; the shipped
  module is `ufs-mediatek-mod`.  This artifact-level inconsistency is recorded
  but is not "fixed" by editing a compiled ELF.
