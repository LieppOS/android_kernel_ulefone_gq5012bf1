# LieppOS FPSGO/performance source — Campaign 14

**Experimental source group; HOLD; offline only. Not deployable.** This is a source-owning continuation from authenticated accessible snapshots, not an assertion that the inaccessible protected Codex branch has been inspected. See `PROVENANCE.md`.

## Implemented group

* `mtk_perf_ioctl`: actual frame/vsync/buffer/SBE/XGFF ioctl dispatch and callback slots.
* `mtk_perf_ioctl_magt`: actual dependency-list/target-FPS and CPU performance observability; policy-mask traversal, bounded wire arrays and proc lifecycle corrections.
* `mtk_ioctl_powerhal`: rewritten bounded native/AAPCS32 ADPF ioctl dispatch, copy/allocation checks, provider error propagation, and proc rollback/rundown.
* `powerhal_cpu_ctrl`: actual ADPF service plus new `powerhal_policy.c` request ownership, policy CREATE/REMOVE notification handling, held policy refs, QoS failure rollback, request restoration for recreated domains, and input parsing without partial desired-state corruption.
* `mtk_fpsgo`: authenticated MediaTek base/composer/FBT/FSTB/XGF/GBE implementation. Generic GKI uses the donor frequency-driven FBT path with live cpufreq tables and normalized CPU capacity. FBT QoS cleanup, loading-history allocation failures, timer/work cancellation, sparse CPU histories, task/clamp/affinity bounds and optional interconnect ownership were repaired. CPU-policy snapshots now publish transactionally, filter invalid OPP entries, support sparse/interleaved masks, deep-copy with complete allocation unwind, and provide an immutable tracepoint-safe domain map. Generic FBT frequency/capacity tables are populated from that snapshot rather than left zero by an absent vendor-only config block. Early policy/sysfs/kthread/trace-hook failures return errors and release pre-publication resources.
* `fpsgo`: the authenticated Phase 13 reconstructed private estimator/FSTB/RL algorithms (all 13 staged source inputs preserved as the initial commit). No proprietary stock object or stock-derived CRC table is linked.

There are six modules, not artificial one-symbol provider modules. One Kbuild/modpost invocation resolves genuine provider exports throughout the group. Original per-file licenses are retained; the private wrapper remains Proprietary, so redistribution review is still necessary.

## Targets and limits

`targets/` records actual stock-compatible and experimental custom-GKI input identities. Both builds use their own prepared headers/config/symvers. No kernel release or CRC is edited. Generic-GKI source builds do **not** claim MediaTek nonlinear scheduler/VIP/LS/group support, GED GPU integration, DRM refresh callbacks, CCI or device bandwidth services. Those require the real matching platform providers/configuration. Unsupported CCI operations return `-EOPNOTSUPP`; no fake export is substituted.

**The historical “11 missing symbols” was a truncated-log error:** the pilot printed ten modpost names and suppressed 41 more. The original unresolved graph is 51 symbols, excluding ELF's generated `__this_module` pseudo-reference. The generic-GKI group closes its enabled import graph; the native vendor-enabled graph remains a separate unfinished target.

Framework still deliberately has no `module_exit`: full render/linger teardown, task identities and remaining native callback families need complete rundown. The current separate PowerHAL-lifetime WIP adds actual provider/boost leases, queue/session/work/QoS teardown and a service exit handler; its kernel build is blocked by the existing metadata floor and it is NOT kernel-validated. The host ioctl TU lease-model test passes but is not real SRCU/module-unload proof. The private module has a source-owned begin/commit/revoke protocol, serialized SRCU callback leases, synchronized ring-writer revocation, private record destruction and an exit handler. Managed FPSGO/XGFF/LR ioctl and MAGT setters now take dispatcher SRCU leases; revoke wakes blocking readers before draining leases and clearing slots. Command/FPS/camera/active waits observe closure, and component exits free their queued command allocations. The ADPF registration handle persists for eventual framework exit. Both real target incremental builds passed strict modpost after the original infrastructure gate returned PASS externally (stock19/custom13). No cleanup or floor change was made here. This does not establish full six-module unload support or runtime acceptance; see `LIFECYCLE-WIP.md`. FBT policy removal detaches requests safely but full FBT OPP-table reconstruction after driver replacement still requires reprobe. Frame/task PID reuse, private callback registration failure and complete module unload remain review items. Framework initialization now checks every child constructor, records completed-stage ownership and unwinds in reverse order before publishing frame callbacks. FBT top-level/row/loading-history allocations and child initialization fail transactionally; workqueues, timers and UX caches are released for completed stages. Sysfs uses guarded wrappers behind a commit/quiesce barrier, including /sys/kernel/gbe, and construction errors propagate to the parent. Composer recycling and XGFF/GBE publication are deferred until commit. The main worker has a stop-aware wait and owns/disposes all queued and nested MAGT payloads, including disabled/aborted events. These pre-publication rollback changes do NOT establish complete post-publication callback teardown or unload safety. No runtime safety or performance claim follows from a successful module build.

## Independent offline builds

Use Claude's authenticated kernel/prepared/toolchain inputs, or independent workspaces provisioned by that infrastructure. This source-specific helper copies only generated module-build inputs using mandatory reflinks and archives the clean committed source; it does not replace the workspace infrastructure.

```sh
python3 tools/build-group.py \
  --kernel /authenticated/common \
  --prepared /authenticated/target-kout \
  --toolchain /authenticated/clang-r487747c/bin \
  --gate /existing/infrastructure/storage-gate.sh \
  --output /new/independent-performance-output --jobs 8
```

Run twice with different fresh output paths for each target. The receipt binds source/kernel commits, config, genuine kernel symvers, compiler/linker, copied input preservation, arguments and final module hashes. `KBUILD_MODPOST_WARN` and external symvers injection are disabled. Prefix mapping is a compiler option, not ELF post-processing. Do not install, sign for deployment, load, or package these modules into an image.

## Focused software tests

```sh
CC=/authenticated/clang/bin/clang sh tools/test-offline.sh /new/test-output
```

ASan/UBSan execute the production ioctl translation unit, deep-session snapshot helper, and new policy provider against test-only kernel-service models. They check bounds, compat layouts, provider errors, policy refs, sparse domains, add/update failure unwind, remove/recreate, request ownership and rollback. A third production-C test checks the CPU-policy snapshot: every root/row/deep-copy allocation failure, absent/malformed policies, invalid frequency entries, unsigned sort ordering, interleaved domains, immutable atomic-context lookups, and exact reference/ownership balance. They do not test real scheduler timing, kernel hotplug races, thermal behavior or hardware.

## Integration

The vendor-tree patch is built in an isolated copy, never another agent's active branch. Keep `VK_PROFILE=performance` explicit; do not silently add this HOLD group to the existing accepted/default profile. Future device-integrated MT6878 support requires an authenticated committed target and native-provider contract before enabling vendor-only features. Phase8 now supplies an authenticated **uncommitted** Target C engineering snapshot; that is not a committed owner delivery or acceptance. See `TARGET-C.md` for the complete-cohort ownership decision, platform-header boundary and still-unvalidated import-root correction.
