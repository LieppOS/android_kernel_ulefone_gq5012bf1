# Experimental camera-bus I2C ACK probe / peek

Out-of-tree diagnostics used to look for a fitted DW9781C OIS controller after
the model-word read at `0x0a` NACKed (see `../ois-diagnostic/README.md`).
**Not drivers. Never shipped. No write phase exists in either module.**

| module | source | what it may do |
|---|---|---|
| `ois_ackscan` | `ois_ackprobe.c` | per bus 2/4/7, once each: (a) one-byte read at fixed `0x0c` (AF control), `0x72`, `0x0e`, `0x0a`; (b) one-byte read of every 7-bit `0x08`–`0x77` except a fixed skip list of driver-owned addresses |
| `ois_i2cpeek2` | `ois_i2cpeek.c` (v2) | fixed (bus, address) allowlist: I2C-4 `0c 21 22 20 51`, I2C-7 `0c 20 51`, I2C-2 `0c 10`; 1–4 byte current-address read, 16 requests total |

Both are inert until insertion with `enable_*=1` **and** an exact acknowledgement
string per request, require `CAP_SYS_RAWIO`, have no unload path and no exports.
A one-byte read sends only the address byte; it cannot change a register pointer
or value. It can still be observed by the device (EEPROM current-address pointer
advances), so driver-owned addresses are skipped.

Module names differ from source names (`Kbuild` composites) so new builds could
coexist with permanently loaded earlier ones. Builds loaded on the phone:
`ois_ackprobe` (fixed table only) `95cbe83d…`; `ois_ackscan` `065770ee…`;
`ois_i2cpeek` v1 (I2C-4 only, `0c 21 22`) `e71cbab9…`; `ois_i2cpeek2`
`723a9080…`. Rebuilding with the current `Kbuild` changes binary hashes of the
unchanged `ois_ackprobe.c` (same `srcversion`).

Build/verify (offline, private new `--out` each time):

```
build.py --kernel-source K --reference-out REF --toolchain-dir TC --out NEW
verify.py --profile {ackscan|i2cpeek} --candidate NEW/module/<ko> \
  --baseline-module imgsensor.ko --reference-out REF --crosscheck-out OTHER
```

Strict checkpatch clean; CRCs (14/15) match both reference outputs; callback and
init KCFI prefixes match.

## Hardware result (2026-11-01, Slot B, nothing flashed, no reboot)

Each bus was probed while its own camera streamed in Open Camera (all of that
module's rails on). `rc 1` = ACK, `-6` = ENXIO (NACK in `i2c-mt65xx`).

* HAL lens config (driver logs): every back AF is driven at 8-bit `0x18`
  (7-bit `0x0c`) with init `02=02 06=40 07=79`. DT `reg` values are placeholders.
* Fixed probe: `0x0c` ACK on buses 2, 4 and 7 (method + power validated);
  `0x72`, `0x0e`, `0x0a` NACK on all three.
* Inventory (skips in brackets): bus 7 `0x1a` IMX989, `0x50` EEPROM
  [`0c 20 51`]; bus 2 `0x2d` S5KJN1 [`0c 10 50`]; bus 4 `0x53` OV64B EEPROM
  (research cam_cal `0xA6`) and `0x21` [`0c 10 20 50 51`]. No non-NACK errors.
* Passive `i2c` tracepoints (private trace instance) during each open: stock
  only talks to sensor/AF/EEPROM addresses; never `0x21`.
* `0x21` peek: NACK idle, NACK with front camera, NACK with camera 0 open (shared
  AFVDD LDO5 on, OV64B in reset; control `0x0c` on bus 4 ACKs), ACK only with
  OV64B open; current-address reads return `00`. OV64B alternate id `0x22` NACK.
  So `0x21` follows OV64B's own enables, not the actuator rail. Most consistent
  with a second responder on the sensor die; unproven.

* Skipped DT placeholder addresses (`ois_i2cpeek2`, each with its camera
  streaming): I2C-7 `0x20`, `0x51`; I2C-4 `0x20`, `0x51`; I2C-2 `0x10` all NACK,
  while the `0x0c` control on each bus ACKs. Addresses stock actually uses
  (sensor, EEPROM, AF) were the only ones never probed.
* Offline stock firmware: factory-mode menu (`vendor/etc/factory.ini`) has
  camera and lens (AF) items but no OIS/hall item; `vendor/bin/factory`,
  `meta_tst`, stock `EngineerMode.apk` contain no OIS strings; stock
  `Camera.apk` exposes EIS only (`config_eis_support`, MTK `EIS`/`EIS3rd`);
  `libcameracustom.lens.so` OIS lists are `Dummy`; no OIS property or config.

**Conclusion:** no DW9781C-like responder on the back-camera buses under any
camera's power, and stock firmware has no OIS feature, test or calibration.
OIS controller very likely not fitted. Not proof: other buses and 10-bit
addressing untested; only teardown is definitive.

All three modules stay loaded until the next normal reboot. Private raw records:
`build/ois-diagnostic/runtime-ackprobe/` (contains EEPROM bytes; never publish).
