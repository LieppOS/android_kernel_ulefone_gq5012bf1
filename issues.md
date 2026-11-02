# GQ5012BF1 (Ulefone Armor 29 Pro) — Open Issues

Device: Ulefone Armor 29 Pro (GQ5012BF1), MediaTek MT6878, Android 17
userdebug/test-keys, Slot `_b`, verified boot `green`, verity `enforcing`.

Kernel: `6.1.115-mt6878-gq5012bf1-c29+`

Recorded: 2026-10-31

---

## 1. Battery capacity follows the MTK gauge fallback instead of the SH366003 FCC (fixed on `integration/battery-gauge-fix`)

### Symptom

The `battery` power supply reports the MTK PMIC gauge's compiled
default instead of the real pack capacity:

```
battery/charge_full        = 2946000   # 2946 mAh
battery/charge_full_design = 8807000   # SH366003 design (drifts 8.807-8.824 Ah)
battery/charge_counter     = 1207860   # scaled from the wrong q_max
battery/capacity           = 42
battery/voltage_now        = 7693000
battery/temp               = 310
battery/cycle_count        = 31
battery/health             = Good
```

The UI `capacity` already tracks the SH366003 (`3rd-gauge/capacity` = 42),
so the visible percentage was not the broken value. The broken values are
`charge_full` and everything derived from it, notably `charge_counter`
and `time_to_full_now`.

The SH366003 fuel gauge on I2C-9 (`9-0055`) logs the real pack data:

```
[sh366003] fg_monitor_workfunc: RSOC=41 Volt=7686 Curr=0 Temp=304
           SoH:94 cycnt:31 Rc:3594 FCC:8796 Vchg:8900 Ichg=13000
```

The learned FCC moved from 8,796 mAh to 8,815, 8,823, then 8,824 mAh
across reads during this session, while the MTK gauge kept claiming
2,946 mAh.

### Root cause

`mt6375-battery.ko` is built from `drivers/power/supply/mtk_battery.c`.
`battery_psy_get_property()` computed:

```c
case POWER_SUPPLY_PROP_CHARGE_FULL:
    val->intval = gm->fg_table_cust_data.fg_profile[
        gm->battery_id].q_max * 1000;
    break;
case POWER_SUPPLY_PROP_CHARGE_COUNTER:
    val->intval = gm->ui_soc * gm->fg_table_cust_data.fg_profile[
        gm->battery_id].q_max * 1000 / 100;
    break;
```

`q_max` is loaded from the gauge node's `g-q-max` DT property by
`fg_read_dts_val_by_idx()` (property names are lowercased and `_` becomes
`-`). The board overlay does not provide `g-q-max`, so the driver keeps
the compiled fallback table in
`drivers/power/supply/mtk_battery_table.h`:

```c
int g_Q_MAX[MAX_TABLE][TOTAL_BATTERY_NUMBER] = {
    /*bat1,  bat2,  bat3,  bat4*/
    { 2946, 2712, 2490, 1965}, /*T0*/
    ...
};
```

Battery 0 therefore got 2,946 mAh. The FCC measured by the SH366003 never
reached the MTK gauge.

### First fix (static table, superseded)

Battery 0 in `g_Q_MAX` was first set to **8,796 mAh** for all ten
temperature rows. That produced `charge_full=8796000` and
`charge_counter=3694320` at 42%, and was flashed and validated on Slot B
(`vendor_boot_b 93f678b5…`, `vbmeta_b 2c5ed2ec…`).

It was superseded because the SH366003 FCC is learned and changes over
time (8,796 → 8,824 mAh in this session). A static table would silently
drift away from the gauge, and `charge_counter` would no longer match
`capacity × FCC`.

### Final fix (live gauge FCC)

`battery_psy_get_property()` now prefers the live SH366003 FCC for both
properties:

- `POWER_SUPPLY_PROP_CHARGE_FULL`: read
  `POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN` from `3rd-gauge`, multiply by
  1,000, and report it.
- `POWER_SUPPLY_PROP_CHARGE_COUNTER`: use the same live FCC and
  `ui_soc` (`ui_soc × FCC / 100`).

