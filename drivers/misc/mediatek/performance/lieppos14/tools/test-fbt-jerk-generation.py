#!/usr/bin/env python3
"""Exercise actual FBT jerk generation, timer publication and invalidation bodies.

Timer/workqueue primitives are modeled. This proves stale-generation rejection and
lock-safe cancellation bookkeeping, not native hrtimer/workqueue ordering.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[1]
src = root/'public/drivers/misc/mediatek/performance/fpsgo_v3/fbt/src/fbt_cpu.c'
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('output', type=Path)
a = ap.parse_args()
a.output.mkdir(exist_ok=False)

def leaf(text, name):
    m = re.search(r'^(?:static )?(?:inline )?(?:bool|void|enum hrtimer_restart)\s+'+
        name+r'\([^)]*\)\n\{', text, re.M)
    assert m, name
    i=m.end(); depth=1
    while depth:
        depth += (text[i]=='{')-(text[i]=='}'); i += 1
    return text[m.start():i]

t = src.read_text()
bodies = '\n'.join(leaf(t,n) for n in [
    'fbt_jerk_generation_current','fbt_jerk_tfn','fbt_init_jerk',
    'fpsgo_base2fbt_cancel_jerk'])
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#define RESCUE_TIMER_NUM 5
#define CLOCK_MONOTONIC 1
#define HRTIMER_MODE_REL 1
#define HRTIMER_NORESTART 0
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
#define container_of(ptr,type,member) ((type *)((char *)(ptr)-offsetof(type,member)))
enum hrtimer_restart { host_norestart=HRTIMER_NORESTART };
struct hrtimer {enum hrtimer_restart (*function)(struct hrtimer *);int initialized;};
struct work_struct {void (*function)(struct work_struct *);int initialized,queued;};
struct workqueue_struct {int unused;};
struct fbt_jerk {
 int id,jerking,postpone,last_check;unsigned int arm_seq,work_seq;
 unsigned long long frame_qu_ts;struct hrtimer timer;struct work_struct work;
};
struct fbt_proc {int active_jerk_id;struct fbt_jerk jerks[RESCUE_TIMER_NUM];};
struct fbt_boost_info {struct fbt_proc proc;};
struct render_info {bool jerks_initialized;struct fbt_boost_info boost_info;};
static struct workqueue_struct *wq_jerk;
static int timer_cancels,queued_work;
static void hrtimer_init(struct hrtimer *t,int clock,int mode) {t->initialized=1;}
#define INIT_WORK(w,fn) do {(w)->function=(fn);(w)->initialized=1;} while(0)
static int hrtimer_cancel(struct hrtimer *t) {assert(t->initialized);timer_cancels++;return 0;}
static bool queue_work(struct workqueue_struct *q,struct work_struct *w) {
 if(w->queued)return false;w->queued=1;queued_work++;return true;
}
static bool schedule_work(struct work_struct *w) {return queue_work(NULL,w);}
static void fbt_do_jerk(struct work_struct *w) {(void)w;}
'''
suffix = r'''
int main(void) {
 struct render_info r={.jerks_initialized=true};
 for(int i=0;i<RESCUE_TIMER_NUM;i++)fbt_init_jerk(&r.boost_info.proc.jerks[i],i);
 struct fbt_jerk *j=&r.boost_info.proc.jerks[2];
 assert(j->id==2 && j->timer.initialized && j->work.initialized);
 assert(fbt_jerk_generation_current(j)); /* initial zero generation */
 j->arm_seq++;j->jerking=1;j->frame_qu_ts=99;
 assert(!fbt_jerk_generation_current(j));
 j->timer.function(&j->timer);
 assert(fbt_jerk_generation_current(j) && j->work.queued && queued_work==1);
 fpsgo_base2fbt_cancel_jerk(&r);
 assert(timer_cancels==RESCUE_TIMER_NUM);
 assert(!j->jerking && !j->postpone && !j->frame_qu_ts);
 assert(!fbt_jerk_generation_current(j)); /* queued generation is stale */
 unsigned int cancelled=j->arm_seq;
 j->arm_seq++;j->jerking=1;j->frame_qu_ts=100;
 assert(j->arm_seq==cancelled+1 && !fbt_jerk_generation_current(j));
 /* A stale worker must not clear this newer arm. */
 if(fbt_jerk_generation_current(j) && !j->postpone)j->jerking=0;
 assert(j->jerking);
 j->work.queued=0;j->timer.function(&j->timer);
 assert(fbt_jerk_generation_current(j) && queued_work==2);
 r.jerks_initialized=false;fpsgo_base2fbt_cancel_jerk(&r);
 assert(timer_cancels==RESCUE_TIMER_NUM);
 puts("PASS actual FBT jerk generation: timer snapshots arms, cancellation invalidates queued work, stale work cannot clear a newer arm, uninitialized nodes are untouched.");
}
'''
c=a.output/'jerk-generation.c'; c.write_text(prefix+bodies+suffix)
exe=a.output/'jerk-generation'
subprocess.run([os.environ.get('CC','clang'),'-std=gnu11','-g','-O1',
    '-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
(a.output/'SOURCE.sha256').write_text(hashlib.sha256(bodies.encode()).hexdigest()+'\n')
