# Target C integration contract — engineering only / HOLD

## Owned ADPF pipeline — current implementation

The producer now captures actual per-thread PID objects at CREATE/SET_THREADS
in caller context. Sessions and every prepared event own independent references;
partial capture, abort, replacement, CLOSE, dispatch and teardown release exactly
their own references. Thread group/death checks precede commit. Callback IDs are
normalized to kernel PID numbers; UAPI layouts/commands and creator-process
permissions across descriptors are unchanged. Process liveness checks executing
threads rather than merely an attached zombie leader. Disable-all still reserves
ALL close events before mutation, and dead CLOSE allocation is retryable.

FPSGO borrows the event's PID objects synchronously through composer into XGF.
The candidate tree obtains tasks from those retained identities, not a numeric
precheck followed by another lookup. It rejects stale/reused objects and commits
by ownership move; failed replacement preserves the old tree. CLOSE is valid
while dead/disabled. Non-CLOSE callbacks require framework READY.

Seventh overlay `native-adpf-session-lease.patch` wires the real EAS consumer to
the task-group/PD, VIP/hint and ratio providers. Bounded session records retain
creator/app/thread identities; shared tasks have one native token pair and a
reference count across sessions. CREATE/SET_THREADS prepare independent candidate
ownership and unwind failures without dropping old memberships. PAUSE releases
only its memberships/ratio; RESUME re-admits the retained objects. Dead owners
close; task death or ordinary-writer preemption withdraws owned requests and leaves
quiescent metadata for a later explicit SET_THREADS/RESUME/new work report. A new
report may re-acquire against CURRENT baselines; the reaper never reapplies an old
boost over an ordinary writer. Recycled numeric TIDs cannot redirect admission.
REPORT uses bounded 64-bit multiply/divide without signed/32-bit truncation.
Unknown vendor hint policies return EOPNOTSUPP rather than invented boost policy.
Asynchronous callback errors are logged, not a claim of synchronous HAL backend ACK.

Target C suppresses native EAS callback registration during native scheduler
COMING. The maintained adapter imports the actual native APIs (strong LIVE module
dependency), owns the callback SRCU slot and registers it before its final RVH.
Rollback unregisters/joins EAS then drains all sessions WITHOUT changing operator
enable policy. First admitted EAS session pins native callback/reaper storage
permanently; normal unload is unsupported. All storage survives committed callbacks.
The adapter's own restricted hook remains the final fallible registration, with
its original self-pin/error normalization. No nonexistent RVH unregister is used.

The actual Target C requires group-aware scheduling, native CPUQoS and real lease
support; do not substitute no-op stubs or the legacy EAS body. All SEVEN reviewed
overlays are mandatory. This is still engineering/HOLD: host models and native
compilation do not establish runtime/IRQ/lockdep/KCSAN/hotplug/hardware safety or
certify the unmodified platform scheduler's broader lifecycle paths.

Focused checks: `test-adpf-sessions.py`, `test-adpf-event-ownership.py`,
`test-adpf-session-transactions.py`, `test-fbt-task-helpers.py`,
`test-fpsgo-teardown-order.py`, and `test-eas-adpf-owned.py` in `tools/`.
The EAS witness executes the actual consumer AND ratio provider, with modeled
kernel/task/VIP/group primitives, both VIP configurations, overlapping sessions,
concurrent reports, allocation rollback, pause/resume, PID reuse, preemption,
zombie/leader-exit cleanup, disable and 64-bit arithmetic.

## Historical provider checkpoints — superseded by implementation above

The authorized distinct workspace is
`/mnt/<reference-ssd>/lieppos-builds/codex-fpsgo-dev20-continuation`.
At source b31b8d0f, the cohort and native scheduler/sugov/core providers were
compiled from pinned dev20 source: 11 modules linked, explicit strict modpost
passed, 1,099 versioned imports matched, dev20+ vermagic verified. The next
CPUQoS provider milestone adds that actual native module to the focused build;
its new receipt must be checked separately from this historical result. The rebuilt core
is **relocatable vmlinux.o only**, not a boot artifact. Unchanged non-selected
provider exports come from authenticated dev20 Module.symvers; all 376 old
selected-provider export rows are excluded before focused modpost. No CRC is
invented or overridden. BUILD.json records the exact consumed revisions.