The stock `3rd-gauge` driver exposes the SH366003 register `0x12`
(learned FCC) as `CHARGE_FULL_DESIGN`; it is not the marketing nameplate.
If `3rd-gauge` is unavailable or returns a non-positive value, both
properties fall back to the compiled `g_Q_MAX` table. Battery 0 remains
8,796 mAh there as a fallback only. `q_max_h_current` is untouched, and
the SH366003 continues to own the UI `capacity`.

### 8,824 mAh vs the 10,600 mAh nameplate

The marketing figure is 21,200 mAh at 3.87 V, which is 10,600 mAh at
7.74 V (2S). That is the nameplate, not necessarily the gauge's design:

- `FCC / SoH = 8,824 / 0.94 ≈ 9,387 mAh`, suggesting a gauge design
  around 9.4 Ah if SoH is an FCC/design ratio.
- The AFI data-flash begins with four `4,530 mAh / 4,350 mV` records
  (`11 b2 10 fe` ×4), consistent with 2S2P 4,530 mAh cells ≈ 9,060 mAh
  design. The mapping is not yet confirmed.
- `8,824 / 10,600 = 83.2%`, i.e. 16.8% below the nameplate, not exactly
  the 20% in `10,600 × 0.8 = 8,480`.
- `SoH=94`, `cycnt=31`, and `health=Good` argue against a 17% capacity
  loss, but SoH is not proven to be a pure FCC/design ratio.

The two hypotheses are therefore still open: real degradation, or a
nameplate higher than the gauge design. The dynamic fix is correct under
both: it reports what the gauge has actually learned. Forcing 10,600 mAh
would overstate usable charge by ~20% and break consistency with
`charge_counter` and `capacity`. A full charge/discharge learning cycle
can be run later to see whether FCC climbs toward 10,600 or stays near
8,800.

### Why not the board DTBO

The obvious alternative is to add `&mtk_gauge { g-q-max = <...>; }` to
`gq5012bf1-board.dts`. That is **not** safe on this device. LK
authenticates the Android dtbo image with a MediaTek cert1/cert2 chain
before applying overlays. The bootloader log in `expdb` shows:

```
lk_dtbo image name found
mkimg hdr detected (44568)
img_auth_required = 1
[SEC] lk_dtbo cert chain vfy pass
[SEC] image lk_dtbo header auth pass
[SEC] image lk_dtbo auth pass
dtbo_size=44504
```

Editing the overlay would invalidate the MTK signature. `vendor_boot`
has no such cert1/cert2 blob; it is a normal Android boot image verified
by AVB, and its hash descriptor is signed by the test key we already
control. The module fix therefore stays inside the AVB trust path.

### Validation (Slot B, dynamic flash 2026-10-31)

Flashed `vendor_boot_b` (`33771522…`) and `vbmeta_b` (`d55c6450…`) in
bootloader mode; `fastboot getvar unlocked` returned `yes`, Slot `b`.
Partition readback after boot matches both image hashes exactly. The
vendor_boot hash descriptor digest changed from `6d5801de…` to
`3ca2a0a1…`; only that descriptor was patched in `vbmeta_b`, the header
and root public key are unchanged, and the new signature verifies.

- `battery/charge_full` = `8824000`, exactly
  `3rd-gauge/charge_full_design (8824) × 1000`. It now follows the gauge.
- `battery/charge_full_design` = `8824000` for the same reason.
- `battery/charge_counter` = `3706080`, exactly 42% of `8824000`
  (was `3694320`, 42% of the old static `8796000`).
- `battery/capacity` = 42 and `3rd-gauge/capacity` = 42.
- `mt6375_battery` loaded (`lsmod`), coresize 372736. The module in the
  flashed ramdisk hashes to `6a017942…` (unstripped build
  `b3786ed1…`). The kernel emits the usual out-of-tree taint when it
  loads.
- `ro.boot.verifiedbootstate=green`, `veritymode=enforcing`,
  `flash.locked=1`; no AVB or dm-verity errors in dmesg.
- `h2w=1` with the headset attached; `mt6369_accdet` still loaded; the
  mounted `/vendor_dlkm/lib/modules/mt6369-accdet.ko` hashes to
  `f8388511…`, matching the AUX build artifact (AUX fix preserved).
- `system_server` running (PID 1805); tombstone count unchanged at 64;
  `pstore` empty.
- CMDQ/display: the fresh post-flash boot shows no `cmdq timeout` lines
  in the current dmesg ring so far. The defect is intermittent and
  remains open as section 2.

### Rollback

