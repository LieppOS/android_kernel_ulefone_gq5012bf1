# GQ5012BF1 / MT6878T reproducible build entry point (C48)

This directory is part of the published kernel repository. It builds the
combined Android GKI 6.1.115 + MediaTek MT6878 + Ulefone GQ5012BF1 source that
produced the C44/C45/C46 kernel and modules.

## C51C vibrator branch exception

C51C changes only the regulator-vibrator module's control/lifetime implementation
and adds its private state policy/tests/ABI documentation. The original C48/C50
configuration and core exports are unchanged; ONESHOT remains disabled. **The new
regulator-vibrator.ko is intentionally NOT byte-identical to C48.** Do not change
reference hashes or suppress the original verifier's expected module mismatch.
No C51C Image/full kernel/module-cohort rebuild is claimed.

ABI: `Documentation/ABI/testing/sysfs-class-led-regulator-vibrator`. Stable
`timeout_abi=1`, `max_duration_ms=5000`, atomic `duration_ms` and `last_error`
replace the HAL's generic trigger sequence. This provisional software ceiling
is not a thermal rating or permission to deploy. See the HAL research handoff.

Reproduce focused tests (no hardware/kernel execution in the host state suite):

```sh
python3 -B tools/ulefone/gq5012bf1/tests/vibrator/run-tests.py
python3 -B tools/ulefone/gq5012bf1/tests/vibrator/verify-module.py \
  /authorized/c50-out/drivers/leds/regulator-vibrator.ko \
  "$OUT/drivers/leds/regulator-vibrator.ko" /authorized/c50-out/vmlinux.symvers
```

Targeted build: copy the unchanged reference config into a **new owned OUT**;
use Android clang17 r487747c, then run `make O="$OUT" ARCH=arm64 LLVM=1 LLVM_IAS=1
DEVICE_MODULES_PATH="$PWD" MTK_PLATFORM=mt6878 -j2 olddefconfig modules_prepare`.
Copy the authorized C50 `Module.symvers` to the new `$OUT/` (never rewrite the
reference output). With that read-only prepared output, run the same make arguments
plus `KBUILD_MIXED_TREE=/authorized/c50-out drivers/leds/regulator-vibrator.ko`.
No private signing key, security bypass, new export or full Image is needed.

Official compiler used: Android prebuilt commit
`425c8149f17c8d9914bf690b4d5abe45a13fb993`, `clang-r487747c`; actual clang-17 SHA256
`25617ca38946d0e7476025e6fa4c15fb386894b75d9372e63a9f3cec47988ce0`.
C51C validated config SHA256 `40de208ba474abfac862ada26301d5a8b26490141e8c944ed303a3ccce8fd2fc`;
new module SHA256 `039526f98cc1dc560f372f2104cb5e4bcafe0885359595dad96990e80e489f45`.
Modpost succeeds; 32 import CRCs match C50 core, 13 common import CRCs and six
existing callback KCFI preamble tags match baseline. No symbols are exported.
These are scoped offline ABI checks, not module load or physical OFF proof.
State tests: 12/12 normal, ASan/UBSan and TSan; four static source-order guards.
Kernel runtime/KUnit/ARM64 device tests were NOT run. Deployment stays CLOSED.

## Original C48 inputs and full-build recipe (historical baseline)

| Input | Where |
| --- | --- |
| Kernel source | this repository |
| Kernel config | `configs/c44-reference.config` |
| Toolchain | Android clang r487747c (17.0.2), LLD 17.0.2, pahole 1.32 |
| Mixed symvers extra | `configs/mixed-vmlinux.symvers.extra` (six authenticated `mtk_ccuv` lines) |
| Reference hashes | `configs/c47-reference-sha256.txt` |
| Normalized module hashes | `configs/c48-module-normalized.txt` |

The toolchain is not vendored. Point `TOOLCHAIN_DIR` at the clang root that
contains `bin/clang`, `bin/ld.lld`, `bin/llvm-ar`, `bin/llvm-nm` and
`bin/llvm-objcopy`, or put those tools plus `pahole` on `PATH`. `openssl` is
required to convert the tracked public DER certificate into the temporary PEM
input expected by `certs/extract-cert`.

## Build

```sh
export TOOLCHAIN_DIR=/path/to/clang-r487747c/bin-parent
export OUT=/path/to/out

./tools/ulefone/gq5012bf1/build-kernel.sh
./tools/ulefone/gq5012bf1/build-modules.sh
```

`build-kernel.sh` copies the reference config to `$OUT/.config`, converts the
tracked public DER certificate to the temporary PEM input expected at
`certs/mtk_signing_key.pem`, and builds `Image`. `build-modules.sh` reconstructs
the mixed `vmlinux.symvers` and builds the in-tree modules.

For a bit-identical C44 reproduction, also set:

```sh
export REFERENCE_DIR=/path/to/gq5012bf1-mt6878-c29-android17/android17-full-3
```