Reproduce/resume (committed clean source required; rechecks resource gates):

```sh
python3 integration/target-c/build-dev20.py --resume
```

In addition to the three historical overlays, apply
`native-adpf-contract.patch`: native EAS and PowerHAL must share the same
kernel callback structure, including **separate creator and app identities**.
The new PowerHAL event owns both references; callbacks borrow until return.
FPSGO consumes the ordered callback synchronously, allowing dead/disabled
CLOSE without a second lossy allocation/queue. Native EAS **shared group/task
leases remain unfinished**: its existing CLOSE can still clear another
session's GROUP_ID_2 ratio/membership. Do not deploy this engineering cohort
or infer lifecycle/acceptance from successful linking. Runtime/lockdep/KCSAN
and hardware testing remain NOT RUN. Historical no-build statements below
refer to earlier checkpoints, not this continuation.

## Native task-group + CPUQoS ownership milestone

The fifth overlay now also places a genuine token provider beside the actual
native group membership helpers. `mtk_group_task_acquire/is_live/release()`
retains the task and acquires its CPUQoS token; group installation/restoration
uses the native pi -> group-writer -> group/rq lock order. A failed group step
releases its PD token. PD preemption racing installation is detected and the
group half is unwound without restoring over the ordinary PD writer. These
are independently revocable resources, NOT one atomic cross-module transaction;
clients must release the whole token on lost liveness and join users first.

Ordinary group writers revoke under the SAME membership lock, even for a
same-value custom-group request, and update the customized flag under that lock.
An old release cannot remove a newer group/PD owner. Manual baseline membership
is restored exactly; automatic baseline membership follows the current cgroup,
not the cgroup at acquisition. New-task initialization clears temporary custom
membership flags so fork cannot inherit an unowned override. None of these
APIs makes existing legacy group+PD setters one rollback-safe transaction.


Apply `native-cpuqos-task-lease.patch` after the four existing overlays.
The actual native CPUQoS module includes the maintained provider implementation
beside its real map/IPI helpers. `cpuqos_task_pd_set()` borrows a caller-retained
task; the legacy `set_task_pd()` wrapper resolves once and preserves its -1/0
error convention. Group API calls keep the same task reference through both
group and CPUQoS updates rather than releasing it before numeric relookup.
This does NOT yet make group+PD updates one rollback-safe transaction.

Native task-PD tokens retain a task and save its rank/PD. One token per task is
accepted; failure owns nothing. All ordinary PD writes revoke a token under
the same raw lock, including identical values. A revoked release cannot undo
an ordinary writer or a newer lease. Release of a live owner restores both
fields; dead/reused tasks cannot redirect it. Allocations, final puts and
synchronous IPIs happen outside the registry lock. IPI notifications read the
latest task state and NEVER write an older snapshot PD back after unlocking.
The trace captures CSS id while inside RCU rather than retaining a CSS pointer.

The CPUQoS module is now a source-built selected provider; its old baseline
exports must be excluded from focused modpost. Source EAS group/ratio/session
leases remain unfinished, and no native runtime/race safety is claimed.
Host check: `python3 tools/test-cpuqos-task-lease.py <fresh-output>`; it compiles
the actual provider with lease support on/off and models kernel tasks/locks/IPIs.
Add `--group` to compile both actual providers together with modeled native rq
membership primitives, including failed second allocation, ABA, same-value
ordinary writes, cgroup changes and PD preemption during group installation.

## Native active-ratio aggregation

Sixth overlay `native-adpf-ratio-lease.patch` puts genuine ratio ownership beside
the actual per-CPU group ratio arrays. Each token contributes zero (withdrawn)
or a bounded cap; the maximum active request wins. With no active request, the
original heterogeneous per-CPU baseline is restored. Releasing one owner cannot
clear another owner's request. All four native ratio setters and the automatic
top-app writer preempt group overlays and write under the SAME IRQ-safe lock.
An old release cannot undo a newer writer or token; invalid CPU/group indices
are rejected before registry access. Allocation and freeing are outside the lock.