All hashes are SHA-256 of the full partition image.

Current dynamic images (flashed):

| Image | Path | SHA-256 |
| --- | --- | --- |
| `vendor_boot_b` dynamic | `build/battery-gauge/package/images/vendor_boot_b-dynamic.img` | `33771522b731da4cd3afb0c6e30831cedd75bfcaa6657560c4a5b9e11f02eae6` |
| `vbmeta_b` dynamic | `build/battery-gauge/package/images/vbmeta_b-dynamic.img` | `d55c64509b3f8d1bfb1b7ca9af87d71b286ada9368a8878da70891d3b96a0604` |
| `mt6375-battery.ko` dynamic | `build/battery-gauge/package/mt6375-battery-dynamic.ko` | `6a017942441ed10ec9e001cd4fe6aef405e64acff221cc30054272e1032631a2` |

Immediate rollback to the previously validated static-FCC pair:

| Image | Path | SHA-256 |
| --- | --- | --- |
| `vendor_boot_b` static | `build/battery-gauge/rollback/vendor_boot_b-static.img` | `93f678b56fa5e3884915cac07c83124f028d46b620d02b4806aaf6745450e9b3` |
| `vbmeta_b` static | `build/battery-gauge/rollback/vbmeta_b-static.img` | `2c5ed2ec3bb146320678a92bdca745277e7314fcbe57bd04b6c12df88f9432fd` |
| `mt6375-battery.ko` static | `build/battery-gauge/rollback/mt6375-battery-static.ko` | `ae8aadbe6a9a3afceececa81587a70ccc00a4087636a706d422e100721b56d16` |

Deeper rollback to the pre-battery AUX baseline remains available:

| Image | Path | SHA-256 |
| --- | --- | --- |
| `vendor_boot_b` baseline | `build/battery-gauge/rollback/vendor_boot_b-baseline.img` | `d251ac5829fdbeef66c36d35ccee083dd2535aed3fa25346aab6dcb7ecfdc3a7` |
| `vbmeta_b` AUX | `build/battery-gauge/rollback/vbmeta_b-aux.img` | `96e6f498f5908ece9c83d73734883d5132e773f986f278fd725d52491557ada6` |
| `vendor_dlkm_b` AUX | `build/battery-gauge/rollback/vendor_dlkm_b-aux.img` | `4a3e89f0ba3cd65da61621abb8e144d2457834274971167f4dd3153799bdd195` |
| `dtbo_b` (untouched) | `build/battery-gauge/rollback/dtbo_b.img` | `9092967872d1362c45a18d62a83360b742eed58aafa4b0c15043ef159ae042d3` |

Fastboot rollback to the static pair (same Slot B; substitute the
device serial):

```sh
fastboot -s <serial> flash vendor_boot_b build/battery-gauge/rollback/vendor_boot_b-static.img
fastboot -s <serial> flash vbmeta_b build/battery-gauge/rollback/vbmeta_b-static.img
fastboot -s <serial> reboot
```

The dtbo partition is not touched by this fix.

---

## 2. CMDQ / display timeouts on refresh-rate switches (VM sender deployed; line artifacts fixed by `f0efdb86e`)

**Status 2026-11-01:** `187f8de4c` (VM sender) was deployed (vm-dfps, then the
TASK_IDLE vendor_boot) and removed the timeouts, but each 60/90/120 switch still
showed a one-frame band of brighter/dimmer lines. `f0efdb86e` follows the stock
provider for `change_fps_by_vfp_send_cmd` panels: stop video at frame end, write
VFP, send `6c` with the normal engine, restart video. Flashed to Slot B
(`vendor_boot_b` `7106da37…`); owner-verified lines gone, 64 switches without
CMDQ timeouts. The text below is the historical record.

**Not hardware-fixed.** The `b62bc26e8` pre-poll-only candidate was tested with
owner authorization on 2026-10-31, failed, and was rolled back. The revised
VM-command implementation below is built/tested offline only, not packaged,
signed, flashed or device-validated.

### Failed device test and completed rollback

The earlier candidate (`mediatek-drm.ko` SHA256
`67e54bb3871aab18a1e51c9431c9fe1f882f072db7faa02c8ad3b7f21eebbea4`) booted but
still produced 12 matching timeout **marker lines** in each direction of the
first 60/120 Hz cycle. Actual display modes were 120.00001 and 60.0 Hz. Marker
lines are not unique callbacks: pre-dumps, duplicate callback messages and
queued-packet dumps must not be counted as independent failures. The owner
reported display malfunction and requested immediate rollback.

