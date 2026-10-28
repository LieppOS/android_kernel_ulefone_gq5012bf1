# LieppOS Campaign 18 handoff

Campaign 18 continues the committed Campaign 17 source line with
evidence-backed source corrections.  This is source work only.  There is no
dev18 Image, no deployable package, no hardware validation, no phone access,
and no push.

## Revision contract

| Repository | Branch | Revision | Role |
|---|---|---|---|
| `src` | `lieppos/dev18` | handoff docs head on top of source milestone `1964b1d4f75fc7e5baa1709bc7b6829343a0bae5` | kernel source |
| `mtkcam` | `lieppos/dev18` | `f7b3485ec33b5b7daed6dfb6068ce35e5e8d3844` | camera/CCU providers |
| `gpu` | `lieppos/dev18` | `cc25eeac3d81834d1065f42b771bebcac18c5216` | Mali r44p1 providers |

Campaign 17 source base remains
`771a4d4527fe1cb70bdbcdb1d0db5a0eb3653ad6`.  Campaign 16 production baseline
remains `446ad5e4ee5b31a8f9ae8b3d92c6a788e20f2a57`.

### `src` commits

- `e318ce673` - `rpmb: fix request ownership and UFS raw-device teardown`
- `1f33f9b58` - `fingerprint: correct microarray probe and regulator lifetime`
- `bac384e72` - `drm: fix CRTC worker lifetime and MSYNC2 floating point`
- `2914769a4` - `soc: reject incomplete pwrap platform data and IRQs`
- `1964b1d4f` - `power: drain MT6375 charger work before unregister`

### External source commits

- `mtkcam` `f7b3485` - `camera: fix seninf list lifetime and CCU resource teardown`
- `gpu` `cc25eea` - `Mali: fix platform init error handling and vm_flags backport`

The external commits are immutable handoff points for the source owner.  No
rebase, amend, force-update, or module substitution is part of this handoff.

## Source changes

### RPMB and UFS

- `rpmb_dev_unregister()` retains the device/class references through
  `rpmb_cdev_del()` and `device_del()`, then drops the find/registration
  references.
- `rpmb_cmd_fixup()` indexes `cmds[i].frames`; `rpmb_cmd_req()` validates
  `ops`; `rpmb_get_rw_size()` handles null `rdev`/`ops`.
- `rpmb_init()` propagates class/cdev failures and unwinds registration.
- `rpmb-mtk` requires `CAP_SYS_RAWIO`, validates UFS ioctl size, address and
  pointers, returns `-EOPNOTSUPP` for unsupported program-key programming,
  preserves request error codes, uses big-endian response conversion, and no
  longer accepts an all-zero HMAC.
- UFS RPMB registration stores the asynchronous cookie and SCSI reference,
  drains registration on removal, handles automatic-PM failure, guards null
  host/SCSI state, and removes the RPMB device before dropping the SCSI
  reference.

### Camera and CCU

- `mtk_cam-seninf-drv.c` removes a probed context from `core->list` on every
  post-`list_add` failure and deletes the list entry on remove.
- CCU common state now tracks chardev registration and balances
  chrdev/class/device teardown.
- CCU ISP7SP probe checks ioremap results, unwinds PM and remoteproc state,
  guards `dev_cammainpwr`/SPM mappings, clears debug/mrdump pointers, and does
  not free an unregistered chardev.

### Fingerprint

- `madev.c` uses zeroed probe state, balances chrdev/class/device/cdev state,
  only requests and frees a valid IRQ, removes the sysfs group on teardown,
  and unwinds SPI, IRQ, buffer, wake-lock and regulator resources.
- `mtk-settings.c` balances the `vfp` regulator get/enable/disable/put,
  suppresses a duplicate enable, and releases the DT node reference in
  `mas_get_irq()`.

### DRM and MSYNC2

- CRTC destruction stops the vblank, DC commit, trigger, delayed-trigger,
  CMDQ-trigger, present-fence and secure-fence threads and releases the
  wakeup source/name.
- Failed `kthread_create()`/`kthread_run()` results are no longer woken or
  bound; the always-true `&thread == NULL` checks were corrected.
- MSYNC2 now represents the 46.1 fps threshold as integer tenths, avoiding
  the arm64 kernel's unsupported floating-point ABI.

### Mali

- Invalid `kbdev` logging no longer dereferences the error/null pointer.
- A failed platform PM initialization clears the cached `mali_kbdev`, and
  teardown clears it after `mtk_platform_pm_term()`.
- The r44p1 compatibility header no longer shadows the `vm_flags_set()` /
  `vm_flags_clear()` backport present in this 6.1 tree, even when
  `__ANDROID_COMMON_KERNEL__` is not defined.
- The `vm_flags` source fix was exercised far enough to remove the original
  redefinition/const-assignment failures.  A clean object compile is still
  pending because the external Mali Kbuild command currently receives an
  empty `KASAN_SHADOW_SCALE_SHIFT` and then fails in unrelated arm64 signal
  array-bounds checks.  That is a build-input issue, not a reason to weaken
  `-Werror` or edit unrelated headers.

### Platform providers and power

- `mtk-pmic-wrap.c` validates the OF node and master capabilities, rejects a
  negative IRQ before `devm_request_irq()`, and depopulates children if OF
  population fails.