Native EAS still needs to consume these task/group/VIP/ratio tokens. In particular,
PAUSE must withdraw that session's task memberships as well as its ratio, and
shared task memberships need reference counting, prepare/commit and rollback.
PowerHAL currently carries retained creator/app identities but only NUMERIC thread
IDs in its ordered snapshot: per-thread caller-time PID ownership must be added
before claiming within-app queued TID-reuse closure. None of these provider
milestones alone closes that lifetime gap or the native EAS session contract.

Host check (lease support on/off, actual setters/provider; kernel primitives modeled):
`python3 tools/test-native-ratio-lease.py <patched-grp_awr.c> <fresh-output>`.

Pinned device kernel: `/home/<user>/kernel-work/noOEM09/dk12/src` commit
**f3f4cb91171896b13d40ad40290d641a0c350832** (observed committed C20 HEAD),
including Campaign 17 implementation
**c933d3ae32807025aaa88fdc8c2eafb22f66a135**. Work began at committed
**771a4d4527fe1cb70bdbcdb1d0db5a0eb3653ad6**; the later commit changes none
of the relevant performance/provider/hook APIs or overlay base blobs.
Only committed Git blobs were read; no owner working file was read or modified,
and the protected research repository was not accessed. During this work HEAD
advanced from f9152925 through9fc51 to f3f4cb91. All overlay base blobs plus the
scheduler Kconfig/Makefile, rq/task headers, tracepoint/module/hook and cpufreq
files checked against9fc51 (14files) are unchanged. The committed
`LIEPPOS-C20-HANDOFF.md` reports a build of code
8d22fceb803a30bbf289e1c021bdfa0ebe103dc1, release
`6.1.115-mt6878-gq5012bf1-dev20+`, Android clang-r487747c17.0.2, and741modules.
This is owner-reported baseline evidence, NOT our build or validation of the
three Codex patches/new source. Owner mutable outputs were not read or modified;
the separate owner ext4 build does not authorize bypassing our storage gate.

## Staging

1. Copy this source cohort to `drivers/misc/mediatek/performance/lieppos14/`
   in a private integration copy of the pinned kernel.
2. Apply `native-owner-overlay.patch` (two files, see `overlay.json`), then
   `native-control-lease.patch` (three native TUs + four small provider files;
   see `native-control-lease.json`), then `scheduler-uclamp-lease.patch`
   (core.c plus its private implementation and public opaque API header;
   see `scheduler-uclamp-lease.json`). All replay against pinned committed
   blobs. These are reviewable kernel-owner patches, not edits to the owner's
   active worktree. BOTH lease patches are REQUIRED for C2PS; no fallback.
   Then apply `native-adpf-contract.patch` and `native-cpuqos-task-lease.patch`;
   then `native-adpf-ratio-lease.patch`. All six overlays are required for the
   current focused candidate.
3. Configure with the real device config. `CONFIG_NONLINEAR_FREQ_CTL=y`,
   `CONFIG_MTK_OPP_CAP_INFO` and `CONFIG_MTK_SCHEDULER` must stay enabled;
   `LIEPPOS_PERFORMANCE` depends on them rather than working around them.
   Merge `performance.config`: BOTH `LIEPPOS_PERFORMANCE=m` and
   `LIEPPOS_C2PS=m`, plus `MTK_PERF_CTRL_LEASE=y`, are required for campaign
   verification. Deployment defaults
   remain off/HOLD; that does not make C2PS an optional deliverable.

The overlay gates exactly five duplicate native output owners
(`mtk_perf_ioctl`, `mtk_ioctl_powerhal`, `mtk_perf_ioctl_magt`, `fpsgo_v3/`,
`powerhal_cpu_ctrl/`) behind `CONFIG_LIEPPOS_PERFORMANCE`. Touch boost, FRS,
game, load tracker and uload remain owned by the device kernel and are
untouched. The two performance generations are never built or installed
together. The disabled selector is tested both unset and explicitly `n`;
only `y`/`m` exclude native owners (the earlier empty-only test was incorrect).

## Restricted-hook contract and lifetime

The current kernel calls `android_rvh_cpufreq_transition` from both the slow
POSTCHANGE path and atomic `cpufreq_driver_fast_switch` under RCU-sched; the
callback must not sleep. Its FPSGO path uses the immutable CPU-domain map and
FBT's irq-safe frequency/loading spinlocks, not policy reference discovery or
render/task work. Persistent callback text/data are not `__init` storage.
`rvh_func_add` can return POSITIVE `ENOMEM`; FPSGO normalizes it before rollback,
because `do_init_module` treats positive init returns as success. Negative/full
registration errors do not publish a callback.