Settings restoration succeeded. The previous brightness pair was restored to
Slot B, Android boot completed, both partition hashes matched the originals,
and the captured Slot A vendor_boot/vbmeta hashes were unchanged. Boot properties
reported green/enforcing (not an independent hardware-attestation claim).
Current restored images:

- `vendor_boot_b-brightness-avb.img`:
  `bc6718f2bd63888b05a5da8a658ed748589a0cb7b2eb8bb341243e18640ee0dc`
- `vbmeta_b-brightness.img`:
  `21ea74788eb308452befb9c6ffe82fcec14d3f1ad1e3cd0cf1cdef6690f35f70`

The rejected package remains only as evidence; **do not flash it again**.
`build/cmdq-vdo-poll/FLASH-PREP-cmdq-vdo-poll.json` records failure/rollback,
`ROLLBACK-RECEIPT.json` verifies restoration, and the existing flash helper now
refuses that rejected candidate. The helper also requires explicit mode/serial,
checks current installed hashes, restores exact settings on failures, resolves
actual mode refresh rates, and establishes 60 Hz before requesting a 120 Hz
transition. The failed test's final full packet capture was not recovered;
its exact post-patch stalled PC is therefore not claimed.

### Evidence-backed source diagnosis

Despite `cmd` in its filename/compatible, the VTDR6115 panel sets
`MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE` in its source.
VFP-only refresh changes leave this video stream running. The old call path was
`mtk_dsi_vdo_timing_change()` -> `mtk_dsi_send_switch_cmd_nothing()` ->
`mtk_dsi_cmdq_pack_gce()`.

A newly captured **restored-baseline** packet shows:

- frame EOF wait on event 162;
- a poll of `0x1401a00c` (`DSI_INTSTA`) masked by `0x80000000` (`DSI_BUSY`);
- timer-event-997 wait at packet offset 0xa8 in that pre-write poll;
- later normal-command writes and `DSI_START` values 0/1/0;
- another poll of the same stream BUSY bit after the start sequence.

The failed patch removed only the first poll. It retained normal command-queue
writes, normal-engine START and the second stream-BUSY poll. That does not turn
a normal command transaction into a video-mode command transaction. The earlier
assertion that the post-poll was necessarily safe was unsupported.

Also, `mtk_dsi_cmdq_poll()` passes `count=0xffff`. In
`cmdq_pkt_poll_timeout_reuse()`, U16_MAX explicitly disables the loop limit:
this is **unbounded polling**, not a 65535-iteration timeout. The separate CMDQ
thread watchdog remains enabled; its log is not proof of normal completion.

The existing `mipi_dsi_dcs_write_gce()` already distinguishes the two engines:
its video branch programs driver-specific VM command registers, toggles only
`VM_CMD_START` (bit 16), clears and polls `VM_CMD_DONE` (bit 5), without a stream
BUSY poll. This is also the existing video brightness sender. Video brightness
and VFP mode changes both use `CLIENT_DSI_CFG`, preserving channel serialization.

### Revised source correction (offline candidate)

In `drivers/gpu/drm/mediatek/mediatek_v2/mtk_dsi.c`:

- VDO DFPS commands go through the existing `mipi_dsi_dcs_write_gce()` VM path;
  source-FPS conditions are checked per table entry, in order.
- Empty command tables emit nothing.
- Command-mode packing and its pre/post idle checks are retained; the ineffective
  `b62bc26e8` pre-poll-only change is undone.
- The caller's frame EOF wait, VFP programming, trigger loop, panel tables,
  brightness fallback and timeout diagnostics are unchanged. No new register
  values or exported interfaces are invented.

### Actual offline validation

- New `tools/ulefone/gq5012bf1/tests/display/` tests extract the real sender,
  packing, VM and mode-selection function bodies/constants. Recording host
  shims expose emitted operations; they do not execute Linux/GCE or a panel.
- The old candidate fails the VDO routing regression. Revised code passes
  **4/4 normal and 4/4 ASan/UBSan** groups: 60/90/120 payload/VM completion,
  per-entry source-FPS/order, absent/empty inputs, and retained command/mixed
  mode checks. Production patch checkpatch: zero errors/warnings.
