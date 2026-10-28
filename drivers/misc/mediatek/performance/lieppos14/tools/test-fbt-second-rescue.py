#!/usr/bin/env python3
"""Run actual second-rescue arm/cancel/timer/worker C; kernel primitives modeled."""
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
text = (root/'public/drivers/misc/mediatek/performance/fpsgo_v3/fbt/src/fbt_cpu.c').read_text()

def function(name):
    m = re.search(r'^static (?:void|enum hrtimer_restart) '+name+r'\([^)]*\)\n\{', text, re.M)
    assert m, name
    end, depth = m.end(), 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[m.start():end]

structure = re.search(r'^struct fbt_sjerk \{.*?^\};', text, re.M | re.S).group(0)
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
typedef uint64_t u64;
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
#define container_of(ptr,type,member) ((type *)((char *)(ptr)-offsetof(type,member)))
#define clamp(v,lo,hi) ((v)<(lo)?(lo):((v)>(hi)?(hi):(v)))
#define HRTIMER_MODE_REL 1
#define HRTIMER_NORESTART 0
#define FBTCPU_SEC_DIVIDER 1000000000ULL
#define FPSGO_JERK_DISAPPEAR 1
#define FPSGO_JERK_ENOUGH_BUFFER 2
#define FPSGO_JERK_SECOND 3
#define FPSGO_HARD_NONE 0
#define GFP_KERNEL 0
#define fpsgo_systrace_c_fbt_debug(...) ((void)0)
#define fbt_print_rescue_info(...) ((void)0)
enum hrtimer_restart { host_norestart=0 };
struct hrtimer {bool active;};
struct work_struct {bool queued;};
struct pid {int nr, refs;bool alive;};
static struct pid *get_pid(struct pid *p) {if(p){assert(p->refs>0);p->refs++;}return p;}
static void put_pid(struct pid *p) {if(p)assert(--p->refs>=1);}
static int hrtimer_cancel(struct hrtimer *t) {t->active=false;return 0;}
static void hrtimer_start(struct hrtimer *t,unsigned long long n,int mode) {t->active=true;}
static unsigned long long ns_to_ktime(unsigned long long n) {return n;}
static void *wq_jerk;
/* suspend epoch: production rescue work carries the epoch it was armed in */
static unsigned int fbt_pm_epoch;
static inline bool fbt_pm_epoch_current(unsigned int armed)
{ return armed == READ_ONCE(fbt_pm_epoch); }
static bool queue_work(void *q,struct work_struct *w) {bool old=w->queued;w->queued=true;return !old;}
static bool schedule_work(struct work_struct *w) {return queue_work(NULL,w);}
struct identity {struct pid *tid;};
struct render_info {
 int pid,linger;unsigned long long identifier,buffer_id,t_enqueue_start,t_enqueue_end;
 unsigned long long t_dequeue_start,t_dequeue_end,dequeue_length,buffer_quota_ts;
 int tgid,buffer_quota;
 struct identity task_identity;
 struct {int rescue_second_time_by_pid,rescue_second_enable_by_pid,rescue_second_group_by_pid,separate_aa_by_pid,check_buffer_quota_by_pid;} attr;
 struct {int sbe_rescue,last_blc,last_blc_b,last_blc_m,cur_stage;struct {int active_jerk_id;} proc;} boost_info;
 int thr_mlock;
};
static bool fpsgo_task_identity_alive(const struct identity *id) {return id->tid && id->tid->alive;}
static int fbt_mlock,tree_locked;
static int fbt_enable=1,rescue_second_time=2,max_blc_pid=41,rescue_opp_f,nr_freq_cpu=4;
static int rescue_second_enhance_f,rescue_second_copp,max_blc_stage;
static unsigned long long max_blc_buffer_id=7;
struct cpu_ctrl_data {int value;};
static void *kcalloc(size_t n,size_t s,int flags) {return calloc(n,s);}
#define kfree free
static int cluster_num=2,boost_calls,ceiling_calls;
static void mutex_lock(int *p) {assert(!*p);*p=1;}
static void mutex_unlock(int *p) {assert(*p);*p=0;}
static void fpsgo_render_tree_lock(const char *s) {assert(!tree_locked);tree_locked=1;}
static void fpsgo_render_tree_unlock(const char *s) {assert(tree_locked);tree_locked=0;}
static void fpsgo_thread_lock(int *p) {assert(tree_locked);mutex_lock(p);}
static void fpsgo_thread_unlock(int *p) {mutex_unlock(p);}
static struct render_info *lookup;
static bool replace_during_lookup;
static struct pid *replacement_identity;
static struct render_info *fpsgo_search_and_add_render_info(int pid,unsigned long long id,int add);
static int fbt_check_to_jerk(unsigned long long a,unsigned long long b,unsigned long long c,unsigned long long d,
 unsigned long long e,int pid,unsigned long long bid,int tgid,unsigned long long ts,int quota,int check) {return 0;}