`do_init_module` allocates its fallible init-free bookkeeping BEFORE invoking
our init. After init returns zero it has no failure path freeing module core
text. Combined with all fallible FPSGO work before the final RVH registration,
the self-reference/no-exit policy supports permanent module lifetime under
normal module APIs. It is not proof of all runtime behavior, hotplug or other
providers' lifetimes, and forced removal is unsupported. Callback state/maps
remain allocated for the boot; early rollback never runs after successful RVH
registration. No nonexistent unregister API is used.

## Header ownership

Owned FPSGO/PowerHAL/C2PS protocol headers are included by source-relative
path, so no include-search precedence can substitute another generation's
interfaces. Native providers (GED, scheduler/sugov, EAS group/flt, DRM,
thermal) are included through their full `drivers/...` namespace and resolve
from `DEVICE_MODULES_PATH`, which the Target C profile forces to `$(srctree)`
so that a prepared foreign tree cannot be selected. C2PS no longer carries
copied `extern` declarations of native scheduler APIs; it includes the real
target headers, which also corrects the `get_adaptive_margin` parameter type.

## C2PS request and task lifetime

The sleepable notify API now waits for the actual notifier transaction result;
allocation, duplicate-id, stale-frame and capacity errors reach the ioctl caller.
No ioctl command numbers or UAPI layouts changed. Work/completion stack storage
is not abandoned on signals. A retained TGID PID object owns the session; other
processes receive EPERM and failed/foreign requests cannot refresh its 5-second
lease. Closing the activating descriptor closes the session; descriptor transfer
does not transfer process ownership, and an orphan expires after the same lease.

Task references are captured in caller context, not re-looked-up by numeric PID
in the worker. Static task ids retain a PID identity; dynamic ids may change it
only between frames. Frame epochs reject delayed regulator jobs. End, dead-task
reaping and missing-END expiry release task resources; session close drains all
requests before clearing bounded task/group tables. Clamp reset restores the
previous explicit/default request rather than invented defaults. Kernel threads
cannot be camera boost targets.

Blocking PM notification stops admission, synchronously cancels timers/drains
requests and closes the session before suspend. It resumes admission, not stale
sessions, on POST. PM registration is the final fallible C2PS module step; no
post-publication unregister/unload is supported.

## Native sugov control lease

The new provider lives alongside the actual native control variables. A token
acquires one temporary overlay over gu_ctrl, cu_ctrl, workload selection and
gear limits. All four native scalar setters serialize with it under an IRQ-safe
raw spinlock. A legacy writer first restores/withdraws the entire temporary
overlay and then performs its own write under the SAME lock, including when it
writes the same numeric value. The client receives ESTALE on later updates;
its release cannot undo that writer or a newer token. Allocation is outside the
lock, and consumers must join their users before freeing the retained handle.
Disabled lease support has no provider API stubs; mandatory verification enables
its actual native implementation.

C2PS acquires before publishing its session, uses token updates for regulator
and recovery requests, joins regulator users before releasing, and closes on
preemption/error at its next worker wake. No unconditional disable/reset of
another client's controls remains. The regulator and notifier share the same
global-info mutex. Requested initial limits populate the governor's max/current
cache after successful installation, preserving the donor's intended ordering.
The obsolete all-zero EAS override helpers (already rejected by the ioctl
contract) are removed, not silently enabled with fictitious ownership.

## Native per-task VIP/hint lease

The real pinned capability is `CONFIG_MTK_SCHED_VIP_TASK` (scheduler Kconfig
and Makefile `scheduler-$(CONFIG_MTK_SCHED_VIP_TASK) += eas/vip.o`), NOT
`CONFIG_MTK_SCHED_VIP`. Provider declarations/definitions and C2PS admission use
the real flag. The targeted host check covers enabled and `--vip-disabled`
configurations, the latter with no VIP storage/API supplied, requiring
EOPNOTSUPP for VIP frames and working non-VIP hint/clamp frames. Earlier host
checks with an invented flag did NOT validate this native configuration path.