- Real ARM64 Clang r487747c compile, modpost and module linkage PASS using the
  established C29/C56 recipes (including the prior ESD flag configuration).
  Only `mtk_dsi.o` differs from the brightness baseline; the other two rebuilt
  objects are byte-identical. No baseline outputs/config were rewritten.
- Actual ELF imports **546/546** and exports **56/56** match the diagnostic
  reference. The old receipt incorrectly called its 56 exported `SYMBOL_CRC`
  records imports. Vermagic/dependencies/name/license are unchanged, no new
  exports. All 128 named KCFI type IDs and eight selected DSI entry type words
  match the brightness reference; this is scoped offline ABI evidence only.
- New module (5,681,872 bytes):
  `7249d78681e9090c9996c4ae6f96549851aaa28625781bb9b8ca21e7c1e1467a`.

Reproduce host checks from the kernel repository:

```sh
python3 -B tools/ulefone/gq5012bf1/tests/display/run-tests.py \
  --work-dir "$PWD/../../../../build/cmdq-vdo-poll/offline-tests"
```

Receipts: `build/cmdq-vdo-poll/receipt-vm-dfps.json`,
`offline-review-vm-dfps.json`, `offline-tests/{before,after}-fix.log`.
The existing tagged builder accepts a fresh `--tag` and refuses overwrites.
Private read-only captures are under
`build/cmdq-vdo-poll/readonly-diagnostics-20261031T112212Z/`;
no raw logs/device identifiers belong in source or public research.
**Research-ready handoff: not yet integrated into the canonical research repo.**

### Remaining proof / limits

The phone was disconnected after the authorized read-only capture. No new image
was prepared or flashed. A future separately authorized package/test must check
VM completion, repeated 60/90/120 transitions, visual stability/brightness,
suspend/wake and retained battery/headset behavior. Failure means restore the
known brightness pair, not suppress timeouts or delete further completion checks.
Event-162 sharing and missing event names/client-1 ENODEV remain separate
questions; no bounded latency guarantee or blanket harmlessness is claimed here.

---

## 3. Benign / known observations

These were checked during the health sweep and are not regressions:

- 6 SELinux `mapper/mediatek` denials.
- 243 `ta_rdy fail` fingerprint log lines; the HAL is alive and one
  print is enrolled.
- Load average around 15, mostly D-state MTK kernel threads.
- KDE Connect listener on TCP 1716; `adb_wifi_enabled=1`.
- Root modules expected: SUSFS, Zygisk, TrickyStore, PIF, Specter.
- `pstore` empty; no new crashes or panics; no dm-verity or EXT4 errors.

---

## 4. AUX/headset fix (already validated, preserved)

The MT6369 ACCDET TRRS work on `origin/integration/aux-detection` is
unaffected by this branch:

- PS4 headset with mic: `MICROPHONE(4-pole)`, `h2w=1`.
- Muted/plain AUX: `h2w=2`.
- Unplug/replug is clean; `system_server` PID 1855 stable; crash count 6.

Commits: `e9eadab7e`, `02d1effb7`, `b91b87ad2`, `b21bde9d9`.

## 5. USER. DISPLAY HDR, Display Brightness does nothing AND refresh rate in settings

**Status 2026-11-01:** brightness fixed (below); refresh selectors and Smooth
Display shipped as a product RRO; HDR brightness enabled via the 12-bit HBM
range (device `3042a26`: DTB backlight max 4095 + product display config).
Owner-verified: HDR video boosts to DBV 4094 (~926 nits), SDR stays 513 nits.

### Brightness root cause (2026-10-31)

Brightness writes reach the LED driver but are dropped before any DSI
command. `mtk_drm_setbacklight()` selects `DSI_SET_BL` for the LED path
(`cfg_flag = 1 << SET_BACKLIGHT_LEVEL`) and for the composer/CSC_BL path
(`mtk_dsi_set_backlight()`). `mtk_dsi_io_cmd()` handles `DSI_SET_BL` by
calling `panel_ext->funcs->set_backlight_pack` only:

- `drivers/gpu/drm/mediatek/mediatek_v2/mtk_dsi.c:10758`
- running `mediatek-drm.ko`: `ldr x8, [x8, #0x18]`; `cbz` skips when NULL.

This panel only provides slot 2, `set_backlight_cmdq`:

