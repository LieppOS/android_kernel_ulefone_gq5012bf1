# Target C integration contract — engineering only / HOLD

Phase8 tested C14 commit1e727b90, not later138adb4d/private-rundown or9efd4a77/PowerHAL changes. Its authenticated read-only import is lk-codex-performance@16bf28be26ab. The inherited Makefile defect still existed in those later sources: DEVICE_MODULES_PATH selected both platform headers AND another FPSGO generation's protocol headers/build fragments.

## Chosen ownership for the C14 experiment

Use the complete maintained C14 FPSGO v7/private/ioctl/PowerHAL cohort as ONE experimental performance owner. Do not mix its sources with C13 FPSGO protocol headers, or load/install both performance owners together. The default C13 build is unchanged. This is a replacement-candidate contract, not proof that C14 currently works on the device kernel. Source/native API, callback type/layout, dependency and runtime gates remain unfinished.

Owned FPSGO roots/build fragments now derive from their own Kbuild `$(src)` and cannot be redirected by legacy FPSGO_TOP/BASE_TOP/etc command-line values. The sibling owned PowerHAL headers also use the local cohort. DEVICE_MODULES_PATH remains a separate platform-header root for GED, DRM, scheduler, thermal and other native providers. The parent Kbuild scheduler include also follows that platform root rather than forcing the trimmed generic scheduler header directory ahead of the selected native one.

For generic GKI, the default remains the trimmed `public/` platform header set. Under Target C's real config (NONLINEAR_FREQ_CTL, MTK_OPP_CAP_INFO and GPU_COMMON_DVFS enabled), that trim is NOT a complete native input. In the frozen engine copy, ged/include/ged_kpi.h and sched/common.h exist; both are absent from the trim. An infrastructure-owner native probe must supply the matching authenticated platform root explicitly, e.g. the Phase8 frozen Target C `src/common`, while the owned protocol headers stay local. Do not copy foreign FPSGO headers into the delivery, guess prototypes, undefine native config options, or inject versions to make compilation pass. Native provider/header compatibility is not established by path isolation.

## Target provenance and build gates

Target C input is targetc-dk12-src@0f5b4c3b7ee3, labeled uncommitted-owner-worktree by Phase8's race-checked snapshot. It is NOT a committed Campaign13 kernel source delivery. The protected C13 research branch was not accessed. A deterministic store snapshot commit is not an owner source commit. Board DT remains an owner draft, and deployment/package blockers remain unchanged.

The root-isolation changes have only metadata-only GNU make parser checks, including the actual parent Kbuild plus child Makefile: generic platform, frozen Target C platform, and conflicting legacy root variables all retain local owned headers while preserving external GED/scheduler paths. The original child-only parser check did not cover inherited parent scheduler include precedence; the later parent+child check does. No kernel make/compile/modpost was run: Phase8 heavy-build freeze and the campaign resource gates are respected. It is NOT a fix certification for Target C.

After normal capacity recovery/unfreeze, infrastructure owner should import a coherent committed C14 candidate, test private GKI targets and the actual Target C config without disabling native features, and resolve real follow-on source/provider/callback conflicts. Existing C13 performance exports/modules must have explicit replacement ownership in the combined build/package; do not filter conflicting symvers or stage duplicate owners. Rebuild the real graph with strict modpost and inspect load order/type/layout before any acceptance change.

TODO51 full core render/linger/timer/work/task lifetimes and TODO52 native provider functionality remain incomplete. No phone/deployment/signing/push or accepted gate changes.
