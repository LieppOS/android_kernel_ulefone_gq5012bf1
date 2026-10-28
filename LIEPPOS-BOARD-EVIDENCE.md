# GQ5012BF1 board evidence and activation state

This file records the provenance and activation state of the GQ5012BF1 board
device-tree source.  It is source evidence, not a hardware-validation claim.

Campaign 17 split the board source into two explicit profiles:

- `arch/arm64/boot/dts/mediatek/gq5012bf1-board.dts` - **TRUSTED**.  Uses only
  properties independently supported by permitted evidence.  Every numeric
  hardware path that is not independently established stays disabled.
- `arch/arm64/boot/dts/mediatek/gq5012bf1-board-pending.dts` -
  **PROVENANCE_PENDING**.  Preserves the Campaign 16 offline-copy values for
  offline comparison.  It is not built by default and must not enter a
  trusted boot-readiness package.

## Evidence classes

| Class | Meaning |
|---|---|
| `TRUSTED` | Independently established through permitted evidence, including the authorized stock module ELF identity set and the MT6878 platform reference DT. |
| `PROVENANCE_PENDING` | Supported only by the offline stock vendor_boot/DTBO copy whose access lineage is not yet authorized. Retained for comparison; not trusted. |
| `UNKNOWN` | No authenticated value; not invented. |
| `DISABLED` | Hardware path deliberately inactive because required wiring, provider, or safety evidence is absent. |

## Provenance incident record

- A broad `/home/<user>` directory-name search performed during Campaign 16
  emitted directory names from inside the protected
  `/home/<user>/<protected-tree>/` tree once.
- No file contents were read or copied by that search.  No script was run from
  the protected tree and no symlink into it was followed deliberately.
- This was **not** perfect compliance with the no-traversal rule.  The incident
  is recorded rather than described as clean.
- Campaign 17 did not repeat any broad `/home/<user>` discovery.  Searches were
  scoped to authorized project roots.
- The offline stock `vendor_boot` FDT / DTBO copy is stored under the authorized
  workspace, but its original extraction lineage is not independently
  authorized.  Moving or re-hashing the copy does not make the lineage clean.
  Its numeric values are therefore `PROVENANCE_PENDING`, not `TRUSTED`.

The offline copy hashes are preserved for audit only:

| File | SHA-256 |
|---|---|
| `vendor_boot.dtb` (342,331 B) | `9ee54c6c02a3b6d8a3c47e1a43e2cefa0953944ebaee6acb858f97437322fd53` |
| `dtbo-entry-00.dtbo` (44,504 B) | `63143a4378081dd8d0d5bdf72dca83a660ecbe6c0ef7520071d8542cc360558d` |
| `gq5012bf1-merged.dts` (457,156 B) | `4377b1aaefecee611083494045a83258b7daaa1d6539a0d56e52510f016a2868` |
| `gq5012bf1-merged.dtb` (372,260 B) | `7ce69ab5bb4f9714446a262966d0478409a7e00a70c8f6d781461f9abb301104` |

## Trusted properties

These properties are independently supported and appear in the trusted
profile.  Compatible strings are read from the authorized stock module ELF
`of_device_id` tables, not from the disputed DT copy.

| Node / property | Value | Evidence | Activation |
|---|---|---|---|
| main panel binding | `hx,vtdr6115,cmd,120hz,ky` | stock `panel-ky-vtdr6115-dphy-cmd.ko` OF alias | node present, `disabled` |
| touch binding | `focaltech,fts` | stock `focaltech_touch_spi_ft3680.ko` OF alias | node present, `disabled` |
| fingerprint transport binding | `mediatek,madev_finger` | stock `microarray_fp_tee.ko` OF alias | node present, `disabled` |
| YFT fingerprint binding | `mediatek,yft_finger` | stock `fingerprint.ko` OF alias | node present, `disabled` |
| YFT device-info binding | `mediatek,yft_devices` | stock `yft_devinfo.ko` | node present, `disabled` |
| YFT touch-gesture binding | `mediatek,yft_tpd` | stock `yft_tpd_gesture.ko` | node present, `disabled` |
| YFT gpio-keys binding | `yft-gpio-keys` | stock `yft_gpio_keys.ko` OF alias | node present, `disabled` |
| YFT tiny2c USB binding | `mediatek,yft_tiny2c_usb` | stock `yft_tiny2c_usb.ko` OF alias | node present, `disabled` |
| YFT camera-light binding | `mediatek,yft_camplight` | stock `yft_camplight.ko` | node present, `disabled` |
| speaker amplifier binding | `mediatek,speaker_amp` | stock `mtk-sp-spk-amp.ko` OF alias | binding documented; no trusted bus/GPIO node |
| display backlight | `mediatek,disp-leds`, `led-mode = <4>` | MT6878 platform reference DT | enabled |
| `dsi0`, `mtkfb` | controller status | MT6878 platform reference DT | enabled |
| reference FT3519 / Goodix / SC8541 / UPM6720 / AW882xx / camera nodes | wrong reference parts | stock module identity set | explicitly disabled |

