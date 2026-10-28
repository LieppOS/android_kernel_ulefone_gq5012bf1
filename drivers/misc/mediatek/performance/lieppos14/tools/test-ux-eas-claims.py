#!/usr/bin/env python3
"""Run the real UX EAS-control claim accounting.

Extracts the production claim table, acquire and release paths and drives them
with overlapping renderers, changing dependency lists, mid-frame destruction,
table exhaustion and repeated frames, asserting that no shared resource is
revoked by a non-owner or left asserted without one.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('output', type=Path)
a = p.parse_args()
a.output.mkdir(exist_ok=False)

src = (root/'public/drivers/misc/mediatek/performance/fpsgo_v3/fbt/src/fbt_cpu_ux.c').read_text()

# the production claim storage and limits, verbatim
block = re.search(r'#define UX_EAS_MAX_OWNERS.*?static DEFINE_MUTEX\(ux_eas_lock\);\n', src, re.S)
assert block, 'claim table missing'


def take(signature):
    m = re.search(r'^' + re.escape(signature) + r'\n\{', src, re.M)
    assert m, signature
    end, depth = m.end(), 1
    while depth:
        depth += (src[end] == '{') - (src[end] == '}')
        end += 1
    return src[m.start():end] + '\n'


wanted = [
    'static int ux_eas_dep_users_locked(int dep_pid)',
    'static int ux_eas_find_owner_locked(const struct render_info *thr)',
    'static int ux_eas_get_owner_locked(const struct render_info *thr)',
    'static void ux_eas_drop_claim_locked(int index)',
    'static bool ux_eas_wanted(const int *deps, int count, int dep_pid)',
    'static void ux_eas_acquire(struct render_info *thr, const int *deps, int count)',
    'void fbt_ux_release_eas_claims(struct render_info *thr)',
]
body = block.group(0) + '\n' + '\n'.join(take(s) for s in wanted)

# every release path must be wired up in production sources
base = (root/'public/drivers/misc/mediatek/performance/fpsgo_v3/base/src/fpsgo_base.c').read_text()
assert 'fbt_ux_release_eas_claims(thr);' in base, 'destruction path does not release claims'
assert 'fbt_ux_release_eas_claims(thr);' in src, 'reset path does not release claims'
assert 'set_top_grp_aware(1, 0)' in src and src.count('set_top_grp_aware(') == 3, \
    'group-aware must be asserted only through claim accounting'

prefix = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define IS_ENABLED(x) 1
#define CONFIG_MTK_SCHED_GROUP_AWARE 1
#define pr_warn_ratelimited(...) ((void)0)
#define DEFINE_MUTEX(n) int n
#define mutex_lock(m) do { lock_depth++; } while (0)
#define mutex_unlock(m) do { lock_depth--; } while (0)
static int lock_depth;

struct render_info { int pid; unsigned long long buffer_id; };

/* modeled shared system resources */
static int grp_aware;
static int grp_aware_transitions;
#define MODEL_MAX_PID 4096
static int vip_state[MODEL_MAX_PID];
static int vip_sets, vip_clears;

static void set_top_grp_aware(int val, int force)
{
	(void)force;
	assert(val != grp_aware);           /* never redundant, never nested */
	grp_aware = val;
	grp_aware_transitions++;
}
static void set_task_basic_vip(int pid)
{
	assert(pid > 0 && pid < MODEL_MAX_PID);
	assert(!vip_state[pid]);            /* double assert is an ownership bug */
	vip_state[pid] = 1;
	vip_sets++;
}
static void unset_task_basic_vip(int pid)
{
	assert(pid > 0 && pid < MODEL_MAX_PID);
	assert(vip_state[pid]);             /* release without a claim is a bug */
	vip_state[pid] = 0;
	vip_clears++;
}
'''

suffix = r'''
static int live_vips(void)
{
	int i, n = 0;

	for (i = 0; i < MODEL_MAX_PID; i++)
		n += vip_state[i];
	return n;
}

int main(void)
{
	struct render_info a = { .pid = 100, .buffer_id = 1 };
	struct render_info b = { .pid = 200, .buffer_id = 2 };
	struct render_info c = { .pid = 300, .buffer_id = 3 };
	int shared[] = { 11, 12 };
	int a_deps[] = { 11, 12, 13 };
	int b_deps[] = { 12, 14 };
	int i;

	/* 1. first owner asserts the shared request, second does not re-assert */
	ux_eas_acquire(&a, a_deps, 3);
	assert(grp_aware == 1 && grp_aware_transitions == 1);
	assert(vip_state[11] && vip_state[12] && vip_state[13]);
	ux_eas_acquire(&b, b_deps, 2);
	assert(grp_aware == 1 && grp_aware_transitions == 1);
	assert(vip_state[14] && vip_sets == 4);   /* 12 was already claimed */

	/* 2. one renderer ending a frame must not revoke the other's requests */
	fbt_ux_release_eas_claims(&b);
	assert(grp_aware == 1);                   /* a still owns it */
	assert(vip_state[12]);                    /* still claimed by a */
	assert(!vip_state[14]);                   /* b's exclusive dep released */

	/* 3. a dependency that leaves the list is released, not orphaned */
	ux_eas_acquire(&a, shared, 2);
	assert(!vip_state[13] && vip_state[11] && vip_state[12]);

	/* 4. repeated frames are idempotent */
	for (i = 0; i < 100; i++)
		ux_eas_acquire(&a, shared, 2);
	assert(grp_aware_transitions == 1 && live_vips() == 2);

	/* 5. destruction mid-frame releases everything the owner held */
	ux_eas_acquire(&c, (int[]){ 21, 22 }, 2);
	fbt_ux_release_eas_claims(&a);
	assert(grp_aware == 1);                   /* c is still live */
	assert(!vip_state[11] && !vip_state[12]);
	fbt_ux_release_eas_claims(&c);
	assert(grp_aware == 0 && grp_aware_transitions == 2);
	assert(live_vips() == 0);

	/* 6. releasing an unknown or already released owner is a no-op */
	fbt_ux_release_eas_claims(&a);
	fbt_ux_release_eas_claims(NULL);
	assert(grp_aware == 0 && live_vips() == 0);

	/* 7. identity is (pid, buffer): a different buffer is a different owner */
	{
		struct render_info a2 = { .pid = 100, .buffer_id = 9 };

		ux_eas_acquire(&a, shared, 2);
		ux_eas_acquire(&a2, shared, 2);
		assert(grp_aware == 1);
		fbt_ux_release_eas_claims(&a);
		assert(vip_state[11] && vip_state[12] && grp_aware == 1);
		fbt_ux_release_eas_claims(&a2);
		assert(!vip_state[11] && grp_aware == 0);
	}

	/* 8. owner table exhaustion fails closed */
	{
		struct render_info owners[UX_EAS_MAX_OWNERS + 4];
		int dep = 1000;

		for (i = 0; i < UX_EAS_MAX_OWNERS + 4; i++) {
			owners[i].pid = 500 + i;
			owners[i].buffer_id = i;
			ux_eas_acquire(&owners[i], &dep, 1);
		}
		assert(grp_aware == 1 && vip_state[dep]);
		for (i = 0; i < UX_EAS_MAX_OWNERS + 4; i++)
			fbt_ux_release_eas_claims(&owners[i]);
		assert(grp_aware == 0 && !vip_state[dep] && live_vips() == 0);
	}

	/* 9. claim table exhaustion fails closed and still unwinds cleanly */
	{
		struct render_info owners[UX_EAS_MAX_OWNERS];
		int deps[64];
		int j;

		for (j = 0; j < 64; j++)
			deps[j] = 2000 + j;
		for (i = 0; i < UX_EAS_MAX_OWNERS; i++) {
			owners[i].pid = 900 + i;
			owners[i].buffer_id = 100 + i;
			ux_eas_acquire(&owners[i], deps, 64);
		}
		for (i = 0; i < UX_EAS_MAX_OWNERS; i++)
			fbt_ux_release_eas_claims(&owners[i]);
		assert(grp_aware == 0 && live_vips() == 0);
	}

	assert(lock_depth == 0 && vip_sets == vip_clears);
	puts("PASS actual UX EAS claim accounting: shared group-aware request and "
	     "dependency VIP are reference-counted per owner, non-owners cannot "
	     "revoke, departing dependencies are released, destruction releases "
	     "everything, and both tables fail closed");
	return 0;
}
'''

c = a.output/'ux-eas-claims.c'
c.write_text(prefix + body + suffix)
exe = a.output/'ux-eas-claims'
subprocess.run([os.environ.get('CC', 'clang'), '-std=gnu11', '-O1', '-g',
                '-fsanitize=address,undefined', str(c), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True)