A second registry uses retained `task_struct` references and a raw spinlock;
no kernel task/vendor-data layout changes or numeric-PID identity are needed.
Only one frame can own a task's basic-VIP/hint overlay. Acquisition failures
leave its previous frame binding untouched. The real native VIP and current-hint
setters atomically withdraw the overlay before writing, even for the same value.
A displaced lease stays allocated for its client to release, but is unhashed:
old releases cannot touch a new token on the same task. Task death skips policy
restoration, releases references, and cannot retarget a reused PID. Allocation
and final puts/frees happen outside the spinlock.

C2PS no longer writes VIP/hint fields or compares their values before restoring.
It acquires the native token before publishing a frame, checks it before updates,
and releases it after serialized frame users stop. The task reaper handles
preempted tokens as well as task death/missing END. Actual patched legacy entry
bodies, native registry code and C2PS adapter run together in the targeted host
model (including same-value writes, two owners, concurrent writes and PID reuse).
This remains host evidence, NOT kernel/IRQ verification.

## Scheduler-owned clamp transaction

A third, scheduler-owned registry retains task references without changing any
kernel task layout. `scheduler-uclamp-lease.patch` exports acquire/apply/is_live/
release and integrates ordinary-write preemption into `__sched_setscheduler`
AFTER all validation/recheck/failure paths and dequeue/put_prev, BEFORE policy/
requested clamp mutations. It restores the baseline before the ordinary write,
so a partial min-only write cannot accidentally retain the temporary max.
An identical-value write revokes too; rejected writes do not. A displaced
handle cannot apply, detach a newer handle, or restore its old snapshot.

The baseline is captured at the first successful apply, under pi/rq locks, not
when reserving the handle. Apply/release are clamp-only transactions through
actual __setscheduler_uclamp and normal dequeue/put_prev/enqueue/set_next plus
balance-callback handling. They never feed zero priority/deadline parameters
into sched_setattr or recursively acquire rq/pi locks. Policy/class/priority
and cgroup/thermal limits are not changed. Reset non-user requests through
current scheduler defaults, including current RT defaults. RT-default sync
only writes non-user requests; applied overlays mark both requests user-defined.
Fork/init write unpublished/new tasks; normal kernel request inheritance is
unchanged and a child does NOT inherit a lease token. Cgroup effective-clamp
updates are not per-task requested-clamp writers.

Lock order is task pi -> rq -> short registry spinlock; tracing/vendor callbacks
run outside the registry lock. Static-key activation and allocation happen in
sleepable acquire outside scheduler locks; final puts/frees are outside them.
The client must join users before release. C2PS now acquires both task-resource
tokens before frame publication, unwinds the first if the second fails, uses the
scheduler API instead of copied rq snapshots, and reaps either revoked token.
An ESTALE during a regulator update immediately closes that frame and releases
its still-owned resource half; an ordinary validation failure does not close it.
This also covers hint preemption between clamp commit and hint commit. External
writer state is preserved by the displaced token's release. Its private
sched/sched.h include and compare-then-sched_setattr are removed.

Targeted host checks compile the actual scheduler registry/transaction bodies
and real core validation/mutation/RT-default functions with the C2PS adapter and
native field provider. Generic scheduler dispatch, rq/queue/hash primitives are
MODELED; these are not real IRQ, rq accounting, or kernel race proofs. Tests
cover allocation/busy unwind, same-value and partial writes, rejected writes,
old/new tokens, current RT defaults, dead tasks and concurrent writers.

Raw FPSGO dependency-TID lifetime, full native compilation/IRQ/lockdep/runtime
validation and the broader component audit remain deployment HOLDs.

## What is NOT established

No kernel compile, link, modpost or BTF run was performed for this revision:
the campaign storage gate reports metadata headroom below its 6 GiB heavy-build
restart margin. A separately reported owner ext4 build does not authorize us
to write that output or bypass the existing gate. Host-side ASan/UBSan models
and GNU make metadata parsing are not native API, ABI, provider or concurrency
proof. Remaining native provider behavior, C2PS session semantics and device
validation stay open. FPSGO/C2PS acceptance remains **HOLD**; stock
certification stays 440/458 and LieppOS OK stays 6/458.
