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

## 2. CMDQ / display timeouts (open, pre-existing)

### Symptom

The boot inspected during the health sweep contained 138 `cmdq timeout`
messages. Additional evidence:

- `cmdq_mbox_create` fails with `-19` (`ENODEV`) for index 1.
- The GCE event list is missing names required by the display paths:
  `disp_token_sodi0`, `disp_rdma0_eof0`, `disp_wdma0_eof0`.
- A previous boot's ramoops contains `mtk_crtc_cmdq_timeout` at 188 s.

Display output still works and the device does not panic, so this is a
latency/reliability defect rather than an immediate boot blocker.

### Root cause

The board overlay / base DTB does not describe every GCE event used by
the display driver. `cmdq_mbox_create()` cannot find the mailbox for
index 1 and returns `-19`; the missing `gce-event-names` entries then
force timeout-based fallbacks in the display command queue.

This is a platform DT/display integration gap. It is unrelated to the
battery gauge and unrelated to the MT6369 AUX/headset work.

### Status

**Documented, not fixed in this pass.** The battery flash must not be
mixed with an unvalidated display/CMDQ change.

Next step: audit the `gce-event-names` list against the events required
by the MT6878 display driver and the platform reference DTS, add the
missing events, and validate in a separate display campaign with a
dedicated rollback path.

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
## 8.