- `drivers/gpu/drm/panel/panel-ky-vtdr6115-dphy-cmd.c:750`
- running `panel_ky_vtdr6115_dphy_cmd.ko` `ext_funcs+0xe0` -> `.text+0x12c8`
  (`lcm_setbacklight_cmdq`); `ext_funcs+0xe8` (slot 3) is NULL.

Result: `lcm_setbacklight_cmdq` is never called (dmesg count 0), no DCS
`0x51` is sent, and the panel stays at the init value written by
`lcm_panel_init()` (`0x51 0x07 0xFF`, 2047). The LED class value still
changes, so the slider appears to work but the panel does not.

Fix (applied on `integration/brightness-cmdq-fallback`, flashed to Slot B,
2026-10-31): in `mtk_dsi_io_cmd()` `DSI_SET_BL`, fall back to slot 2 when
slot 3 is absent:

```c
else if (panel_ext->funcs->set_backlight_cmdq)
    panel_ext->funcs->set_backlight_cmdq(dsi,
        mipi_dsi_dcs_write_gce, handle, *(int *)params);
```

`mediatek-drm.ko` was rebuilt from the saved C29 recipes using the C56
diag source plus this fallback (sha256
`8e9f911d09672dd7a31d9ed4da059b74b0cfb806379e62e7b49ce25d556fa311`;
import and export CRCs identical to the running module). The new
vendor_boot_b/vbmeta_b were re-signed and flashed to Slot B; the
dynamic-FCC `mt6375-battery.ko`, DTB, and recovery ramdisk are
byte-identical to the battery-fix image.

Validation (Slot B, `green` / `enforcing` / locked):

- 486 `lcm_setbacklight_cmdq` entries in dmesg (0 before the fix).
- The boot brightness ramp includes direct `200 0 200` and
  `2047 7 255` calls, proving `DSI_SET_BL` reaches the panel callback.
- Framework path end-to-end: `settings put system screen_brightness 50`
  (mode 0) ramps the panel down to 398
  (`lcm_setbacklight_cmdq 398 1 142`); restoring 255 ramps back to
  2047. The brightness slider now changes the panel.
- Sysfs `lcd-backlight/brightness` is a separate path: `leds_mtk`
  applies it directly ("Set lcd-backlight directly") and bypasses
  `DSI_SET_BL`, so it is not a test of the fallback.
- Battery regression check: `charge_full` equals the 3rd-gauge FCC x
  1000 (8,822,000 uAh at 8822 mAh); `mt6375_battery` loaded;
  `system_server` alive; tombstones unchanged at 64.

Separate, not causal: CMDQ timeouts recur during brightness ramps and
mode switches (`mtk_crtc_cmdq_timeout_cb`, crtc 0, thread 6). This boot
shows one at 48.016630 s (reported at 48.84 s) and more at 127-129 s
and 219-232 s, with `mtk_drm_esd_testing_process` in the timeout
stack. The brightness callback was previously skipped before a DSI
command was queued, so fixing CMDQ alone would not restore brightness.
Refresh-rate behavior and the CMDQ/DT event gap remain separate open
items (section 2).

## 6. Fm Radio
## 7. 5G, NoVR option. Make show numbers of the SIM cards.

### VoLTE / VoNR (enabled 2026-11-01, slot B; device tree `ims/`)

Status: VoLTE works on both SIMs (Bite LT 246-02, Telia LT 246-01); calls
go over IMS. The VoNR switch is shown and works in both directions; an actual
VoNR call needs 5G SA coverage and has not been observed yet (LTE only at the
test location). "Show numbers of the SIM cards" is still open.

- Nothing bound IMS before: no `config_ims_mmtel_package`, and the vendor-tree
  stock `com.mediatek.ims` (v35) cannot run here: it needs 24 classes from the
  `mediatek-*` framework jars, which are not on the AOSP boot classpath.
- Used the community unified MediaTek ImsService (v33). Against the phone's
  boot classpath the only missing class was
  `com.android.internal.telephony.metrics.TelephonyMetrics` (removed in Android 17),
  so a `classes2.dex` with a no-op stub was added. The APK is platform-signed
  (shared uid phone).
- It then died in `WfoService.makeWfoService`: SELinux denied `add` for `mwis`
  (labelled `default_android_service`). Added the stock `system_ext` labels
  `imsa mtkIms GbaService mwis wfo` -> `radio_service`.
