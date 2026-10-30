# SukiSU Ultra kernel source provenance

- Upstream: `https://github.com/SukiSU-Ultra/SukiSU-Ultra`
- Branch: `main`
- Pinned release: `v4.3.0`
- Pinned commit: `e67b14e6bbbbd3e67f24df658a01d3e73a64159c`
- Upstream commit date: 2026-10-12
- Imported: 2026-10-30
- Imported path: upstream `kernel/` -> `drivers/kernelsu/`
- Imported UAPI headers: upstream root `uapi/` -> `drivers/kernelsu/include/uapi/` (the upstream `kernel/include/uapi` symlink would point outside this subtree, so it is materialized as real files here)
- License: GNU GPL v3 (upstream `LICENSE`; kernel source headers retain their notices)

The import is a source snapshot.  It is not a symlink and does not require the
upstream checkout or `/home/armol/kernel-work/`.  The upstream `setup.sh`,
`.vscode/` and CI helper scripts are not needed for the kernel build and were
not imported.  The only local modification to upstream build logic is the
pinned, offline version block in `Kbuild` (v4.3.0 / version code 41100), because
the vendored snapshot has no upstream `.git` directory.  The exact local diff
is in `Kbuild.local.diff`.

Integration is kprobe-based (`main` branch), not the manual-hook `builtin`
branch.  Required kernel options are `CONFIG_KPROBES=y` and `CONFIG_EXT4_FS=y`;
this kernel also has `CONFIG_KRETPROBES=y` and
`CONFIG_HAVE_SYSCALL_TRACEPOINTS=y`.  `CONFIG_KSU=y` builds SukiSU into the
kernel image.  KPM is disabled by default for stability.
