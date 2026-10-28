# C47 Kernel Source Import

## Identity

| Field | Value |
| --- | --- |
| Source worktree | `/mnt/<reference-ssd>/lieppos-c29/engine/builds/gq5012bf1-mt6878-c29-android17/android17-full-3/src/common` |
| Source commit | `ce07b664a30032456984ac0df9682c91deddc90e` |
| Source tree | `1a47f5432f7f9ad0bc3598b755ab1481db15b489` |
| Source root snapshot | `bcf53f02aa57a906efa31381ff20dbb8980c2425` |
| C44 kernel release | `6.1.115-mt6878-gq5012bf1-c29+` |
| C44 Image SHA256 | `585080a838140a2c73c8caaae27c60ea6f360c926dc1b25ebc0e3a52c1f3a3d0` |
| Imported at | 2026-10-28 |
| Import branch | `campaign/c47-source-import` |
| Canonical build tree | this repository |

The import is a **source snapshot**, not a copy of the source repository's Git
history. The external source is a shallow worktree of a combined GKI 6.1 +
MediaTek MT6878 + Ulefone GQ5012BF1 tree. Its root commit is a source snapshot,
so preserving its object IDs would not create a complete upstream history.
The original worktree remains authoritative and unmodified.

## What is included

- The complete coherent kernel source tree at `ce07b664`, which is the C44
  build source.
- `configs/c44-reference.config`, the exact `.config` from the authenticated
  C44/C29 build (`40de208ba474abfac862ada26301d5a8b26490141e8c944ed303a3ccce8fd2fc`).
- `certs/mtk_signing_key.x509`, the **public** certificate extracted by the
  reference build. It contains no private key.
- Existing LieppOS campaign notes and provenance files from the source tree.

## What is excluded

| Path | Classification | Reason |
| --- | --- | --- |
| `certs/mtk_signing_key.pem` | PKCS#8 RSA private key (4096-bit) | MediaTek module-signing private key; never import |
| `tools/testing/selftests/sgx/sign_key.pem` | RSA private key (test fixture) | Private key material; never import |

Both files are present in every commit of the external source history. They are
excluded from this snapshot and are not present in this repository's history.
The public certificate is the only key-related file imported.

`CONFIG_MODULE_SIG_KEY` in the stored reference config still names the excluded
private key. For local reproduction, the public certificate is written as a PEM
at `certs/mtk_signing_key.pem` in the disposable build worktree only; the stored
config is unchanged. This does not embed private key material. The imported tree
cannot produce the original module signatures without the external private key;
that limitation is recorded in the C47 build report.

## Relation to the module repositories

The kernel source is the canonical coherent build tree. MediaTek and Ulefone
module repositories contain verified source mirrors of the files relevant to
their ownership, with source commit and SHA256 manifests. They are not
independent build roots; splitting the shared Kbuild/header graph would break
the proven build. See `docs/integration/C47-MODULE-SOURCE-IMPORT.md`.

## Reproduction

The build commands, toolchain, reference hashes and results are in
`docs/integration/C47-BUILD-REPRODUCIBILITY.md` and
`docs/integration/C47-RESULTS.md` in the workspace.
