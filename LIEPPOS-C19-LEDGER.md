# Campaign 19 task ledger — dev19

Status values: `DONE`, `DONE (offline)`, `BLOCKED`, `NOT STARTED`.
No task in this ledger constitutes hardware validation.

| ID | Work item | Owner | Status | Evidence / next action |
|---|---|---|---|---|
| C19-01 | Create dev19 branches in source-owning repos | kernel | DONE | `src`, `mtkcam`, `gpu`, `connectivity`, `met_drv_v3`, nested imgsensor |
| C19-02 | Restore canonical clang config and generated headers | kernel | DONE | `staging/kernel/config.dk17`; clang 17 config sync log |
| C19-03 | Establish separate ext4 output workspace | kernel | DONE | `/mnt/<reference-ssd>/lieppos-builds/c19-dev19/out` |
| C19-04 | Full `Image` + strict in-tree modules | kernel | DONE (offline) | 708 in-tree `.ko`; strict mixed provider modpost rc 0 |
| C19-05 | Mali r44p1 four-module build | Mali | DONE (offline) | 4 dev19 modules; strict modpost |
| C19-06 | ISP7SP AOV/seninf CRC repair | camera | DONE (offline) | stale `.mod` regenerated; five non-zero CRCs |
| C19-07 | Camera provider closure | camera | DONE (offline) | 9 camera/ISP modules, all dev19 vermagic |
| C19-08 | Connectivity closure | connectivity | DONE (offline) | 9 modules; stock WLAN project inputs |
| C19-09 | MET v3 closure | MET | DONE (offline) | 11 modules, all dev19 vermagic |
| C19-10 | Current-target module manifest | kernel | DONE (offline) | 741 modules, 0 vermagic mismatches |
| C19-11 | Trusted DTBO rebuild | kernel | DONE (offline) | hash `e9896d...` retained; pending DT disabled |
| C19-12 | TEE/GenieZone source fix | kernel | DONE (offline) | `a5084627...`; TrustKernel selected |
| C19-13 | CCU lifetime source fix | camera | DONE (offline) | `dead8ab6...`; compiled |
| C19-14 | WLAN non-WMT source fix | connectivity | DONE (offline) | `1a9a0c3...`; stock build passes |
| C19-15 | Mali build recipe | Mali | DONE | `gpu/build-mali-r44.sh` |
| C19-16 | ISP7SP build recipe | camera | DONE | `mtkcam/camsys/build-isp7sp.sh` |
| C19-17 | Claude build-system handoff | kernel | DONE | `LIEPPOS-C19-HANDOFF.md`, section M |
| C19-18 | Codex FPSGO/C2PS handoff | kernel | DONE | `LIEPPOS-C19-HANDOFF.md`, section M |
| C19-19 | Research handoff | research | DONE (staged) | `KERNEL-DEVELOPMENT-19-STAGED.patch` |
| C19-20 | Hardware validation | device | BLOCKED | Not authorized and not attempted |
| C19-21 | Global `make dtbs` | kernel | BLOCKED | Reaches unrelated mt8192 sources; targeted DTBO used instead |
| C19-22 | Bootable/deployable package | release | NOT STARTED | Intentionally out of scope for this session |

## Reproduction gates

- Use the pinned Android clang 17 toolchain.
- Pass `DEVICE_MODULES_PATH=<src>` and `MTK_PLATFORM=mt6878`.
- Use the canonical config and matching generated headers.
- Build providers before consumers; never pass a module's own symvers or
  symbols already present in the target `Module.symvers`.
- Keep strict modpost; no `KBUILD_MODPOST_WARN`.
- Do not use source-tree dev15 `.ko` files as dev19 inputs.

## Build-output root

`/mnt/<reference-ssd>/lieppos-builds/c19-dev19`

The Btrfs source workspace remained below the 6 GiB metadata restart gate
(approximately 5.86 GiB free); no full build was restarted there.
