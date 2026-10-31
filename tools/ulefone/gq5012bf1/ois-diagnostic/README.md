# Experimental OIS model-word diagnostic — protocol v2

**V2 IS BUILT/ABI-VERIFIED OFFLINE ONLY. IT HAS NEVER RUN ON THE PHONE.**
**OIS population, real electrical power, lens ownership and stabilization remain
unconfirmed. No new device access is authorized by this source or its build.**

This optional standalone helper is not an OIS activation driver and has no
boot/init integration. The stock driver still binds without checking silicon.
A module name, DT client, V4L2 node or `RPM_ACTIVE` state is not chip detection.

## What changed from v1

V1 read only firmware version/date at `0x7001`/`0x7002`. Its disabled passive
load/status/refusal test passed, but made **zero transfers**. V2 instead permits
one raw candidate firmware/model word at **`0x7000`**, with no second word,
retry or fallback. It never reads `0xD060` or releases factory protection.

The OEM source below expects `0x9781`, but that is **not a value observed on this
phone**, an immutable silicon signature, or a mechanically verified OIS result.
Even a matching raw value is only supporting model-response evidence. A NACK,
zero, all-ones or unexpected value cannot prove absence: power, firmware state,
address/profile applicability and physical population remain separate unknowns.
`valid_mask=1` means only that the exact message pair completed, not valid ID.

V2 status identifies `protocol=2` and reports `model_word`; it never changes
`identity_proven=0 mechanical_ois_proven=0`. It has a new exact risk token in
`protocol.h`; the old v1 token is rejected. Do not confuse an old loaded v1
instance with this candidate. Opt-in remains immutable after insertion.

## Source/protocol evidence

* Helper/tests: **INDEPENDENT**, LieppOS-authored, GPL-2.0-only. No vendor firmware
  image, calibration table or register-data programming sequence is copied here.
