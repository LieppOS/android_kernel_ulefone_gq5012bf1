#!/usr/bin/env python3
"""Refresh a narrow pinned core-scheduler patch; never touch the owner worktree.

Component patch maintenance only, not a build orchestrator. Output is in the
owned source repository and optional explicit owned fixture directory.
"""
import argparse
import difflib
import hashlib
import json
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
out = root/'integration/target-c'
ap = argparse.ArgumentParser()
ap.add_argument('--fixture', type=Path)
a = ap.parse_args()
repo = '/home/<user>/kernel-work/noOEM09/dk12/src'
pin = 'f3f4cb91171896b13d40ad40290d641a0c350832'
path = 'kernel/sched/core.c'
old = subprocess.check_output(['git', '--no-optional-locks', '-C', repo,
    'show', pin+':'+path], cwd=root, text=True)
anchor = 'static int __sched_setscheduler(struct task_struct *p,'
assert old.count(anchor) == 1
new = old.replace(anchor, '#include "uclamp_lease.inc"\n\n'+anchor)
anchor = '\tprev_class = p->sched_class;\n\n\tif (!(attr->sched_flags & SCHED_FLAG_KEEP_PARAMS))'
assert new.count(anchor) == 1
new = new.replace(anchor, '\tprev_class = p->sched_class;\n\n'
    '\t/* Withdraw a temporary clamp overlay before the successful ordinary\n'
    '\t * scheduler write, including identical-value writes. Already dequeued,\n'
    '\t * pi/rq locked, past every error and policy-recheck path.\n'
    '\t */\n\tsched_uclamp_lease_preempt(p);\n\n'
    '\tif (!(attr->sched_flags & SCHED_FLAG_KEEP_PARAMS))')
added = {'kernel/sched/uclamp_lease.inc': (out/'scheduler/uclamp_lease.inc').read_text(),
    'include/linux/sched/uclamp_lease.h': (out/'scheduler/uclamp_lease.h').read_text()}
patch = 'diff --git a/'+path+' b/'+path+'\n'+''.join(difflib.unified_diff(
    old.splitlines(True), new.splitlines(True), fromfile='a/'+path, tofile='b/'+path))
for dest, content in added.items():
    exists = subprocess.check_output(['git','--no-optional-locks','-C',repo,
        'ls-tree',pin,'--',dest],cwd=root,text=True)
    assert not exists,dest
    patch += 'diff --git a/'+dest+' b/'+dest+'\nnew file mode 100644\n'+''.join(
        difflib.unified_diff([],content.splitlines(True),fromfile='/dev/null',tofile='b/'+dest))
sha = lambda text: hashlib.sha256(text.encode()).hexdigest()
(out/'scheduler-uclamp-lease.patch').write_text(patch)
(out/'scheduler-uclamp-lease.json').write_text(json.dumps({
    'kernel_commit':pin,'modified_files':{path:{'base_sha256':sha(old),'patched_sha256':sha(new)}},
    'new_files':{p:sha(c) for p,c in added.items()},'patch_sha256':sha(patch),
    'exports':['sched_uclamp_lease_acquire','sched_uclamp_lease_apply',
        'sched_uclamp_lease_is_live','sched_uclamp_lease_release'],
    'contract':'task-ref registry; baseline on first apply; all ordinary sched_setattr writes preempt under pi/rq locks after rejection paths; clamp-only queue/accounting transaction',
    'native_compile':False,'layout_changes':False,'ordinary_writer_callsite':'__sched_setscheduler after dequeue/put_prev, before params/uclamp',
    'other_req_writers':'RT default sync only changes non-user requests (owned applied requests are user-defined); fork/init write unpublished/new tasks; task-group writes are effective limits, not per-task request ownership',
    'inheritance':'normal kernel request inheritance unchanged; child does not inherit a lease token',
},indent=2)+'\n')
if a.fixture:
    a.fixture.mkdir(parents=True,exist_ok=True)
    (a.fixture/'core.c').write_text(new)
    for p,c in added.items():(a.fixture/Path(p).name).write_text(c)
print('Refreshed three-file scheduler ownership patch against',pin)
