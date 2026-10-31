# Experimental DW9781C diagnostic — passive module test verified

**PASSIVE MODULE TEST PASSED; OIS HARDWARE UNPROVEN.** The owner authorized one
disabled-load/status/refusal test after confirming the other agent was paused.
It passed without I2C or power operations. This does not authorize future loading,
active sampling, power changes, rebooting or flashing. Never unload the existing
OIS/camera modules or this helper. It deliberately has no unload callback and
stays until a separately authorized normal reboot. There is no boot/init integration.

## What it can and cannot establish

The existing `dw9781c` probe binds from DT without checking silicon. A module
name, V4L2 node or DT client is therefore not hardware identification.

Only **firmware version/date** reads at `0x7001`/`0x7002` are evidenced by the
matching driver. They are **not verified silicon-ID registers**. A successful
response records two raw words; neither success nor plausible values prove a
DW9781C, lens ownership, calibrated operation or mechanical OIS. Zero, all-ones,
partial transfers and failures must not be interpreted as hardware detection.
A NACK can also mean absent power, not absent hardware.

### Source/protocol provenance

* Helper and tests: **INDEPENDENT**, authored for LieppOS; GPL-2.0-only.
* Protocol reference (not copied into this directory): NothingOSS
  `android_kernel_device_modules_6.1_nothing_mt6878`, commit
  `957dac185efe46cbf6336b0fff9516d84c8cd78f`,
  [`drivers/misc/mediatek/lens/ois/dw9781c/dw9781c.c`](https://github.com/NothingOSS/android_kernel_device_modules_6.1_nothing_mt6878/blob/957dac185efe46cbf6336b0fff9516d84c8cd78f/drivers/misc/mediatek/lens/ois/dw9781c/dw9781c.c).
  SHA256 `2391c3b1f325193bb6f5f56e827fce4a05d611eeebceb6dc10925c96dc62a281`.
  The published source is GPL-2.0 and matches the previously audited stock
  driver source. Lines 164–188 show 16-bit big-endian register selection/read;
  lines 524–525 read FW version/date. `0x9781` elsewhere is a firmware-context
  constant, **not** an observed chip ID. Unlike its read helper, this candidate
  rejects any transfer count other than exactly two completed messages.

## Safety boundaries and remaining risk

* Insertion is passive, even with `enable_sample=1`. Initialization only marks
  readiness and logs; no adapter lookup, transfer, workqueue or power callback.
* Default `enable_sample=0` is immutable after insertion, enforced by the setter
  in code as well as permissions. An active candidate would require explicit
  load-time opt-in **and** a separate post-load request. Supplying `sample=`
  during insertion is rejected before readiness.
* `status` is root-readable cached state, with no bus access. `sample` is
  root-write-only and additionally requires `CAP_SYS_RAWIO` plus the exact
  acknowledgement string defined in `protocol.h`. No executable deployment or
  sampling command is provided here; further approval and preflight are needed.
* Fixed target only: I2C bus 4, effective 7-bit address `0x0a`, registered name
  `4-000e`, exact DT path/compatible and currently bound `dw9781c` driver.
  These are **software topology guards**, not proof of a fitted chip.
* Hold the client device lock to prevent rebinding during the operation.
  Take a runtime-PM reference **only if already active**; disabled or suspended
  runtime PM refuses the request. Release with `pm_runtime_put_noidle`, not a
  callback-scheduling PM operation. There is no resume, regulator/GPIO action,
  OIS initialization, firmware download, calibration command or servo command.
* **RPM_ACTIVE is not electrical proof of the real VDD/coil rails.** The observed
  DT has no real `vdd-supply` binding. Independently established power/wiring is
  still a prerequisite for any approved experiment. The helper does not solve it.
* Each read is exactly two messages: a two-byte register-selector **bus write**,
  then a two-byte read, with the standard I2C-core adapter locking. This avoids
  register-data writes **under the DW9781C protocol only**. At an unidentified
  device with a different protocol, those two bytes could be a configuration
  write. **It is not guaranteed harmless or strictly read-only hardware access.**
* At most two `i2c_transfer` calls, stopping on the first error/short transfer.
  The single lifetime attempt is consumed before entering the transfer path,
  including on NACK/failure. There are no helper retries, scans or alternate
  addresses. The I2C core/controller can perform its own retries, recovery,
  runtime-PM and timeout handling; the helper does not override those settings.
* It does not serialize against a private OIS initialization sequence beyond
  normal adapter locking. Another agent, flash operation, camera/HAL transition,
  driver initialization or power transition must not overlap a future test.
* No SELinux, signature, CFI, MODVERSIONS, AVB or kernel protection is relaxed.
  The output is **unsigned**. Target signature/protected-symbol/SELinux rules may
  reject it; compilation and ABI checks do not authorize bypassing those rules.

**Last passive observation:** root ADB, Slot B, c29+ kernel; OIS client suspended;
`CONFIG_I2C_CHARDEV` disabled; no direct register-access interface found. That
suspended state deliberately prevents this candidate from sampling. **Do not
wake it through the stock initialization**, which can store calibration NVM.
Because another agent may flash, these observations are not a future load gate.

## Offline build and validation

Use a **fresh** task-owned output path and a previously verified prepared output
for the intended target. The build uses generated headers/host tools copied to
its own output, not a duplicate kernel source or writes to another agent's
prepared tree. `build.py` checks reference hashes before/after. It pins the
reference release for external-module compilation; the string alone is not ABI
proof. Keep the exact matching Android clang r487747c toolchain, CFI and
MODVERSIONS. Do not sign/install or regenerate another agent's kernel.

```sh
python3 build.py --kernel-source "$KERNEL_SRC" --reference-out "$REFERENCE_OUT" \
  --toolchain-dir "$TOOLCHAIN_DIR" --out "$NEW_OUTPUT"

# Host-only tests of the SAME protocol/gates used by the module:
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  tests/protocol_test.c -o "$HOST_TEST_OUTPUT"
"$HOST_TEST_OUTPUT"

python3 verify.py --candidate "$NEW_OUTPUT/module/ois_diag.ko" \
  --baseline-module "$VERIFIED_BASELINE_MODULE" --reference-out "$REFERENCE_OUT" \
  --crosscheck-out "$SECOND_VERIFIED_OUTPUT"
```

Review `build.log`, compiler/config/source hashes, all module imports/CRCs,
callback KCFI types, code paths, protection settings and the whole staged diff.
Run protocol tests with the kernel compiler as well. These tests cover exact
selectors/endianness, opt-in/readiness/capability/acknowledgement/one-shot gates,
short transfers, NACK/errors, stopping on failure and raw zero/all-ones handling.
They do **not** emulate the kernel's driver locking, PM races, electrical bus or
unidentified hardware. No KUnit or OIS/I2C hardware test has been performed;
the limited passive phone test is recorded below.

## Recorded offline result

* **BUILD_VERIFIED / ABI_VERIFIED_OFFLINE** before the passive phone test below.
  Android clang17 r487747c, c29+ vermagic matches the verified camera-module baseline.
* Final artifact: workspace-relative
  `build/ois-diagnostic/candidate/module/ois_diag.ko` (356448 bytes, unsigned).
  SHA256 `4dde6501bbba6fec22540cc1b92805fb623cdc98e04da263c8719fb402a47b82`.
  Build/config/source hashes and full checks are in that candidate output's
  `BUILD.json`, `ABI-VERIFY.json`, `TESTS.json` and `build.log`.
* 19 modversion entries (18 imports plus `module_layout`) match both selected
  C62 and SUSFS reference outputs. Callback KCFI prefixes, init KCFI and the
  device-match callback agree with the checked reference kernel/module code.
  Required imports are allowed by the reference GKI unprotected-symbol lists.
  None of this substitutes for verifying the kernel after another agent flashes.
* Host tests: **25 cases pass with GCC ASan/UBSan and Android clang17**.
  Strict checkpatch on module/header: **0 errors, 0 warnings, 0 checks**.
  Python AST checks pass. Negative tests reject existing output, a different
  compiler, a different-vermagic baseline and a deliberately wrong I2C CRC in a
  separate text-only fault fixture. Reference binaries were not modified.
* Review hardened post-load opt-in immutability in code. Init and cached-status
  paths were reviewed for absence of transfers/power actions; the only transfer
  site has fixed messages/registers and lies behind all stated preconditions.
* Client PM/locking and electrical transfer behavior remain **untested**. Safe real
  power/wiring and a trustworthy silicon-ID register are **not established**.
  The last observed suspended state blocks the active path by design.

## Recorded passive phone test (2026-10-31)

**PASSIVE_MODULE_RUNTIME_VERIFIED_ONLY — no OIS identification or bus test.**

* Owner confirmed the other agent was paused and chose the passive-only test.
  Fresh root/Slot-B preflight confirmed the live config exactly matched the
  captured compatible baseline and `boot_b` matched its authenticated hash.
* The phone staging copy matched the final candidate SHA256 above. Normal
  insertion with immutable `enable_sample=0` returned success, without force
  flags, signature changes, SELinux relaxation or reboot. The module appeared
  as live and `[permanent]`; its init log explicitly reported passive operation.
* Cached status initially reported `ready=1 enabled=0 attempted=0 last_error=0
  valid_mask=0x0`. A sampling request while disabled was rejected with **EACCES
  (-13)**. Afterwards `enabled=0 attempted=0 valid_mask=0x0` remained unchanged.
  This verifies the **disabled opt-in gate**, not the runtime-PM/transfer path.
* The displayed zero FW words are unfilled cached fields, **not register reads**.
  No I2C transfer was attempted and neither chip presence nor OIS is established.
  The real OIS client remained suspended. No power/calibration/servo command,
  module removal, reboot, slot switch or partition write was performed.
* Boot identity, `boot_b` hash, SELinux enforcing state and the pre-existing
  kernel taint value were unchanged at the end of the test.
* Removed only the staging copy created by this test. The disabled helper
  remains in RAM until a normal reboot; there is no persistent load integration.
  Its immutable opt-in cannot be enabled in this loaded instance. A future
  enabled instance requires a separately approved normal reboot/load sequence,
  satisfied power/topology prerequisites and explicit active-test approval.
* Private raw evidence and summary are workspace-relative
  `build/ois-diagnostic/runtime/{PREFLIGHT.json,COPY.json,LOAD.json,STATUS-BEFORE.txt,
  GUARD-TEST.json,FINAL-STATE.txt,RUNTIME-RESULT.json}`. Raw device/boot identifiers
  stay outside Git. This public handoff records only the bounded outcome.

## Before any future deployment

Owner approval is still required. Coordinate exclusive device access with the
other agent; re-establish device identity, boot/slot, exact kernel/config/module
ABI and protection state. Establish a defensible power/topology plan and accept
pointer-write/unknown-device risk explicitly. If any prerequisite fails, stop.
Never force-load, force-remove, unbind, power the chip speculatively, change Slot
A, install an init hook or treat metadata as proof of OIS.

Phone findings are preserved here as a **research-ready handoff, not yet
integrated** into the shared research checkout. That checkout has unrelated work
and a publication hold; this source task must not modify or publish it.