* Matching stock-driver source: NothingOSS commit
  `957dac185efe46cbf6336b0fff9516d84c8cd78f`,
  [`dw9781c.c`](https://github.com/NothingOSS/android_kernel_device_modules_6.1_nothing_mt6878/blob/957dac185efe46cbf6336b0fff9516d84c8cd78f/drivers/misc/mediatek/lens/ois/dw9781c/dw9781c.c).
  Canonical source SHA256:
  `2391c3b1f325193bb6f5f56e827fce4a05d611eeebceb6dc10925c96dc62a281`.
  The prior saved stock parity/reachability evidence is reused, not rerun.
* New external protocol lead: Vivo `Y53s` commit
  `2b1b1f054ccce7ed551137f0eb1d0cce6976f2aa`,
  [DW9781C register definitions](https://github.com/vivo-source-mirror/Y53s/blob/2b1b1f054ccce7ed551137f0eb1d0cce6976f2aa/drivers/misc/mediatek/lens/ois/DW9781C/ois_dw9781c.h),
  [identity logic](https://github.com/vivo-source-mirror/Y53s/blob/2b1b1f054ccce7ed551137f0eb1d0cce6976f2aa/drivers/misc/mediatek/lens/ois/DW9781C/ois_dw9781c.c#L1371),
  and [16-bit transport](https://github.com/vivo-source-mirror/Y53s/blob/2b1b1f054ccce7ed551137f0eb1d0cce6976f2aa/drivers/misc/mediatek/lens/ois/ois_util.c#L148).
  Transport SHA256:
  `bb8ec4d5403662a3aa33c65054f6ae90afb71619a13fa7a2bb150ac5fcdce098`.
  Its selector and result are both two-byte big-endian, matching the audited
  Nothing transport. Unlike the OEM helper, v2 rejects every nonexact message
  count and does not expose poisoned/partial result bytes.
* The Vivo profile uses 7-bit address `0x72`, not the phone driver's `0x0a`.
  It is register/protocol evidence, **not this board's wiring or a validated
  cold-read recipe**. Its secondary factory-ID path performs protection writes
  and belongs to firmware-update logic; never transplant that routine.

## Safety boundaries

* Insertion is passive even with `enable_sample=1`: init only sets readiness and
  logs. No adapter lookup, I2C, worker or power callback runs on insertion.
* Default `enable_sample=0` is immutable in the setter and permissions. A sample
  supplied during insertion is rejected before readiness. An eligible active
  candidate needs separately approved load-time opt-in AND a post-load request.
* `status` is root-readable cached data only. `sample` is root-write-only and
  requires `CAP_SYS_RAWIO`, the exact v2 acknowledgement, and all guards below.
  No executable deployment/sampling commands are supplied here.
* Fixed software target: I2C bus 4, effective 7-bit address `0x0a`, registered
  name `4-000e`, exact `/soc/i2c@11c23000/camera-ois-main@e`, compatible
  `mediatek,dw9781c`, flags zero, bound driver `dw9781c`. DT binding is not
  physical identification. No arbitrary address/register/length is accepted.
* Hold the client device lock and take a runtime-PM reference **only if already
  active**. Disabled/suspended runtime PM refuses. Release with
  `pm_runtime_put_noidle`, without scheduling a power callback. No resume,
  regulator/GPIO action, initialization, calibration, servo or firmware command.
* One eligible lifetime attempt is consumed before bus access, even on NACK or
  error. It makes at most **one `i2c_transfer` call**, containing two messages:
  selector bus write `[0x70, 0x00]`, then two-byte read. No fallback or helper
  retries. The adapter/core can perform its own retries, recovery, timeout and
  PM handling; those are not overridden or bounded by a mock host test.
* A register-selector write is not a data write **under the DW9781C protocol
  only**. At an unidentified device those bytes might be configuration data.
  This is **not guaranteed harmless or strictly read-only hardware access**.
* `RPM_ACTIVE` and successful transfers do not measure the actual VDD/coil
  rails. Establish a defensible power/topology plan and explicitly address
  unknown-device/pointer-write risk before an approved experiment. Do not guess
  GPIOs, switch unrelated regulators, bypass the PM guard or assume a NACK means
  no hardware. If the required prerequisites cannot be justified, stop.
* The device lock/adapter lock does not serialize a whole private OIS init
  sequence. No concurrent OIS/hf_manager enable, flash, camera/HAL transition,
  power change or other agent/device operator may overlap a future request.
* No SELinux, signature, KCFI/CFI, MODVERSIONS, AVB or GKI restriction is relaxed.
  Output is **unsigned** and may be rejected on the actual target. Do not force
  load, force remove, unbind drivers or change protections.
* No unload callback: an inserted helper remains until a separately approved
  normal reboot. Never use rmmod/modprobe removal on this or camera/OIS modules.

## PM-only open/close assessment — source evidence, not a phone test

The canonical V4L2 core (`drivers/media/v4l2-core/v4l2-subdev.c`, `subdev_open`
and `subdev_close`) dispatches the driver's internal open/close callbacks.
The matched DW9781C source shows:

* `dw9781c_open` (line 758) calls `pm_runtime_get_sync`, not `dw9781c_init`.
* Runtime resume calls `dw9781c_power_on` (line 675); runtime suspend calls
  `dw9781c_power_off` (line 624). Neither calls I2C or `dw9781c_release`.
* `dw9781c_close` (line 771) calls `pm_runtime_put`; normal close is distinct from
  `S_OIS_UNINIT` or hf_manager disable, which CAN send chip commands.
* The two regulator names are `vdd`/`rst`; pinctrl and clock-name arrays are empty.
  In the captured configuration `vdd` is dummy and `rst` is a fixed 1.8 V regulator
  with no enable GPIO. Those bindings do not identify the physical OIS rails.
* Existing `power_on` overwrites `ret` on successive regulator operations and
  has no partial-enable rollback. An earlier failure can be hidden by a later
  success. `ret` also depends on a regulator operation actually occurring.
  **PM-active is not proof that every supply operation succeeded.** Do not patch
  or use that path merely to manufacture a passing diagnostic guard.

Thus an explicitly approved subdevice-open hold is a possible PM-only test path,
not OIS initialization or proof of real power. Both success and error/close paths,
current provider bindings and concurrent consumers need fresh review/preflight.
No subdevice was opened and no PM operation was performed in this investigation.

## Current offline result — v2

* Real ARM64 Android clang17 r487747c compile, modpost and link: **PASS**.
* Workspace-relative artifact:
  `build/ois-diagnostic/id-only-candidate/module/ois_diag.ko`
  (354920 bytes, unsigned, module version 2).
* SHA256: `91c4eb0fc5c253b0eef4a253df580c367b231bb31126088acfc94c62e77d114f`.
* `BUILD.json`, `ABI-VERIFY.json`, `TESTS.json` and build log are in that candidate
  directory; detailed host/checkpatch logs are in `build/ois-diagnostic/id-only-review/`.
  Exact helper-source hashes qualify this build; the recorded source HEAD is
  pre-commit context, not a claim that a whole kernel was rebuilt.
* 19 modversion records (18 imports plus `module_layout`) match the preserved
  C62 and SUSFS reference outputs. Callback, init and device-match KCFI checks
  pass; reference GKI unsigned-import checks pass. No exports or unload callback.
  This does not establish compatibility with a phone changed by another agent.
* **28 cases** pass with GCC ASan/UBSan and Android clang17: 11 gate cases,
  five selector cases, four raw/endian cases and eight transfer-failure cases.
  The expected model word, zero and all-ones remain raw values, not detection.
* Strict module/header checkpatch: **0 errors, 0 warnings, 0 checks**; Python AST
  checks pass. Negative tests reject reused output, another genuine compiler,
  different-vermagic baseline and one wrong CRC in a text-only fault fixture.
* Reference ELF/symbol-table hashes were unchanged. No new firmware, real-bus,
  kernel-PM race, V4L2 lifecycle runtime, lens or stabilization test was run.

For a fresh offline build, use `build.py --help` and the existing explicit
`--kernel-source`, `--reference-out`, `--toolchain-dir`, `--out` interface.
Never reuse/overwrite another output. `verify.py` is read-only and requires an
explicit baseline module and two selected prepared reference outputs.

## Historical v1 passive test — not validation of v2

At commit `d937ef02ecec014bf9128947f76b025d79bca974`, the owner authorized one
disabled load/status/refusal test after exclusive-device coordination. The
recorded v1 artifact was **356448 bytes**, SHA256
`4dde6501bbba6fec22540cc1b92805fb623cdc98e04da263c8719fb402a47b82`.
It loaded normally with `enable_sample=0`; cached status worked; disabled sampling
returned `EACCES`; the attempt counter stayed zero. No chip word was read.
OIS stayed suspended. No power/init/calibration/servo action, unload, reboot or
partition write occurred; the staging copy was removed. Historical boot identity,
SELinux and taint checks were unchanged within that bounded test.

The helper was left in RAM until a normal reboot. Its current presence is
**unknown without fresh preflight**. It cannot be enabled in place, and the old
passive result does not authorize loading v2 or any active test. Private v1
records remain under `build/ois-diagnostic/runtime/`; do not publish raw device
identifiers. The original source/readme/build evidence remains recoverable in Git
and the preserved v1 output; v2 does not overwrite it.

## Read-only device preflight (2026-10-31)

Owner-authorized, exclusive-use, read-only ADB checks (sysfs/procfs/live DT,
properties). No subdevice open, module load, PM/power change, I2C or reboot.

* Slot B, c29+ kernel, SELinux enforcing, verified-boot property green.
* v1 helper still loaded `[permanent]`: `enabled=0 attempted=0`; it cannot be
  enabled. Loading v2 would first need a separately approved normal reboot.
* OIS client `4-000e` (V4L2 entity `dw9781c 4-000a`): runtime **suspended**,
  usage 0, **zero runtime-active time this boot**; no process held its subdevice.
  The I2C-4 adapter was suspended too. The v2 guard would refuse (`-EHOSTDOWN`).
* Supplies: `vdd` is `regulator-dummy`; `rst` is `camera-common-dovdd`.
  A `camera-common-oisvdd` node does exist, but like `dovdd` it is a
  `regulator-fixed` 1.8 V with **no enable GPIO**. It has no consumers, and no
  supply on camera I2C buses 2/4/7 references its phandle.
* So the live DT has **no software-controlled OIS power path**. Whether the chip
  is powered (e.g. tied to an always-on or camera-switched rail) is unknown.
  A PM-active state would change only regulator bookkeeping.

Private raw output: `build/ois-diagnostic/runtime-preflight-v2/`.

## Next step / research ownership

Obtain specific owner permission for fresh read-only device preflight before
any phone operation. Re-establish actual boot/kernel/config/module identity,
protection state, topology, consumers and power prerequisites. Then separately
review any PM-open/camera-power/one-word experiment and its pointer-write risk.
No normal reboot, insertion, register read or stock initialization is authorized
by a successful offline build. Never use an unsuffixed/implicit flash target or
write Slot A; no flashing is needed or authorized by this diagnostic preparation.

Canonical research receives the bounded source findings and remaining unknowns
under `reverse-engineering/camera-stack/dw9781c-ois/` on local `main`. Its existing
publication hold remains: local integration is not a public research push.
