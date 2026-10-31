# GQ5012BF1 (Ulefone Armor 29 Pro) — Open Issues

Device: Ulefone Armor 29 Pro (GQ5012BF1), MediaTek MT6878, Android 17
userdebug/test-keys, Slot `_b`, verified boot `green`, verity `enforcing`.

Kernel: `6.1.115-mt6878-gq5012bf1-c29+`

Recorded: 2026-10-31

---

## 1. Battery capacity is under-reported by the MTK gauge (fixed on `integration/battery-gauge-fix`)

### Symptom

The `battery` power supply reports the MTK PMIC gauge's compiled
default instead of the real pack capacity:

```
battery/charge_full        = 2946000   # 2946 mAh
battery/charge_full_design = 8807000   # SH366003 design (drifts 8.807-8.815 Ah)
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

So the real full-charge capacity (FCC) is 8796 mAh, while the MTK gauge
claims 2946 mAh.

### Root cause

`mt6375-battery.ko` is built from `drivers/power/supply/mtk_battery.c`.
`battery_psy_get_property()` computes:

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

Battery 0 therefore gets 2946 mAh. The 8796 mAh measured by the SH366003
never reaches the MTK gauge.

### Fix

Set battery 0 in the compiled `g_Q_MAX` table to **8796 mAh** for all ten
temperature rows, rebuild `mt6375-battery.ko`, replace the copy in the
vendor_boot ramdisk (`lib/modules/mt6375-battery.ko`), and re-sign
`vendor_boot_b` plus `vbmeta_b` with the existing AOSP test key.

`q_max_h_current` is intentionally left at the platform default. The
reported `charge_full` and `charge_counter` use `q_max`; the SH366003
continues to own the UI `capacity`, which was already correct.

### Why not the board DTBO

The obvious fix is to add `&mtk_gauge { g-q-max = <...>; }` to
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
control. The module-table fix therefore stays inside the AVB trust path.

### Validation (Slot B, flashed 2026-10-31)

Flashed `vendor_boot_b` (`93f678b5…`) and `vbmeta_b` (`2c5ed2ec…`) in
bootloader mode; `fastboot getvar unlocked` returned `yes`, Slot `b`.
Partition readback after boot matches both image hashes exactly.

- `battery/charge_full` = `8796000` (was `2946000`) — the new `g_Q_MAX`
  table is active.
- `battery/charge_full_design` = `8815000` in this sample. The SH366003
  design value drifts between 8.807 and 8.815 Ah across reads; the fix
  does not touch it.
- `battery/charge_counter` = `3694320`, exactly 42% of `8796000`
  (was `1237320`, 42% of `2946000`).
- `battery/capacity` = 42 and `3rd-gauge/capacity` = 42.
- `mt6375_battery` loaded (`lsmod`), coresize 372736. The module in the
  flashed ramdisk hashes to `ae8aadbe…`, matching the package. The kernel
  emits the usual out-of-tree taint when it loads.
- `ro.boot.verifiedbootstate=green`, `veritymode=enforcing`,
  `flash.locked=1`; no AVB or dm-verity errors in dmesg.
- `h2w=1` with the headset attached; `mt6369_accdet` still loaded;
  `vendor_dlkm_b` still hashes to `4a3e89f0…` (AUX fix preserved).
- `system_server` running (PID 1968); tombstone count unchanged at 64;
  `pstore` empty.
- CMDQ/display: the fresh post-flash boot shows no `cmdq timeout` lines
  in the current dmesg ring so far. The defect is intermittent and
  remains open as section 2.

### Rollback

All hashes are SHA-256 of the full partition image.

| Image | Path | SHA-256 |
| --- | --- | --- |
| `vendor_boot_b` baseline | `build/battery-gauge/rollback/vendor_boot_b-baseline.img` | `d251ac5829fdbeef66c36d35ccee083dd2535aed3fa25346aab6dcb7ecfdc3a7` |
| `vbmeta_b` current (AUX) | `build/battery-gauge/rollback/vbmeta_b-aux.img` | `96e6f498f5908ece9c83d73734883d5132e773f986f278fd725d52491557ada6` |
| `vendor_dlkm_b` current (AUX) | `build/battery-gauge/rollback/vendor_dlkm_b-aux.img` | `4a3e89f0ba3cd65da61621abb8e144d2457834274971167f4dd3153799bdd195` |
| `dtbo_b` (untouched) | `build/battery-gauge/rollback/dtbo_b.img` | `9092967872d1362c45a18d62a83360b742eed58aafa4b0c15043ef159ae042d3` |

The rollback set is mirrored under `build/battery-gauge/rollback/` and
recorded in `build/battery-gauge/ROLLBACK-COPIES.json`; every hash above
was re-verified against `BASELINE.json` after the flash.

Fastboot rollback (same Slot B; substitute the device serial):

```sh
fastboot -s <serial> flash vendor_boot_b build/battery-gauge/rollback/vendor_boot_b-baseline.img
fastboot -s <serial> flash vbmeta_b build/battery-gauge/rollback/vbmeta_b-aux.img
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