The script then adds the debug-only `-fdebug-prefix-map` and
`-fdebug-compilation-dir` flags used by the C47 reproduction. Without them the
instruction/data content is the same, but DWARF paths and the GNU build-id differ.

## Verify

```sh
REFERENCE_OUT="$REFERENCE_DIR/out" ./tools/ulefone/gq5012bf1/verify-build.sh
```

The script checks the required artifact hashes, CFI/modversions configuration,
the kernel release, the five documented exclusions, the full in-tree module set,
and the normalized hashes of the three modules whose packaged bytes differ from
the C44 reference. It also requires the `Module.symvers` delta to be exactly the
two Hynitron export lines.

## Expected result

- `Image`, `vmlinux`, `System.map`: bit-identical to the C47/C44 reference.
- `699` in-tree modules: byte-identical to the C44 reference.
- Panel and `mtk-mmc`: code-identical to the authenticated C45/C46 packaged
  modules after `llvm-strip --strip-debug` and removal of
  `.note.gnu.build-id`. Only DWARF source-path strings and the build-id differ,
  because the C45/C46 references were built in their own source roots.
- `apusys.ko`: code-identical to the C44 reference after the same normalization.
  The C48 MDLA link-order pin removed the former object-order difference; only
  local symbol/string tables differ before normalization.
- `702` in-tree modules total. `fpsgo.ko` (private wrapper),
  `focaltech_touch_spi_ft3680.ko`, `hynitron.ko`, `spi_tiny_co5300_lcd.ko` and
  `sh366003_fg.ko` (embedded vendor firmware/calibration) are intentionally
  absent.
- `Module.symvers` and `modules-only.symvers` differ from the C47 reference by
  exactly the two Hynitron exports, which disappear with the excluded module.
  The verifier checks that exact delta rather than a fixed hash.

## Reproducibility caveats

The build is source-coherent only as a combined tree. The two module mirror
repositories are provenance mirrors, not standalone build roots. The six
`mtk_ccuv` symbol lines in `configs/mixed-vmlinux.symvers.extra` remain the
in-tree module input, so the C48 in-tree result does not depend on the C49
vendor modules below.

## MediaTek vendor modules (C49)

`vendor/mediatek/kernel_modules/{gpu,connectivity,mtkcam,met}` holds a tracked
source snapshot of the external MediaTek module trees. Per-file SHA-256,
exclusions, source pins and the intentionally unresolved symlinks are recorded
in `vendor/mediatek/kernel_modules/SOURCE-MANIFEST.json`.

```sh
# after build-kernel.sh and build-modules.sh
OUT=/path/to/out TOOLCHAIN_DIR=/path/to/clang-r487747c \
    ./tools/ulefone/gq5012bf1/build-vendor-modules.py
```

`build-vendor-modules.py` builds each group with
`M=vendor/mediatek/kernel_modules/<group>` into `$OUT` in the C29 reference
provider order and `KBUILD_EXTRA_SYMBOLS` chain, and writes
`vendor-build-receipt.json` (hashes, vermagic, depends, undefined symbols) to
`$WORK_DIR` (default: the parent of `$OUT`). Two layout adaptations are made on
the command line or in the output tree, never in the source:

- `MTK_ISP_DRIVER_PATH` is overridden for `imgsensor`, whose Kbuild assumes
  `vendor/` is a sibling of `DEVICE_MODULES_PATH`.
- For `M=` directories with a `Kbuild` file, the source `Kbuild` is linked
  into `$OUT/<M>/`, because Linux 6.1 `scripts/Makefile.modpost` resolves
  `$(src)/Kbuild` relative to the objtree. Without it modpost aborts on a
  missing `Makefile` (`fmradio/Build/connac2x`) or includes the Android-build
  `Makefile` instead of `Kbuild` (`wlan/core/gen4m`).

`wlan/core/gen4m` (`wlan_drv_gen4m_6878.ko`) builds with the LieppOS-authored
`mgmt/tkip_mic.c` (C50, BSD-2-Clause, independent implementation of the TKIP
Michael MIC receive check). The upstream file of that name carries a
no-redistribution notice and stays excluded; `import-vendor-modules.py` records
the replacement under `lieppos_authored` in `SOURCE-MANIFEST.json` and refuses
to import if the upstream file is ever classified differently.

Host tests for the MIC code (IEEE vectors from `crypto/testmgr.h`, differential
comparison with `net/mac80211/michael.c`, frame-level accept/reject,
malformed input under ASan/UBSan):

```sh
./tools/ulefone/gq5012bf1/tests/tkip_mic/run-tests.py --work-dir /path/outside/tree
```

`import-vendor-modules.py --source-root <dir>` regenerates the snapshot from
the source-owning repositories (tracked files only, read-only Git queries) and
refuses to finish if any symlink dangles other than the four recorded optional
components (`gps/gps_mcudl` for GPS v060/v061 and `wlan/core/gen4m_ext`).

`verify-build.sh` ignores `$OUT/vendor/` when counting the 702 in-tree modules.
