# SukiSU builtin source provenance

This tree is the SukiSU Ultra builtin implementation, not the SukiSU `main`
kprobe implementation. The change is intentional: the SUSFS kernel patch and
SUSFS Kconfig options are carried by SukiSU's builtin hook model. SukiSU
`main` v4.3.0 only carries the SUSFS extension stub; it has no SUSFS Kconfig,
no SUSFS init call, and no manual-hook integration.

| Item | Value |
| --- | --- |
| Upstream repository | `https://github.com/SukiSU-Ultra/SukiSU-Ultra` |
| Branch | `builtin` |
| Pinned commit | `b16e899a555e9b96aa1225f049cd7274820ca5b8` |
| Version identity | `41100` / `v4.3.0-builtin-b16e899a@vendored` |
| Parent line | SukiSU v4.3.0-era builtin branch |
| Imported source path | `drivers/kernelsu/` |
| Source files | 169 files, about 1.5 MiB |

The source is present as real files. It is not a symlink to an upstream
checkout and does not depend on `/home/armol/kernel-work/`.

The only local build-logic change is the pinned, offline version block in
`drivers/kernelsu/Makefile`. It prevents the build from querying GitHub or
inferring a version from an absent `.git` directory. The builtin tree does not
use the `Kbuild` file that the SukiSU `main` import used.

SUSFS is not part of SukiSU. The separate SUSFS kernel implementation is
recorded in `SUSFS-PATCH-NOTES.md` and is vendored under `fs/susfs.c`,
`include/linux/susfs.h`, and `include/linux/susfs_def.h`.
