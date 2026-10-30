# SUSFS v2.3.0 patch provenance and local adaptation

| Item | Value |
| --- | --- |
| Upstream repository | `https://gitlab.com/simonpunk/susfs4ksu` |
| Branch | `gki-android14-6.1` |
| Pinned commit | `ee155a1ad4219804a7c54cb39dd4fa1b5c2a3fa4` |
| Version string | `v2.3.0` |
| Kernel patch | `kernel_patches/50_add_susfs_in_gki-android14-6.1.patch` |
| Vendored source | `fs/susfs.c`, `include/linux/susfs.h`, `include/linux/susfs_def.h` |

SUSFS v2.2.0 was rejected for this tree. SukiSU builtin `b16e899a` calls the
v2.3.0 priority-UID-filter and AVC-log-spoofing interfaces; the v2.2.0 source
does not contain them. SUSFS v2.3.0 branch head is therefore the compatible
implementation for this SukiSU builtin snapshot.

The SUSFS `KernelSU/10_enable_susfs_for_ksu.patch` was **not** applied. SukiSU
builtin already contains the SUSFS Kconfig menu, `susfs_init()` call, manual
hook points, and SUSFS supercall dispatch that the patch would add. Applying it
would remove the builtin hook model.

The kernel patch was applied to the C56-based kernel at 6.1.115. Two include
hunks did not apply because this kernel has a different include ordering:

- `fs/namespace.c`: added `#include <linux/susfs_def.h>` and the
  `CL_COPY_MNT_NS`/SUSFS extern block before the mount-max definition.
- `fs/proc/base.c`: added the guarded `#include <linux/susfs_def.h>` after
  `linux/cpufreq_times.h`.

The `fs/exec.c` include hunk applied with fuzz 2. The resulting kernel builds
cleanly and is covered by `tools/ulefone/gq5012bf1/verify-susfs.sh`. No SUSFS
kernel object is built as a module; SUSFS is built into `vmlinux`.

SUSFS source is GPL-2.0; the vendored files retain their upstream notices.
