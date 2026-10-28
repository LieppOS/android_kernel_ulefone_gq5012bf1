# Publication notice — GQ5012BF1 kernel source snapshot

This repository is a **sanitized publication snapshot** of the LieppOS C47
source import for the Ulefone Armor 29 Pro Thermal (GQ5012BF1 / MediaTek
MT6878T). It is not a preservation of the external eight-commit vendor or
project history, and it is not a hardware validation claim.

## Lineage

| Stage | Verified source |
| --- | --- |
| Android GKI base | ACK 6.1.115 `6b18f0b574ab3267615ae6ce642d5a7c3c21ac09` (GKI 12901745, `android14-6.1-2024-12`) |
| MediaTek donor | `kernel_device_modules-6.1` `ca8b550b75e8b49f403f0097e8323c4a930bad79` |
| C44 combined source | `ce07b664a30032456984ac0df9682c91deddc90e` |
| C45 panel ABI fix | `d02a89424974655d8b8a981421aced87d0761fae` |
| C46 MMC probe fix | `2c896fab1c641ee7577ba40ccc2137b660e94c7c` |
| C47 local snapshot | `7a75e19ebf5d7e4b3876c19f4e77cad385dff4f0` (five local commits) |

The C47 local branch remains `campaign/c47-source-import` in the development
workspace. This publication branch is a new root snapshot because the C47
history contained a non-redistributable private wrapper (below); publishing that
branch would have made the restricted files reachable.

## Excluded from publication

`drivers/misc/mediatek/performance/lieppos14/source/` is excluded in full. It
was an experimental source reconstruction of MediaTek's FPSGO private scope.
Its wrapper declared a Proprietary module-license string and carried an
explicit MediaTek notice prohibiting reproduction, modification, use or
disclosure without written permission. The C47 provenance record already flagged it as
requiring a distribution/license review. It is not relicensed by this
publication. The `lieppos14/Kbuild` reference to the directory was removed; the
public GPL-2.0 `lieppos14/public/` cohort (MediaTek `mtk_fpsgo` and related
providers) is retained and remains buildable.

The following Ulefone board sources are also excluded because they embed
byte-exact vendor firmware, logo or calibration/programming images with no
redistribution grant. The C47 "binary files = 0" check counted file extensions,
not embedded byte arrays; C48 found these by content:

| Path | Embedded material |
| --- | --- |
| `drivers/input/touchscreen/focaltech_touch/` | 118,972-byte stock FT3680 firmware extracted from a vendor `.ko`; `focaltech_flash.h` also lacks a GPL grant |
| `drivers/input/touchscreen/hynitron/` | `firmware_cst816t.inc` (95,100 bytes) and `firmware_cst8xx.inc` (95,152 bytes) |
| `drivers/misc/mediatek/tiny_lcd/` | `gImage_logo.inc` (1,425,768-byte vendor logo) and `init_setting.inc` |
| `drivers/power/supply/sh366003/` | `sinofs_afi_data.inc` (stock battery gauge AFI/profile image) |

Their Kconfig symbols are retained so the reference `.config` hash is
unchanged, but their Makefile hooks are removed and the modules are not built.
The public tree therefore builds **702** in-tree modules instead of the C47
local set of 707. `fpsgo.ko` and the four board modules above are absent. The
kernel `Image`, `vmlinux`, `System.map`, panel module and `mtk-mmc` module are
unaffected; `Module.symvers` and `modules-only.symvers` lose exactly the two
Hynitron exports (`tiny_tp_gesture_contorl`, `tiny_tp_power_contorl`).

No private-key PEM was ever imported. The two C47 findings
(`certs/mtk_signing_key.pem` and `tools/testing/selftests/sgx/sign_key.pem`)
remain excluded. The public X.509 certificate `certs/mtk_signing_key.x509` is
retained. It is a DER certificate; the build script converts it to a temporary
PEM file at the config-named `.pem` path so `certs/extract-cert` can reproduce
the reference `signing_key.x509` without a private key.

## Licensing

The tree is predominantly GPL-2.0, with per-file SPDX identifiers and the
upstream `COPYING`/`LICENSES/` notices preserved. Components under MIT, BSD,
ISC and other GPL-compatible terms keep their own notices. Files without an
SPDX header are not relicensed by LieppOS; their original notices and the
kernel's default GPL-2.0 terms continue to apply. The LieppOS experimental
`lieppos14` glue carries no separate license grant and is published only as part
of the combined kernel work; the owner should add explicit SPDX identifiers
before representing those files as independently licensed.

## Build and validation status

- Source build entry point: `tools/ulefone/gq5012bf1/`.
- C47 reproduced `Image`, `vmlinux`, `System.map`, `Module.symvers` and
  `modules-only.symvers` bit-identically.
- The APUSYS MDLA link order is pinned to the authenticated reference order;
  see the C48 reproducibility report.
- No C47 or C48 hardware boot, flash or phone operation was performed. The C46
  `sys.boot_completed=1` result belongs to that exact C46 image set and is not a
  verification of this snapshot.
