# SukiSU Ultra offline validation

Validation date: 2026-10-30 UTC. This is an offline build and source validation,
not a hardware boot test.

## Build result

| Artifact | SHA256 |
| --- | --- |
| `Image` | `f34a4cf2b393aa961b798d1a11da2f887d608193cfd2c550721667b2d87a4f3e` |
| `vmlinux` | `af71aaa7d57cdc19a06bb6ed3c09c6ea600c84702d12f94c4c26bf653d50512b` |
| `System.map` | `753e6f66247a40bc92918260cc5fb7ceb56b44487b496e3ab4ccf555fde13966` |
| `Module.symvers` | `3331100038b4a2e528f29e8c2c49217cc2c4ddecdbd3f1f43bebe497684348e6` |
| `modules-only.symvers` | `664eea239871deb5554e9bf2b787e41f9d5cda87167608df2644b18b87dcab93` |
| `.config` | `eb1b8332665afb694deaec50e326dd13af4916339cdfcadd234ee2bb9db0bc6d` |

- Kernel build: success (`build-kernel.sh` exit 0).
- Module build: success; 702 in-tree `.ko` files.
- Kernel release: `6.1.115-mt6878-gq5012bf1-c29+`.
- SukiSU build identity: version 41100, `v4.3.0-e67b14e6@vendored`.
- SukiSU manager identity: `com.sukisu.ultra`, signature hash
  `947ae944f3de4ed4c21a7e4f7953ecf351bfa2b36239da37a34111ad29993eef`.
- `System.map` contains `kernelsu_init`, `kernelsu_exit`, and the SukiSU `ksu_*`
  implementation symbols.
- No `kernelsu.ko` is produced; SukiSU is built into `vmlinux` as configured.
- No `ksu_*` symbol is exported in `Module.symvers`; no SukiSU module ABI is
  added to the in-tree module interface.
- `verify-sukisu.sh` passes: required config is present, KPM is absent, the
  built-in symbols exist, and the expected 702-module output is present.

## Source consistency

- `drivers/kernelsu/` contains 187 real files (about 1.4 MiB), with no symlinks.
- The vendored tree differs from upstream `v4.3.0` only by the pinned `Kbuild`
  version block, the added provenance file, omitted upstream setup/CI helpers,
  and the materialized UAPI headers. The UAPI headers match upstream root
  `uapi/` exactly.
- `git diff integration/c56-display-repair -- . ':!drivers/kernelsu'` shows only
  the four intentional registration/config files.
- `git diff 5418aa7c1 -- drivers/mmc/host/mtk-mmc.c drivers/gpu/drm/panel/panel-ky-vtdr6115-dphy-cmd.c` is empty. Those
  device-specific files are unchanged from the C46 hardware-booted baseline.

## Comparison with the recorded reference

A C46 source build was made in a temporary detached worktree with the same
local toolchain:

| C46 baseline output | SHA256 |
| --- | --- |
| `System.map` | `721b3267e4cf5120f3482449ed634ef885770da74241a28f9ba1438c8da945e2` |
| `Image` | `784713622faf0471094ab1c5c87988b89d1d5aa5aaec33af683d6c7f390c595f` |
| `vmlinux` | `9ebb9218f7e4c079bed1e664105b143977bb7263d33035a14592c6603f4c0b53` |

The C46 `System.map` exactly matches the recorded reference
`721b3267e4cf5120f3482449ed634ef885770da74241a28f9ba1438c8da945e2`. The
recorded reference `Image` is
`585080a838140a2c73c8caaae27c60ea6f360c926dc1b25ebc0e3a52c1f3a3d0`; the
temporary build did not reproduce that byte hash because the reference build
used its debug-prefix/build-id reference directory. This is reported as a
non-bit-identical artifact, not as a source or symbol mismatch. The SukiSU
`Image` is necessarily different from the C46 image because SukiSU and the C56
display/touch changes are compiled in.

## Protected state at validation

- C56 branch: `b9d284691a53a9aaeaed9e0199b52ef2a5847399`; C56 worktree unchanged
  except its pre-existing untracked `scripts/__pycache__/`.
- C47 worktree: detached at `a6b594803212ea19265bb58f7cf4ba1706f3d009`; exactly
  the two protected modified files remain.
- Research worktree: the 16 pre-existing TODO deletions remain unstaged; no
  unrelated change was committed.
- No phone, ADB, partition, slot, image, push, reset, clean, or Git GC operation
  was performed.
