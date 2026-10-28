# GQ5012BF1 / MT6878T reproducible build entry point (C48)

This directory is part of the published kernel repository. It builds the
combined Android GKI 6.1.115 + MediaTek MT6878 + Ulefone GQ5012BF1 source that
produced the C44/C45/C46 kernel and modules.

## Inputs

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
repositories are provenance mirrors, not standalone build roots. External
`gpu/`, `connectivity/`, `mtkcam/` and `met_drv_v3/` trees are not imported; the
six `mtk_ccuv` symbol lines stand in for the reference mixed-tree inputs and do
not contain `mtkcam` source.
