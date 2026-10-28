#!/usr/bin/env python3
"""Source invariants + actual reclaim C under tiny host primitives, NOT kernel proof."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[1]
perf = root / 'public/drivers/misc/mediatek/performance'
core = perf / 'fpsgo_v3'

def function(text, signature):
    start = text.index(signature)
    brace = text.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('output', type=Path)
args = ap.parse_args()
args.output.mkdir(exist_ok=False)
main = (core / 'fpsgo_main.c').read_text()
init = function(main, 'static int __init fpsgo_init(void)')
assert 'unregister_trace_android_rvh_' not in main
assert init.index('fpsgo_sysfs_prepare_commit()') < init.index('__module_get(THIS_MODULE)')
assert init.index('__module_get(THIS_MODULE)') < init.index('register_fpsgo_android_cpufreq_transition_hook()')
ready = init.index('smp_store_release(&fpsgo_runtime, FPSGO_READY)')
commit_tail = init[ready:init.index('rollback:')]
assert not re.search(r'\b(kzalloc|kmalloc|fpsgo_alloc_atomic|kthread_create|register_\w+)\s*\(', commit_tail)
assert not re.search(r'\bgoto\s+\w+', commit_tail)
assert 'fpsgo_switch_enable(1)' not in commit_tail
assert init.index('initial_enable = kzalloc') < ready
assert init.index('fpsgo_sysfs_publish()') > ready
headers = {}
for p in perf.rglob('*.h'):
    headers.setdefault(p.name, []).append(p)
scoped = 0
for p in perf.rglob('*'):
    if p.suffix not in ('.c', '.h'):
        continue
    for name in re.findall(r'^#include ["<]([^">]+)[">]', p.read_text(), re.M):
        matches = headers.get(Path(name).name, [])
        if len(matches) == 1:
            assert (p.parent / name).is_file(), (p, name)
            scoped += 1
base = (core / 'base/src/fpsgo_base.c').read_text()
fbt = (core / 'fbt/src/fbt_cpu.c').read_text()
assert 'vfree(thr)' not in fbt
assert 'vfree(data)' not in base and 'vfree(pos)' not in base
body = function(base, 'static void fpsgo_release_render_work(')
# Compile the actual changed production function, not a rewritten algorithm.
# These primitives test ownership/order only, not kernel timers/workqueue races.
prefix = r'''
#include <assert.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#define RESCUE_TIMER_NUM 5
#define container_of(p,t,m) ((t *)((char *)(p) - offsetof(t,m)))
struct work_struct { int id; };
struct hrtimer { int id; };
struct jerk { struct hrtimer timer; struct work_struct work; };
struct render_info {
    struct work_struct reclaim_work;
    bool jerks_initialized;
    struct { struct { struct jerk jerks[RESCUE_TIMER_NUM]; } proc; } boost_info;
};
static int timers, works, ux, freed;
static bool initialized;
static void hrtimer_cancel(struct hrtimer *t) { assert(initialized); timers++; }
static void cancel_work_sync(struct work_struct *w) {
    assert(initialized && timers == RESCUE_TIMER_NUM); works++;
}
static void fpsgo_ux_free_frame_info(struct render_info *r) {
    assert(works == (initialized ? RESCUE_TIMER_NUM : 0)); ux++;
}
static void vfree(struct render_info *r) { assert(ux == 1); freed++; }
'''
suffix = r'''
int main(void) {
    for (int i = 0; i < 2; i++) {
        struct render_info r = {0};
        timers = works = ux = freed = 0;
        r.jerks_initialized = initialized = i;
        fpsgo_release_render_work(&r.reclaim_work);
        assert(freed == 1 && ux == 1);
        assert(timers == (i ? RESCUE_TIMER_NUM : 0));
        assert(works == timers);
    }
    puts("PASS: actual production reclaim function timer/work/UX/free ordering");
}
'''
source = args.output / 'reclaim.c'
source.write_text(prefix + body + suffix)
cmd = [os.environ.get('CC', 'clang'), '-std=gnu11', '-O1', '-g',
       '-fsanitize=address,undefined', str(source), '-o', str(args.output / 'reclaim')]
subprocess.run(cmd, check=True)
subprocess.run([str(args.output / 'reclaim')], check=True)
# The real target has a nonstandard positive ENOMEM return in rvh_func_add.
# Compile our actual adapter, since a positive module-init result means success.
hook = function(main, 'int register_fpsgo_android_cpufreq_transition_hook(void)')
hook_model = r'''
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
struct cpufreq_policy { int unused; };
static int registration_result;
static void fpsgo_cpu_frequency_cap_tracer(void *p, struct cpufreq_policy *q) {}
#define pr_info(...) ((void)0)
static int register_trace_android_rvh_cpufreq_transition(
    void (*fn)(void *, struct cpufreq_policy *), void *data) {
    assert(fn == fpsgo_cpu_frequency_cap_tracer && data == NULL);
    return registration_result;
}
'''
hook_main = r'''
int main(void) {
    const int inputs[] = {0, ENOMEM, -ENOMEM, -EBUSY, -EINVAL};
    for (unsigned int i = 0; i < sizeof(inputs) / sizeof(inputs[0]); i++) {
        registration_result = inputs[i];
        assert(register_fpsgo_android_cpufreq_transition_hook() ==
            (inputs[i] > 0 ? -inputs[i] : inputs[i]));
    }
    puts("PASS: actual RVH adapter normalizes positive allocator errors");
}
'''
hook_source = args.output / 'restricted_hook.c'
hook_source.write_text(hook_model + hook + hook_main)
hook_cmd = [os.environ.get('CC', 'clang'), '-std=gnu11', '-O1', '-g',
            '-fsanitize=address,undefined', str(hook_source), '-o', str(args.output / 'restricted_hook')]
subprocess.run(hook_cmd, check=True)
subprocess.run([str(args.output / 'restricted_hook')], check=True)
result = {'scope': __doc__, 'owned_includes_scoped': scoped,
          'rvh_error_adapter_sha256': hashlib.sha256(hook.encode()).hexdigest(),
          'positive_registration_errno_normalized': True,
          'restricted_unregister_absent': True, 'publication_after_preparation': True,
          'initial_enable_preallocated': True,
          'reclaim_function_sha256': hashlib.sha256(body.encode()).hexdigest(),
          'kernel_compile': False, 'kernel_concurrency_test': False}
(args.output / 'RESULT.json').write_text(json.dumps(result, indent=2) + '\n')
print('PASS: scoped owned headers and irreversible-commit source invariants (not native compile)')