- Android disabled every MMTEL capability: `config_device_volte_available` and
  `carrier_volte_available_bool` default to false and no AOSP carrier file
  exists for these carriers. Fixed with framework/phone/CarrierConfig overlays
  (`vendor.xml`; the mcc/mnc filters did not match, carrier-id 892/893 do).
- VoNR: the RIL accepted `ENABLE_VONR` but `IS_VONR_ENABLED` stayed false.
  In `libmtk-ril.so`, `triggerImsCfgCommand` recomputes the
  `persist.vendor.mtk.vonr.enable` bitmask. With
  `persist.vendor.mtk_dynamic_ims_switch=1` it takes the value from a per-carrier
  capability table filled only by the stock MediaTek IMS framework, so VoNR could
  be switched off but never on. Set the switch to 0 (product prop).
- Flashed B-only: system_ext_b `cce94d77`, product_b `d9182734`,
  vbmeta_system_b `2481b7bd`, vbmeta_b `a3efd3dc`. Slot A unchanged; boots green,
  enforcing, no IMS crashes. Rollback = minimal-media system_ext/vbmeta_system +
  HDR product/vbmeta (`build/ims/flash_b.py rollback-*`).
- Not enabled: Wi-Fi calling (no wfca/ePDG in vendor) and ViLTE.
- VoNR shown directly below VoLTE (Settings rebuilt from source; only
  `res/xml/mobile_network_settings.xml` differs, dex identical). Process error:
  the first Settings package was built on the pre-IMS `system_ext` (a script
  substitution failed silently) and briefly removed IMS. The corrected package
  (`system_ext_b d6d36bfc`, `vbmeta_system_b 22461e80`) asserts that the IMS files
  are present; IMS is registered again on both SIMs.

## 5a. Battery saver capped the refresh rate at 60 Hz (fixed 2026-11-01)

AOSP `DisplayModeDirector.updateLowPowerModeSettingLocked` always votes a
0-60 Hz render range when `low_power=1`; this panel has no
`lowPowerSupportedModes`, and Settings does not grey anything out. The vote is
now gated on `ro.lieppos.low_power_limits_refresh_rate` (default true; this
device sets false). Smali diff vs the shipped services.jar: only
`SettingsObserver.updateLowPowerModeSettingLocked`; the boot image is unchanged.
RAM-only bind-mount test, then flashed B-only `system_b 90623920`,
`vbmeta_system_b a1b17b8e`. With battery saver on, the display stays at 120/120.
Owner confirmed.
## 8. Thermocamera + LED's. IR led.

LEDs done (flashed, B only). Quick Settings tiles: Super flashlight
(off/lowest/max), Red light and Blue light (off/low/max). Red/blue are dimmed by
hardware PWM 1/2 on GPIO 20/21 (old mode, 32 kHz block clock, width 40 = ~820
Hz; the first try at 26 MHz assumptions ran at ~4 Hz and visibly blinked).
Tile lag came from the app freezer and was fixed by making the tiles app
persistent. Flashed `vendor_dlkm_b a6853c88`, `vbmeta_b f72c545d`,
`system_ext_b f35cc3d9`, `vbmeta_system_b b5884873`.
Note: rebuilt EROFS images keep fixed file mtimes, so PackageManager reused a
stale parse of the changed apk until `/data/system/package_cache` was cleared.
Thermal camera done (flashed, B only; owner: image works, no dialogs).
ThermoVue Pro (stock app, 64-bit repack) in system_ext. Root cause of "stuck
loading": the reconstructed extcon-mtk-usb asked for USB *device* role on
tiny2c_mode=1 (and toggled the uSmart switch GPIOs = the relay click); stock
asks for *host*. Fixed module (9881dfdf0) in vendor_boot_b eabadc0c / vbmeta_b
1c7655f0. Camera enumerates as UVC 3474:4321. ArmorParts powers it off if
ThermoVue dies (stock services.jar did); a services.jar allowlist
(ro.lieppos.usb_device_allowlist) removes the per-launch USB dialog.
system_ext_b 56195e1a, system_b 600bf426, vbmeta_system_b 4294e7f5.
Device tree 147c915. Note: with a PC data cable attached, the camera takes the
port (adb over USB drops while ThermoVue runs).
IR LED: open.
## 9. Android Auto, MicroG.