- `mt6375-charger.c` cancels the delayed power-ready work and BC1.2 work
  before unregistering the charger device and destroying its workqueue.

## Evidence

Evidence is copied to `staging/evidence/dev18/`; matching hashes are in
`staging/evidence/dev18/SHA256SUMS`.

| Evidence | Result |
|---|---|
| `c18-rpmb-compile-2.log` | `rpmb/core.o` and `rpmb-mtk.o` compiled |
| `c18-ufs-mod-compile-1.log` | UFS vendor composite object compiled |
| `c18-fp-compile-2.log` | microarray `madev.o`/`mtk-settings.o` compiled |
| `c18-camsys-compile-1.log` | ISP7SP seninf object compiled |
| `c18-ccu-compile-1.log` | CCU common/ISP7SP objects compiled |
| `c18-drm-crtc-compile-2.log` | DRM CRTC object compiled after the MSYNC2 fix |
| `c18-mali-compile-1.log` | first Mali object attempt exposed the `vm_flags` shadowing; fixed |
| `c18-mali-compile-2.log` | recorded Kbuild command after the fix; blocked by external Kbuild KASAN-shift/array-bounds input mismatch |
| `c18-mali-compile-3.log` | `KBUILD_OPTIONS` probe dropped config defines; not a source result |
| `c18-pwrap-compile-1.log` | pwrap object compiled |
| `c18-mt6375-chg-compile-1.log` | MT6375 charger object compiled |
| `c18-trusted-dtbo-dtc-3.log` | trusted DTBO rebuilt; SHA-256 matches the existing trusted artifact |

The trusted DTBO direct compile produced
`e9896d333cd3be163a890df6eed525fe6770dd73a6e0c4e635eb7994e8459c8a`,
matching `out/arch/arm64/boot/dts/mediatek/gq5012bf1-board.dtbo`.  The global
`dtbs` target was also attempted and failed only on an unrelated existing
mt8192 Asurada label error; that failure is not attributed to GQ5012BF1 and no
unrelated DT was modified.  The direct `dtc` run emitted the existing
disabled-node unit-address/address-cells warnings; those nodes remain
disabled and no numeric wiring was invented to silence them.

No full `Target C` build, full module link, or new strict modpost run was
performed.  In particular, the five AOV seninf export-version warnings remain
the known scoped-symvers/stale-`mod.c` input-set issue recorded in Campaign 17;
no CRC was fabricated and no `KBUILD_MODPOST_WARN` was used.

A temporary LLVM dry-run regenerated compiler-detection fields in
`out/.config` and `out/include/config/auto.conf`.  The `.config` was restored
from `.config.old` and `auto.conf` was re-synced with the configured GCC
toolchain; the key TEE, camera, pending-DT and module selections were
rechecked and are unchanged.  No Claude-owned BTF/symvers input or module
staging set was edited.

## Board profile

The trusted profile remains
`arch/arm64/boot/dts/mediatek/gq5012bf1-board.dts`.  Campaign 16 offline-copy
values remain in `gq5012bf1-board-pending.dts`, gated by default-`n`
`CONFIG_GQ5012BF1_PENDING_BOARD_DT`.  The active config confirms
`# CONFIG_GQ5012BF1_PENDING_BOARD_DT is not set`.

All board numerics from the offline copy remain `PROVENANCE_PENDING` and do
not enter the trusted boot profile.  Display, touch, fingerprint/TEE runtime,
camera, charger/regulator, USB-PD, and fuel-gauge paths remain disabled or
unvalidated.  Hardware validation is `0`.

## Build and storage state

The full Target C build remains gated: Btrfs metadata free was approximately
`6.29 GB` (~`5.86 GiB`) at the last check, below the `6 GiB` restart gate.
No balance, defrag, broad dedup, destructive cleanup, or unapproved deletion
was performed.

- `out/Module.symvers` remains the C15 file, 19,812 lines,
  SHA-256 `f8cd5720381345f68ccc68d9609a976b36f581b2a58a8fca379084722e7d5b69`.
- Current Image remains C15, 30,116,352 B,
  SHA-256 `6c36c5d78ca4ead996bed07e4bb89db68282f73ca1cbaf87a13703cc1f4e9885`;
  no dev18 Image exists.
- The 754-module dev15-ABI staging manifest is preserved at
  `staging/modules-targetC-dev15-full/MANIFEST.sha256`, SHA-256
  `eb0611fcae603c1fb321f29efbde7b21b5252fdb51783e24572fdbee13cbd252`.
- No dev14/dev15 object mixing or prebuilt-module substitution occurred.

## Ownership and next steps

- Claude owns build/BTF/provenance infrastructure and scoped-symvers input
  correction.  This handoff does not change that ownership.
- Codex owns FPSGO/C2PS source.  Its config symbol is
  `CONFIG_NONLINEAR_FREQ_CTL`.
- Preserve the C16 pins, licenses, revisions, and the 754-module staging set.
- Next: recheck the Btrfs metadata gate; when it is above 6 GiB, run the full
  Target C build and strict modpost, then compare the resulting module set with
  the preserved ABI manifest.  Until then, do not claim dev18 boot or hardware
  validation.

No phone, ADB/fastboot, flashing, module loading, RPMB command, TEE secure
session, camera/panel/touch activation, charger/PD operation, fuel-gauge
firmware access, or push was performed.
