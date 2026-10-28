#!/usr/bin/env python3
"""Run the actual referenced-task adapter under host kernel primitives, not kernel ABI/concurrency proof."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[1]
f = root / 'public/drivers/misc/mediatek/c2ps/common/src/c2ps_task.c'
production = f.read_text()
body = re.sub(r'^#include[^\n]*\n', '', production, flags=re.M)
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('output', type=Path)
ap.add_argument('--native-fixture', type=Path,
    help='also compile the actual patched vip.c/nonlinear_opp_cap.c legacy entrypoints')
ap.add_argument('--vip-disabled', action='store_true',
    help='compile without native VIP storage/support; require honest unsupported errors')
ap.add_argument('--scheduler-fixture', type=Path, default=root.parent/'scheduler-lease-fixture',
    help='committed core.c fixture produced by refresh-uclamp-lease.py')
a = ap.parse_args()
a.output.mkdir(exist_ok=False)
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#define IS_ENABLED(x) (x)
#define CONFIG_MTK_PERF_CTRL_LEASE 1
#define CONFIG_MTK_SCHED_VIP_TASK 1
#define GFP_KERNEL 0
#define EXPORT_SYMBOL_GPL(x)
#define U32_MAX UINT32_MAX
#define UCLAMP_MIN 0
#define UCLAMP_MAX 1
#define PIDTYPE_PID 0
#define PF_KTHREAD 0x200000
#define SCHED_FLAG_KEEP_ALL 1
#define SCHED_FLAG_UTIL_CLAMP_MIN 0x20
#define SCHED_FLAG_UTIL_CLAMP_MAX 0x40
#define SCHED_FLAG_UTIL_CLAMP (SCHED_FLAG_UTIL_CLAMP_MIN|SCHED_FLAG_UTIL_CLAMP_MAX)
#define SCHED_CAPACITY_SCALE 1024
#define DEQUEUE_SAVE 1
#define DEQUEUE_NOCLOCK 2
#define likely(x) (x)
#define unlikely(x) (x)
#define for_each_clamp_id(i) for((i)=0;(i)<2;(i)++)
typedef uint32_t u32;
enum uclamp_id { unused_enum_id };
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
#define clamp(x,a,b) ((x)<(a)?(a):((x)>(b)?(b):(x)))
#define lockdep_assert_held(p) assert(*(p))
struct pid { int refs, nr; };
struct mtk_task { struct { int hint; } cu_hint; struct { bool basic_vip; } vip_task; };
struct uclamp { unsigned int value; bool user_defined; };
struct task_struct { int refs; bool alive; int exit_state; unsigned int flags; struct pid *identity;
    struct uclamp uclamp_req[2]; struct mtk_task android_vendor_data1[1];
    bool pi_lock, is_rt, queued, running; };
#define uclamp_se uclamp
struct c2ps_task_info { bool mlock; bool is_active, is_dynamic_tid, is_vip_task;
    struct task_struct *task; struct mtk_perf_task_lease *native_task;
    struct pid *identity; int pid; uint64_t frame_seq;
    struct sched_uclamp_lease *uclamp_lease; };
struct regulator_req { struct c2ps_task_info *tsk_info; uint64_t frame_seq; };
struct rq { struct task_struct *stop; }; struct rq_flags { int unused; };
struct balance_callback {int unused;};
struct sched_attr { unsigned int sched_flags; uint32_t sched_util_min, sched_util_max; };
static struct task_struct *lookup;
static int attr_error, attr_calls;
static void rcu_read_lock(void) {} static void rcu_read_unlock(void) {}
static struct task_struct *find_task_by_vpid(int pid) {
    return lookup && lookup->identity->nr==pid && lookup->alive ? lookup : NULL;
}
static void get_task_struct(struct task_struct *p) { assert(p->refs>0); p->refs++; }
static void put_task_struct(struct task_struct *p) { assert(p->refs>1); p->refs--; }
static bool pid_alive(struct task_struct *p) { return p->alive; }
static struct pid *get_task_pid(struct task_struct *p, int type) {
    if (!p->alive) return NULL; p->identity->refs++; return p->identity;
}
static void put_pid(struct pid *p) { if (p) { assert(p->refs>1); p->refs--; } }
static int task_pid_nr(struct task_struct *p) { return p->identity->nr; }
static pthread_mutex_t rq_mutex=PTHREAD_MUTEX_INITIALIZER;
static _Thread_local bool rq_held;
static _Thread_local int preempt_count;
static struct rq rq_model;
static struct rq *task_rq_lock(struct task_struct *p, struct rq_flags *r) {
    assert(!rq_held && p->refs>1);pthread_mutex_lock(&rq_mutex);
    assert(!p->pi_lock);p->pi_lock=true;rq_held=true;return &rq_model;
}
static void task_rq_unlock(struct rq *r, struct task_struct *p, struct rq_flags *f) {
    assert(rq_held && p->pi_lock);p->pi_lock=false;rq_held=false;pthread_mutex_unlock(&rq_mutex);
}
static int sysctl_sched_uclamp_util_min_rt_default=1024;
static bool rt_task(struct task_struct *p) {return p->is_rt;}
static unsigned int uclamp_none(int i) {return i ? 1024 : 0;}
static void uclamp_se_set(struct uclamp *p,unsigned int v,bool u) {
    assert(rq_held);attr_calls++;p->value=v;p->user_defined=u;
}
static void (*on_clamp_set)(struct task_struct *);
#define trace_android_vh_setscheduler_uclamp(p,i,v) do {if(on_clamp_set){void (*fn)(struct task_struct *)=on_clamp_set;on_clamp_set=NULL;fn(p);}} while(0)
#define trace_android_vh_uclamp_validate(p,a,u,r,d) do {assert(!rq_held);if(attr_error){*(r)=attr_error;*(d)=true;}} while(0)
static int sched_uclamp_used;
static void static_branch_enable(int *p) {assert(!rq_held);*p=1;}
static bool task_on_rq_queued(struct task_struct *p) {return p->queued;}
static bool task_current(struct rq *r,struct task_struct *p) {return p->running;}
static void update_rq_clock(struct rq *r) {assert(rq_held);}
static void dequeue_task(struct rq *r,struct task_struct *p,int f) {assert(rq_held && p->queued);p->queued=false;}
static void enqueue_task(struct rq *r,struct task_struct *p,int f) {assert(rq_held && !p->queued);p->queued=true;}
static void put_prev_task(struct rq *r,struct task_struct *p) {assert(rq_held && p->running);}
static void set_next_task(struct rq *r,struct task_struct *p) {assert(rq_held && p->running);}
static void preempt_disable(void) {assert(rq_held);preempt_count++;}
static void preempt_enable(void) {assert(!rq_held && preempt_count==1);preempt_count--;}
static struct balance_callback *splice_balance_callbacks(struct rq *r) {assert(rq_held);return NULL;}
static void balance_callbacks(struct rq *r,struct balance_callback *p) {assert(!rq_held && preempt_count);}
'''
# The actual native provider is compiled with tiny host lock/list primitives.
# One hash bucket exercises key comparisons, not kernel hashing/IRQ behavior.
provider_primitives = r'''
struct hlist_node {struct hlist_node *next, **pprev;};
struct hlist_head {struct hlist_node *first;};
#define DEFINE_HASHTABLE(n,b) struct hlist_head n = {0}
static void test_hash_add(struct hlist_head *h,struct hlist_node *n) {
    n->next=h->first;if(n->next)n->next->pprev=&n->next;n->pprev=&h->first;h->first=n;
}
#define hash_add(h,n,k) test_hash_add(&(h),n)
static void hash_del(struct hlist_node *n) {
    assert(n->pprev);*n->pprev=n->next;if(n->next)n->next->pprev=n->pprev;n->next=NULL;n->pprev=NULL;
}
static bool hlist_unhashed(struct hlist_node *n) {return !n->pprev;}
#define hash_for_each_possible(h,e,m,k) \
    for(struct hlist_node *n=(h).first;n && ((e)=(void *)((char *)n - offsetof(__typeof__(*(e)),m)),true);n=n->next)
#define DEFINE_RAW_SPINLOCK(n) pthread_mutex_t n=PTHREAD_MUTEX_INITIALIZER
static _Thread_local bool kernel_atomic;
#define raw_spin_lock_irqsave(p,f) do {assert(!kernel_atomic);pthread_mutex_lock(p);kernel_atomic=true;(f)=0;} while(0)
#define raw_spin_unlock_irqrestore(p,f) do {assert(kernel_atomic);kernel_atomic=false;pthread_mutex_unlock(p);(void)(f);} while(0)
#undef lockdep_assert_held
#define lockdep_assert_held(p) assert(_Generic((p),bool *: *(bool *)(p),default: kernel_atomic))
static bool fail_lease_alloc;
static int fail_alloc_after=-1;
static void *kzalloc(size_t n,int g) {
    assert(!kernel_atomic && !rq_held);
    if(fail_lease_alloc || fail_alloc_after==0)return NULL;
    if(fail_alloc_after>0)fail_alloc_after--;
    return calloc(1,n);
}
static void kfree(void *p) {assert(!kernel_atomic && !rq_held);free(p);}
'''
provider = re.sub(r'^#include[^\n]*\n', '',
    (root/'integration/target-c/provider/mtk_perf_task_ctrl_impl.inc').read_text(), flags=re.M)
def leaf(text, name):
    m = re.search(r'^(?:static )?(?:void|int|bool) '+name+r'\([^)]*\)\n\{',text,re.M)
    assert m,name
    i=m.end(); depth=1
    while depth:
        depth+=(text[i]=='{')-(text[i]=='}');i+=1
    return text[m.start():i]
core_source = (a.scheduler_fixture/'core.c').read_text()
assert '\tsched_uclamp_lease_preempt(p);\n\n\tif (!(attr->sched_flags & SCHED_FLAG_KEEP_PARAMS))' in core_source
core_primitives = '\n'.join(leaf(core_source,n) for n in ['__uclamp_update_util_min_rt_default','uclamp_validate','uclamp_reset','__setscheduler_uclamp'])
core_provider = re.sub(r'^#include[^\n]*\n','',
    (root/'integration/target-c/scheduler/uclamp_lease.inc').read_text(),flags=re.M)
# Only generic dispatch/queue primitives are modeled here; the actual core
# validation, clamp mutation, registry, owned transaction and restore are C.
legacy_scheduler = r'''
static int legacy_clamp(struct task_struct *p,unsigned int lo,unsigned int hi,unsigned int mask) {
    struct sched_attr a={mask,lo,hi};struct rq_flags f;
    int ret=uclamp_validate(p,&a,false);if(ret)return ret;
    get_task_struct(p);struct rq *r=task_rq_lock(p,&f);
    bool q=p->queued,run=p->running;
    if(q)dequeue_task(r,p,DEQUEUE_SAVE|DEQUEUE_NOCLOCK);
    if(run)put_prev_task(r,p);
    sched_uclamp_lease_preempt(p);
    __setscheduler_uclamp(p,&a);
    if(q)enqueue_task(r,p,DEQUEUE_SAVE|DEQUEUE_NOCLOCK);
    if(run)set_next_task(r,p);
    task_rq_unlock(r,p,&f);put_task_struct(p);return 0;
}
'''
legacy = ''
if a.native_fixture:
    vip = (a.native_fixture/'vip.c').read_text()
    nl = (a.native_fixture/'nonlinear_opp_cap.c').read_text()
    legacy = '\n'.join([leaf(vip,'set_task_basic_vip'),leaf(vip,'unset_task_basic_vip'),leaf(nl,'set_curr_uclamp_hint')])
else:
    legacy = 'static void set_task_basic_vip(int n) {mtk_perf_task_legacy_vip(lookup,true);}\nstatic int set_curr_uclamp_hint(int n,int v) {mtk_perf_task_legacy_hint(lookup,v);return 0;}\n'
suffix = r'''
static void *owned_hint_writer(void *p) {
    for(int i=0;i<10000;i++) {
        int r=mtk_perf_task_set_hint(p,1);assert(!r || r==-ESTALE);
    }
    return NULL;
}
static void *legacy_vip_writer(void *p) {set_task_basic_vip(71);return NULL;}
static void preempt_hint_during_apply(struct task_struct *p) {mtk_perf_task_legacy_hint(p,2);}
static void *owned_clamp_writer(void *p) {
    for(int i=0;i<10000;i++) {
        int r=sched_uclamp_lease_apply(p,400,700);assert(!r || r==-ESTALE);
    }
    return NULL;
}
static void *legacy_clamp_writer(void *p) {
    assert(!legacy_clamp(p,400,700,SCHED_FLAG_UTIL_CLAMP));return NULL;
}
static void test_scheduler_ownership(void) {
    struct pid id={1,39};
    struct task_struct p={.refs=1,.alive=true,.identity=&id,
        .uclamp_req={{100,true},{800,true}},.queued=true,.running=true};
    struct sched_uclamp_lease *a=NULL,*b=NULL;
    struct rq_flags f;struct rq *r;
    assert(sched_uclamp_lease_acquire(&p,NULL)==-EINVAL);
    rq_model.stop=&p;
    assert(sched_uclamp_lease_acquire(&p,&a)==-EPERM && p.refs==1 && !a);
    rq_model.stop=NULL;
    assert(!sched_uclamp_lease_acquire(&p,&a) && p.refs==2);
    assert(sched_uclamp_lease_acquire(&p,&b)==-EBUSY && !b && p.refs==2);
    assert(sched_uclamp_lease_apply(a,800,700)==-EINVAL && !a->applied);
    assert(!sched_uclamp_lease_apply(a,400,700));
    attr_error=-EPERM;
    assert(legacy_clamp(&p,200,900,SCHED_FLAG_UTIL_CLAMP)==-EPERM);
    attr_error=0;
    assert(sched_uclamp_lease_is_live(a) && p.uclamp_req[0].value==400);
    assert(!legacy_clamp(&p,400,700,SCHED_FLAG_UTIL_CLAMP)); /* same value */
    assert(!sched_uclamp_lease_is_live(a));
    assert(sched_uclamp_lease_apply(a,600,800)==-ESTALE);
    assert(!sched_uclamp_lease_acquire(&p,&b));
    assert(!sched_uclamp_lease_apply(b,450,650));
    sched_uclamp_lease_release(a); /* old descriptor cannot detach/restore new */
    assert(p.uclamp_req[1].value==650 && sched_uclamp_lease_is_live(b));
    sched_uclamp_lease_release(b);
    assert(p.refs==1 && p.uclamp_req[0].value==400 && p.uclamp_req[1].value==700);
    p.uclamp_req[0]=(struct uclamp){100,true};
    p.uclamp_req[1]=(struct uclamp){800,true};
    assert(!sched_uclamp_lease_acquire(&p,&a));
    assert(!sched_uclamp_lease_apply(a,400,700));
    assert(!legacy_clamp(&p,350,0,SCHED_FLAG_UTIL_CLAMP_MIN));
    sched_uclamp_lease_release(a);
    assert(p.uclamp_req[0].value==350 && p.uclamp_req[1].value==800); /* baseline max, not overlay */
    /* A reserved but unapplied lease snapshots the latest RT default at first
       apply, and restores current defaults rather than a cached numeric value. */
    p.is_rt=true;p.uclamp_req[0]=(struct uclamp){0,false};
    p.uclamp_req[1]=(struct uclamp){1024,false};
    assert(!sched_uclamp_lease_acquire(&p,&a));
    sysctl_sched_uclamp_util_min_rt_default=600;
    r=task_rq_lock(&p,&f);__uclamp_update_util_min_rt_default(&p);task_rq_unlock(r,&p,&f);
    assert(!sched_uclamp_lease_apply(a,300,900));
    sysctl_sched_uclamp_util_min_rt_default=550;
    r=task_rq_lock(&p,&f);__uclamp_update_util_min_rt_default(&p);task_rq_unlock(r,&p,&f);
    assert(p.uclamp_req[0].value==300);
    sched_uclamp_lease_release(a);
    assert(p.is_rt && p.uclamp_req[0].value==550 && !p.uclamp_req[0].user_defined);
    assert(p.uclamp_req[1].value==1024 && !p.uclamp_req[1].user_defined);
    p.is_rt=false;
    assert(!sched_uclamp_lease_acquire(&p,&a));
    pthread_t own,other;
    assert(!pthread_create(&own,NULL,owned_clamp_writer,a));
    assert(!pthread_create(&other,NULL,legacy_clamp_writer,&p));
    assert(!pthread_join(own,NULL) && !pthread_join(other,NULL));
    sched_uclamp_lease_release(a);
    assert(p.uclamp_req[0].value==400 && p.uclamp_req[1].value==700 && p.refs==1);
    assert(!sched_uclamp_lease_acquire(&p,&a));
    assert(!sched_uclamp_lease_apply(a,300,600));
    int calls=attr_calls;p.alive=false;
    sched_uclamp_lease_release(a);
    assert(attr_calls==calls && p.refs==1 && p.queued && p.running);
    sysctl_sched_uclamp_util_min_rt_default=1024;
}
int main(void) {
    test_scheduler_ownership();
    struct pid first={1,71}, reused={1,71};
    struct task_struct old={.refs=1,.alive=true,.identity=&first,
        .uclamp_req={{100,true},{800,true}},.android_vendor_data1={{{3},{false}}},.queued=true,.running=true};
    struct task_struct newer={.refs=1,.alive=true,.identity=&reused};
    struct c2ps_task_info info={.mlock=true,.is_vip_task=true};
    struct regulator_req req={.tsk_info=&info};
    lookup=&old;
    struct task_struct *admitted=c2ps_get_task(71);
    assert(admitted==&old && old.refs==2);
    old.flags=PF_KTHREAD;
    assert(c2ps_task_begin_locked(&info,admitted)==-EPERM);
    old.flags=0;fail_lease_alloc=true;
    assert(c2ps_task_begin_locked(&info,admitted)==-ENOMEM && !info.identity && first.refs==1 && old.refs==2);
    fail_lease_alloc=false;fail_alloc_after=1;
    assert(c2ps_task_begin_locked(&info,admitted)==-ENOMEM && !info.identity && first.refs==1 && old.refs==2);
    fail_alloc_after=-1;
    assert(c2ps_task_begin_locked(&info,admitted)==0);
    put_task_struct(admitted);
    assert(old.refs==4 && first.refs==2); /* frame + native + scheduler references */
    assert(c2ps_task_begin_locked(&info,&old)==-EALREADY);
    struct c2ps_task_info other={.mlock=true,.is_dynamic_tid=true};
    assert(c2ps_task_begin_locked(&other,&old)==-EBUSY && !other.identity && old.refs==4);
    req.frame_seq=info.frame_seq;
    assert(c2ps_apply_task_clamp(&req,700,400)==0);
    assert(old.uclamp_req[0].value==400 && old.uclamp_req[1].value==700);
    c2ps_task_release_locked(&info);
    assert(old.refs==1 && old.uclamp_req[0].value==100 && old.uclamp_req[1].value==800);
    assert(old.android_vendor_data1[0].cu_hint.hint==3);
    assert(!old.android_vendor_data1[0].vip_task.basic_vip);
    assert(c2ps_apply_task_clamp(&req,900,500)==-ESRCH);
    assert(c2ps_task_begin_locked(&info,&old)==0);
    req.frame_seq=info.frame_seq;
    attr_error=-EINVAL;
    assert(c2ps_apply_task_clamp(&req,700,400)==-EINVAL && !info.uclamp_lease->applied);
    attr_error=0;
    assert(c2ps_apply_task_clamp(&req,700,400)==0);
    assert(!legacy_clamp(&old,400,750,SCHED_FLAG_UTIL_CLAMP));
    assert(c2ps_apply_task_clamp(&req,900,500)==-ESTALE);
    assert(!info.is_active && !info.native_task && !info.uclamp_lease && old.refs==1);
    c2ps_task_release_locked(&info);
    assert(old.uclamp_req[1].value==750 && old.refs==1);
    assert(c2ps_task_begin_locked(&info,&old)==0);
    req.frame_seq=info.frame_seq;
    set_task_basic_vip(71); /* actual same-value legacy entrypoint when provided */
    assert(c2ps_apply_task_clamp(&req,700,400)==-ESTALE);
    assert(!info.is_active && !info.native_task && !info.uclamp_lease && old.refs==1);
    assert(!c2ps_task_begin_locked(&other,&old)); /* split resource cleanup immediate */
    c2ps_task_release_locked(&info);
    assert(mtk_perf_task_is_live(other.native_task));
    c2ps_task_release_locked(&other);put_pid(other.identity);other.identity=NULL;
    assert(old.android_vendor_data1[0].vip_task.basic_vip && old.refs==1);
    assert(c2ps_task_begin_locked(&info,&old)==0);
    req.frame_seq=info.frame_seq;on_clamp_set=preempt_hint_during_apply;
    assert(c2ps_apply_task_clamp(&req,700,400)==-ESTALE);
    assert(!info.is_active && old.refs==1 && !info.native_task && !info.uclamp_lease);
    assert(old.android_vendor_data1[0].cu_hint.hint==2 && old.uclamp_req[1].value==750);
    assert(c2ps_task_begin_locked(&info,&old)==0);
    req.frame_seq=info.frame_seq;
    assert(!c2ps_apply_task_clamp(&req,700,400));
    assert(!set_curr_uclamp_hint(71,1));
    c2ps_task_release_locked(&info);
    assert(old.android_vendor_data1[0].cu_hint.hint==1 && old.refs==1);
    old.android_vendor_data1[0].vip_task.basic_vip=false;
    old.android_vendor_data1[0].cu_hint.hint=3;
    assert(c2ps_task_begin_locked(&info,&old)==0);
    pthread_t a,b;
    pthread_create(&a,NULL,owned_hint_writer,info.native_task);
    pthread_create(&b,NULL,legacy_vip_writer,NULL);
    pthread_join(a,NULL);pthread_join(b,NULL);
    assert(!mtk_perf_task_is_live(info.native_task));
    c2ps_task_release_locked(&info);
    assert(old.refs==1 && old.android_vendor_data1[0].vip_task.basic_vip);
    old.android_vendor_data1[0].vip_task.basic_vip=false;
    assert(c2ps_task_begin_locked(&info,&old)==0);
    req.frame_seq=info.frame_seq;
    old.alive=false; lookup=&newer;
    int calls=attr_calls;
    assert(c2ps_apply_task_clamp(&req,700,400)==-ESRCH && attr_calls==calls);
    c2ps_task_release_locked(&info);
    assert(old.refs==1 && newer.refs==1);
    assert(c2ps_task_begin_locked(&info,&newer)==-ESTALE);
    info.is_dynamic_tid=true;
    assert(c2ps_task_begin_locked(&info,&newer)==0);
    assert(c2ps_apply_task_clamp(&req,900,500)==-ESRCH); /* old frame */
    req.frame_seq=info.frame_seq;
    assert(c2ps_apply_task_clamp(&req,700,400)==0);
    c2ps_task_release_locked(&info);
    assert(newer.uclamp_req[0].value==0 && !newer.uclamp_req[0].user_defined);
    assert(newer.uclamp_req[1].value==1024 && !newer.uclamp_req[1].user_defined);
    put_pid(info.identity);
    assert(first.refs==1 && reused.refs==1 && old.refs==1 && newer.refs==1);
    puts("PASS: actual C2PS adapter, native VIP/hint leases AND scheduler clamp leases; real core validation/mutation, modeled generic dispatcher/queue locks, not kernel runtime proof");
}
'''
if a.vip_disabled:
    prefix = prefix.replace('#define CONFIG_MTK_SCHED_VIP_TASK 1', '#define CONFIG_MTK_SCHED_VIP_TASK 0')
    prefix = prefix.replace(' struct { bool basic_vip; } vip_task;', '')
    # vip.c is not a selected TU in this configuration. Keep only the actual
    # nonlinear hint entrypoint; no dummy VIP API is supplied to make it link.
    legacy = leaf((a.native_fixture/'nonlinear_opp_cap.c').read_text(), 'set_curr_uclamp_hint') if a.native_fixture else ''
    suffix = r'''
int main(void) {
    struct pid id={1,71};
    struct task_struct task={.refs=1,.alive=true,.identity=&id,
        .uclamp_req={{100,true},{800,true}},.android_vendor_data1={{{3}}}};
    struct c2ps_task_info info={.mlock=true,.is_vip_task=true};
    struct regulator_req req={.tsk_info=&info};
    struct mtk_perf_task_lease *native=NULL;
    lookup=&task;
    assert(mtk_perf_task_acquire(&task,true,&native)==-EOPNOTSUPP && !native && task.refs==1);
    assert(c2ps_task_begin_locked(&info,&task)==-EOPNOTSUPP);
    assert(!info.task && !info.identity && !info.is_active && task.refs==1 && id.refs==1);
    info.is_vip_task=false;
    assert(!c2ps_task_begin_locked(&info,&task));
    req.frame_seq=info.frame_seq;
    assert(!c2ps_apply_task_clamp(&req,700,400));
    c2ps_task_release_locked(&info);put_pid(info.identity);
    assert(task.uclamp_req[0].value==100 && task.uclamp_req[1].value==800);
    assert(task.android_vendor_data1[0].cu_hint.hint==3 && task.refs==1 && id.refs==1);
    puts("PASS CONFIG_MTK_SCHED_VIP_TASK=0: no VIP storage/access/stub; unsupported VIP request unwinds, native hint/clamp frame works");
}
'''
c = a.output / 'task.c'
c.write_text(prefix + provider_primitives + provider + legacy + core_primitives +
    core_provider + legacy_scheduler +
    '\n#undef lockdep_assert_held\n#define lockdep_assert_held(p) assert(*(p))\n'
    'void c2ps_task_release_locked(struct c2ps_task_info *info);\n' + body + suffix)
out = a.output / 'task'
subprocess.run([os.environ.get('CC','clang'), '-std=gnu11','-O1','-g',
    '-fsanitize=address,undefined','-pthread',str(c),'-o',str(out)],check=True)
subprocess.run([str(out)],check=True)
(a.output/'SOURCE.sha256').write_text(hashlib.sha256(production.encode()).hexdigest()+'\n')
(a.output/'NATIVE-SOURCE.sha256').write_text(hashlib.sha256(provider.encode()).hexdigest()+'\n')
(a.output/'SCHEDULER-SOURCE.sha256').write_text(hashlib.sha256((core_primitives+core_provider).encode()).hexdigest()+'\n')
