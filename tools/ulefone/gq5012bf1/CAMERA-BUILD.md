# GQ5012BF1 camera profile

The C44 reference and legacy build wrappers intentionally preserve a donor-board
sensor list for historical reproducibility. They are **not** the camera-enabled
device build entrypoint. Updating the board defconfig alone does not override an
existing output `.config` or the legacy wrapper's explicit C44 input.

Use `build-camera.sh` with an authenticated working kernel configuration. It
changes only `CONFIG_CUSTOM_KERNEL_IMGSENSOR`, refuses contradictory reused
outputs, builds the kernel/in-tree modules, then builds real camera providers
and `imgsensor`. No device, flashing, signing or publication commands run.

```sh
BASE_CONFIG=/absolute/path/to/authenticated-working.config \
OUT=/absolute/path/to/owned-output \
WORK_DIR=/absolute/path/to/owned-receipts \
TOOLCHAIN_DIR=/absolute/path/to/clang-r487747c \
CAMERA_DATA_DIR=/absolute/path/to/private-stock-data \
JOBS=24 \
tools/ulefone/gq5012bf1/build-camera.sh
```

Use `PREPARE_ONLY=1` to prepare/check configuration without compilation. The
working baseline must retain the intended root, display, touchscreen, ABI and
security features. Do not substitute an old generic config because its kernel
release happens to match. Source worktrees are supported with explicit `OUT`,
`WORK_DIR` and `TOOLCHAIN_DIR` rather than wrapper-relative workspace guesses.

## Private dependency and provenance

All nine stock-derived `<sensor>_stock_data.h` headers are required as flat files
in `CAMERA_DATA_DIR`: imx989, imx989cts, s5kgm2sp, s5kgn1sp, ov32b40, s5kjn1,
s5kjn1cts, s5kjn1main2 and ov64b. These are preserved register/mode data from the
authentic GQ5012BF1 stock module, **not fabricated tables**. Their redistribution
is publication-held; they are not embedded in this repository or the helper.
Obtain them only from the authorized project evidence or an appropriately
licensed/authenticated equivalent. The private build is not publicly
self-contained, nor does it make the camera firmware/HAL/tuning open source.

`prepare-camera-data.py` writes derived headers under `OUT/camera-data`, with
input/output SHA-256 receipts. It adds explicit two-element braces to historical
flattened PDAF coordinate initializers (`i4Crop`, `i4PosL`, `i4PosR`), checking that
no non-brace/non-whitespace tokens change. It never edits reference headers or
disables compiler warnings. Differing existing prepared files are rejected.
Stock geometry remains selected; `GQ5012BF1_CAM_CORRECTED` is not enabled.

## Validation boundary

```sh
python3 tools/ulefone/gq5012bf1/tests/test_camera_profile.py -v
```

C62 validated the complete wrapper, all nine final module table entries, 132
import CRCs against the captured loaded module, existing vermagic and exported
frame-sync/KCFI contracts. Hardware deployment changed only `imgsensor.ko` in
`vendor_dlkm_b` and its bound descriptor/signature in `vbmeta_b`; the kernel,
other module contents and AVB flags were retained. Android enumerated all four
sensors and main-camera preview produced frames in both Open Camera and Aperture.
Other-camera previews, still/video capture, AF/quality, torch/IR, concurrency and
thermal imaging are not claimed validated. Research evidence and firmware images
remain private/publication-held. Runtime success does not replace ABI, AVB,
rollback, target-slot and authorization checks for a future deployment.