static unsigned int fbt_get_new_base_blc(struct cpu_ctrl_data *p,unsigned int b,int e,int o,int c) {return b;}
static void fbt_set_hard_limit_locked(int n,struct cpu_ctrl_data *p) {assert(fbt_mlock);}
static void fbt_set_ceiling(struct cpu_ctrl_data *p,int pid,unsigned long long b) {assert(fbt_mlock);ceiling_calls++;}
static void fbt_do_jerk_boost(struct render_info *r,int a,int b,int c,int g,int j) {assert(fbt_mlock);boost_calls++;}
'''
after = r'''
static struct fbt_sjerk sjerk;
'''
# Lookup is deliberately allowed to change the pending arm while the worker's
# fbt mutex is dropped. Its private PID snapshot must survive that replacement.
lookup_body = r'''
static struct render_info *fpsgo_search_and_add_render_info(int pid,unsigned long long id,int add) {
 if(replace_during_lookup) {
  assert(!fbt_mlock);replace_during_lookup=false;
  mutex_lock(&fbt_mlock);fbt_cancel_sjerk();
  sjerk.identity=get_pid(replacement_identity);sjerk.jerking=1;
  sjerk.pid=pid;sjerk.identifier=id;sjerk.active_id=0;
  WRITE_ONCE(sjerk.work_seq,sjerk.arm_seq);mutex_unlock(&fbt_mlock);
 }
 return lookup;
}
'''
suffix = r'''
static void arm(struct render_info *r) {
 tree_locked=1;mutex_lock(&fbt_mlock);lookup=r;
 fbt_set_sjerk(r->pid,r->buffer_id,r->identifier,0,10);
 mutex_unlock(&fbt_mlock);tree_locked=0;
}
static void fire(void) {fbt_sjerk_tfn(&sjerk.timer);sjerk.work.queued=false;fbt_do_sjerk(&sjerk.work);}
int main(void) {
 struct pid old={41,1,true}, recycled={41,1,true};
 struct render_info r={.pid=41,.identifier=9,.buffer_id=7,.task_identity={&old},
  .attr={.rescue_second_time_by_pid=2,.rescue_second_enable_by_pid=1},.boost_info={.last_blc=30}};
 arm(&r);assert(old.refs==2);fire();assert(boost_calls==1 && ceiling_calls==1 && old.refs==1);
 arm(&r);fbt_sjerk_tfn(&sjerk.timer);
 mutex_lock(&fbt_mlock);fbt_cancel_sjerk();mutex_unlock(&fbt_mlock);
 fbt_do_sjerk(&sjerk.work);assert(boost_calls==1 && old.refs==1);
 arm(&r);r.task_identity.tid=&recycled;fire();
 assert(boost_calls==1 && old.refs==1 && recycled.refs==1);
 r.task_identity.tid=&old;arm(&r);old.alive=false;fire();assert(boost_calls==1 && old.refs==1);
 old.alive=true;arm(&r);fbt_enable=0;fire();assert(boost_calls==1 && old.refs==1);fbt_enable=1;
 arm(&r);replacement_identity=&recycled;replace_during_lookup=true;
 fire();assert(boost_calls==1 && old.refs==1 && recycled.refs==2 && sjerk.jerking);
 mutex_lock(&fbt_mlock);fbt_cancel_sjerk();mutex_unlock(&fbt_mlock);
 assert(recycled.refs==1);
 arm(&r);lookup=NULL;fire();assert(old.refs==1 && !sjerk.jerking);
 old.alive=false;arm(&r);assert(!sjerk.jerking && old.refs==1);
 assert(!fbt_mlock && !tree_locked);
 puts("PASS actual FBT second-rescue worker: PID ownership, same-number replacement, death, disable, canceled generations, concurrent replacement snapshot, missing renderer and balanced refs");
}
'''
bodies = function('fbt_cancel_sjerk')+'\n'+function('fbt_set_sjerk')+'\n'+function('fbt_do_sjerk')+'\n'+function('fbt_sjerk_tfn')
c = a.output/'second-rescue.c'
c.write_text(prefix+structure+after+bodies+lookup_body+suffix)
exe = a.output/'second-rescue'
subprocess.run([os.environ.get('CC','clang'),'-std=gnu11','-O1','-g',
    '-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
