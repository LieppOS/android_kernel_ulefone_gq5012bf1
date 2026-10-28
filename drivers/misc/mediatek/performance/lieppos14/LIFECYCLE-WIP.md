# Campaign14 lifecycle implementation — WIP / HOLD

Earlier compile-tested production milestone `4be98fa3a3b5e0c99f7f7def1618fa10ff4c8abe` added: transactional framework/FBT construction, reverse ownership rollback, sysfs commit/quiesce, deferred publication/timers, and stop-aware owned event disposal. Both real target incremental builds passed strict modpost. Their changed C/header bytes match the committed production files exactly. Fresh independent revalidation is pending; the first resumed gate check mistakenly inspected shell rc only (the existing gate exits0 on FAIL), so those two builds ran despite FAIL:metadata. No further heavy builds are allowed until its actual output ends PASS.

This separate WIP branch adds actual production C for private algorithm lifetime:

* Every maintained private callback invocation takes an SRCU lease and shared private-state mutex. The latter serializes private lists accessed under different FBT/FSTB/base locks. It is not a replacement for governor/task identity correctness.
* begin/commit makes registration invisible until all private initialization/readiness steps succeed. Failed init revokes every publication before its module text/BSS can be released.
* revoke disables new leases, unregisters and synchronizes trace-ring writers under the XGF registration lock, drains all active private readers, then clears every slot/ring/readiness flag.
* Private cleanup destroys all owned FSTB/XGF/RL records; FSTB destruction now frees active per-path dependency allocations without re-freeing stale inactive addresses.
* A private exit handler uses the same revoke-and-destroy path. Full runtime, lockdep, fault injection and unload behavior are NOT proved by source inspection.

## Subsequent dispatcher continuation

The unchanged infrastructure gate recovered externally to PASS (recorded2026-10-20T08:59:05+03:00, headroom6,527,221,760 bytes). The guarded stock19/custom13 incremental subsystem builds both passed strict modpost, compiling the private source above and new actual dispatcher C. No CRC/metadata/acceptance or resource floor was changed, no cleanup was performed, and no old build was substituted as evidence for this source.

* Managed native/compat FPSGO, XGFF and LR ioctls take SRCU leases across all indirect calls; MAGT setters share the protocol while local CPU queries remain independent.
* Bind occurs after constructing the provider's queues. Publication is release-ordered after the full callback table and main queue are ready. Failed construction revokes only its successfully bound dispatcher.
* Revoke closes admission, wakes all owned sleeping FPS/command/active/camera readers, drains SRCU, then clears the managed publications. Waiting for SRCU before waking these readers would deadlock.
* Waiters observe explicit closure and initialize legacy void outputs; active-wait errors propagate. Producers cannot append after closing their queue. Component exits free queued command allocations. The successful ADPF callback handle now persists for future teardown.
* Unsupported pointer-bearing XGFF boost compat calls return EOPNOTSUPP instead of fabricated success. The static SRCU definitions use the actual kernel's DEFINE_STATIC_SRCU macro.

**Compile-tested is not runtime/unload acceptance or completed #51.** Full frame/kernel fault injection and lockdep execution are still absent. Subsequent PowerHAL-lifetime WIP now implements source-owned ioctl provider admission/revocation, boost-reader wake-before-drain, full service proc/event/session/queue/QoS teardown and guarded boost-registration controls. Composer rejects duplicate/closed registrations, joins in-flight callbacks under its registry mutex, sends final boost0 and closes the table. This later code is NOT kernel-built: the original gate failed again below6GiB. Only the production ioctl TU's explicit host lease model was exercised (ASan/UBSan PASS), not kernel SRCU or complete service unload. Next source work remains validating this WIP, full render/linger/timer/system-work rundown, remaining task/PID lifetimes and native callback families. Legacy void getters still cannot return interrupt errors through their unchanged callback signature. Core/framework remains non-unloadable. The WIP service exit handler is not an acceptance claim and must pass actual target builds plus full teardown review before being eligible for integration. Native target TODO52 is still unfinished and not hidden by this work.