The trusted profile intentionally contains no panel GPIOs, no touch bus/GPIO
assignment, no fingerprint SPI/GPIO/TEE transport, no YFT GPIO maps, no
speaker-amplifier bus address or GPIOs, no power/NFC/fuel-gauge children, and
no camera graph.

## Provenance-pending properties

These Campaign 16 values are preserved in
`gq5012bf1-board-pending.dts`.  They are technically plausible but are not
independently authenticated.  They must not be treated as trusted board
configuration.

| Node / property | Campaign 16 value | State |
|---|---|---|
| panel reset / powerdm / dvdd / vci | GPIO 90 / 194 / 38 / 39 | PROVENANCE_PENDING; disabled in trusted profile |
| `dsi0.switch-fps` | 120 | PROVENANCE_PENDING |
| touch transport | SPI3 CS0, 6 MHz, reset GPIO6, IRQ GPIO7, VDDI GPIO183, VDD `mt6369_vtp`, 1080x2400 | PROVENANCE_PENDING; disabled |
| fingerprint transport | SPI1 CS0, CS GPIO61, 8 MHz, VFP supply | PROVENANCE_PENDING; disabled |
| YFT fingerprint wiring | reset GPIO30, IRQ GPIO3, 16 pinctrl states | PROVENANCE_PENDING; disabled |
| YFT device-info GPIO | GPIO186 | PROVENANCE_PENDING; disabled |
| YFT gpio-keys | GPIO13/8, KEY_F1/KEY_F2, active low | PROVENANCE_PENDING; disabled |
| YFT tiny2c USB | GPIO192/191/149 | PROVENANCE_PENDING; disabled |
| YFT camera light | GPIO74/91/153/20/21 and PWM states | PROVENANCE_PENDING; disabled |
| speaker amplifier | I2C6 0x34, reset GPIO193, IRQ GPIO41, re 1k-40k | PROVENANCE_PENDING; not activated |
| SC8571 / SC851X / WL2864C power children | bus/address/IRQ/tuning values | PROVENANCE_PENDING; disabled |
| ST21NFC | I2C6 0x08 and GPIOs | PROVENANCE_PENDING; disabled |
| four-sensor camera graph / flashlight | sensor addresses, EEPROMs, VCMs, AW36515 | PROVENANCE_PENDING; disabled |
| fuel gauge | SH366003 bus mapping | UNKNOWN/PROVENANCE_PENDING; disabled |

## Build results

The trusted overlay compiles cleanly and applies offline on top of the
source-built MT6878 DTB:

| Artifact | Size | SHA-256 |
|---|---|---|
| `gq5012bf1-board.dtbo` | 3,480 B | `e9896d333cd3be163a890df6eed525fe6770dd73a6e0c4e635eb7994e8459c8a` |
| trusted merged DTB (`mt6878.dtb` + overlay) | 339,846 B | `b9161b2eb946123c1fd62599b354f08454e249df74e3e61891966431d5ffddd0` |

The provenance-pending overlay also compiles and applies offline when
explicitly enabled:

| Artifact | Size | SHA-256 |
|---|---|---|
| `gq5012bf1-board-pending.dtbo` | 14,206 B | `a34b20311d56a2a2c7387724e1e5ca857f7b0bdd19217e4b71276f0d9a546583` |
| pending merged DTB (`mt6878.dtb` + overlay) | 340,368 B | `a87d6f491fa26f932af0dc4c4c443368f5b25a1897314f48540808367a00a12c` |

The pending DTBO is not in the default `dtb-y` set.  It is built only with
`CONFIG_GQ5012BF1_PENDING_BOARD_DT=y`, which defaults to `n`.

The inherited SPI node-name warning was removed by renaming the reference
Goodix node from `gffingerprint@spi1` to `gffingerprint@0`; the label
`goodix` is unchanged.  Both overlays now compile without DTC warnings.

No generated DTBO is a deployment artefact.  Hardware validation is 0.
