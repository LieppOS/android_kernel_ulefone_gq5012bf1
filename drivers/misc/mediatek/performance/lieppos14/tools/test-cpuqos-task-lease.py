#!/usr/bin/env python3
"""Actual CPUQoS/group providers under modeled locks/tasks/IPIs/rq; NOT kernel proof."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess

root = Path(__file__).resolve().parents[1]
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('output', type=Path)
ap.add_argument('--group', action='store_true', help='also compile actual group owner with modeled rq helpers')
a = ap.parse_args()
a.output.mkdir(exist_ok=False)
provider = root/'integration/target-c/provider/mtk_perf_cpuqos_ctrl_impl.inc'
impl = re.sub(r'^#include[^\n]*\n', '', provider.read_text(), flags=re.M)
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <pthread.h>
#define IS_ENABLED(x) (x)
#define GFP_KERNEL 0
#define PF_KTHREAD 1
#define PF_EXITING 2
#define GROUP_RANK 0
#define TASK_RANK 1
#define PD9 9
#define DISABLE 3
#define EXPORT_SYMBOL_GPL(x)
#define WRITE_ONCE(a,b) __atomic_store_n(&(a),(b),__ATOMIC_RELAXED)
#define READ_ONCE(a) __atomic_load_n(&(a),__ATOMIC_RELAXED)
#define DEFINE_RAW_SPINLOCK(n) pthread_mutex_t n = PTHREAD_MUTEX_INITIALIZER
static _Thread_local int held;
#define raw_spin_lock_irqsave(p,f) do { (f)=0; assert(!held); pthread_mutex_lock(p); held=1; } while (0)
#define raw_spin_unlock_irqrestore(p,f) do { (void)(f); assert(held); held=0; pthread_mutex_unlock(p); } while (0)
#define lockdep_assert_held(p) assert(held)
static void might_sleep(void) { assert(!held); }
static bool fail_alloc;
static int alloc_n,fail_n=-1;
static void *kzalloc(size_t n, int f) { assert(!held); alloc_n++;return (fail_alloc || alloc_n==fail_n) ? NULL : calloc(1,n); }
static void kfree(void *p) { assert(!held); free(p); }
struct hlist_node {struct hlist_node *next, **pprev;};
struct hlist_head {struct hlist_node *first;};
#define DEFINE_HASHTABLE(n,b) struct hlist_head n = {0}
static void test_hash_add(struct hlist_head *h,struct hlist_node *n) {
    assert(!n->pprev); n->next=h->first;
    if(h->first) h->first->pprev=&n->next;
    h->first=n; n->pprev=&h->first;
}
#define hash_add(h,n,k) test_hash_add(&(h),n)
static void hash_del(struct hlist_node *n) {
    assert(n->pprev); *n->pprev=n->next;
    if(n->next) n->next->pprev=n->pprev;
    n->next=NULL; n->pprev=NULL;
}
static bool hlist_unhashed(struct hlist_node *n) {return !n->pprev;}
#define hash_for_each_possible(h,e,m,k) \
    for(struct hlist_node *n=(h).first;n && ((e)=(void *)((char *)n - offsetof(__typeof__(*(e)),m)),true);n=n->next)
struct cpuqos_task_struct {unsigned int pd,rank;};
struct grp {int id;};
struct gp_task_struct {struct grp *grp;bool customized;};
struct mtk_task {struct cpuqos_task_struct cpuqos_task;struct gp_task_struct gp_task;};
struct task_struct {int pid,refs; bool alive; int exit_state,flags; pthread_mutex_t pi_lock; struct mtk_task android_vendor_data1[1];};
static bool pid_alive(struct task_struct *p) {return p->alive;}
static void get_task_struct(struct task_struct *p) {__atomic_add_fetch(&p->refs,1,__ATOMIC_RELAXED);}
static void put_task_struct(struct task_struct *p) {assert(!held);assert(__atomic_sub_fetch(&p->refs,1,__ATOMIC_RELAXED)>=1);}
static struct task_struct *numeric;
static struct task_struct *find_task_by_vpid(int pid) {return numeric && numeric->pid==pid ? numeric : NULL;}
struct cgroup_subsys_state {int id;};
static struct cgroup_subsys_state css={17};
static int plat_enable=1,cpuqos_perf_mode=1,cpuqos_subsys_id;
static _Thread_local int rcu_held;
static void rcu_read_lock(void) {rcu_held++;}
static void rcu_read_unlock(void) {assert(rcu_held-- > 0);}
static struct cgroup_subsys_state *task_css(struct task_struct *p,int id) {assert(rcu_held);return &css;}
static unsigned int get_task_rank(struct task_struct *p) {return READ_ONCE(p->android_vendor_data1->cpuqos_task.rank);}
static int cpuqos_v3_map_task_pd(struct task_struct *p) {
    if(cpuqos_perf_mode==DISABLE)return 0;
    return get_task_rank(p)==TASK_RANK ? READ_ONCE(p->android_vendor_data1->cpuqos_task.pd) : 3;
}
static void trace_cpuqos_set_task_pd(int pid,int cssid,int pd,int old,int new,int rank,int mode) {assert(cssid==17);}
static pthread_mutex_t notify_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t notify_cond=PTHREAD_COND_INITIALIZER;
static _Thread_local bool pause_notify;
static bool reached,proceed;
static int hardware;
static void cpuqos_v3_kick_task(struct task_struct *p,int pd) {
    assert(!held && pd==-1); /* no stale cached PD write-back permitted */
    if(pause_notify){
        pthread_mutex_lock(&notify_lock);reached=true;pthread_cond_broadcast(&notify_cond);
        while(!proceed)pthread_cond_wait(&notify_cond,&notify_lock);
        pthread_mutex_unlock(&notify_lock);
    }
    WRITE_ONCE(hardware,cpuqos_v3_map_task_pd(p));
}
static struct task_struct task={.pid=42,.refs=1,.alive=true,.pi_lock=PTHREAD_MUTEX_INITIALIZER};
static void reset(void) {
    assert(task.refs==1);task.alive=true;task.flags=task.exit_state=0;
    task.android_vendor_data1->cpuqos_task.pd=5;
    task.android_vendor_data1->cpuqos_task.rank=TASK_RANK;
    numeric=&task;hardware=5;cpuqos_perf_mode=1;plat_enable=1;
}
'''
suffix = r'''
#if IS_ENABLED(CONFIG_MTK_PERF_CTRL_LEASE)
static void *delayed_writer(void *unused) {pause_notify=true;assert(!cpuqos_task_pd_set(&task,6));return NULL;}
static void *ordinary_writer(void *unused) {for(int i=0;i<10000;i++)assert(!cpuqos_task_pd_set(&task,8));return NULL;}
#endif
int main(void) {
    reset();assert(!cpuqos_task_pd_set(&task,7));assert(hardware==7);
    assert(!set_task_pd(42,-10));assert(hardware==3);
    assert(set_task_pd(0,5)==-1);
    assert(cpuqos_task_pd_set(&task,10)==-EINVAL);
#if IS_ENABLED(CONFIG_MTK_PERF_CTRL_LEASE)
    struct mtk_cpuqos_task_lease *p=NULL,*q=NULL;
    reset();fail_alloc=true;
    assert(mtk_cpuqos_task_acquire(&task,7,&p)==-ENOMEM && !p);
    assert(hardware==5 && task.refs==1);fail_alloc=false;
    assert(!mtk_cpuqos_task_acquire(&task,7,&p));
    assert(mtk_cpuqos_task_acquire(&task,8,&q)==-EBUSY && !q);
    assert(task.refs==2 && mtk_cpuqos_task_is_live(p));
    assert(cpuqos_task_pd_set(&task,10)==-EINVAL && mtk_cpuqos_task_is_live(p));
    mtk_cpuqos_task_release(p);assert(hardware==5 && task.refs==1);
    reset();assert(!cpuqos_task_pd_set(&task,-1));
    assert(!mtk_cpuqos_task_acquire(&task,7,&p));mtk_cpuqos_task_release(p);
    assert(get_task_rank(&task)==GROUP_RANK && hardware==3);
    reset();assert(!mtk_cpuqos_task_acquire(&task,7,&p));
    assert(!cpuqos_task_pd_set(&task,7)); /* same-value ordinary write revokes */
    assert(!mtk_cpuqos_task_is_live(p));assert(!mtk_cpuqos_task_acquire(&task,8,&q));
    mtk_cpuqos_task_release(p);assert(mtk_cpuqos_task_is_live(q) && hardware==8);
    mtk_cpuqos_task_release(q);assert(hardware==7);
    reset();assert(!mtk_cpuqos_task_acquire(&task,7,&p));
    pthread_t a,b;pthread_create(&a,NULL,ordinary_writer,NULL);pthread_create(&b,NULL,ordinary_writer,NULL);
    pthread_join(a,NULL);pthread_join(b,NULL);
    assert(!mtk_cpuqos_task_is_live(p));mtk_cpuqos_task_release(p);assert(hardware==8);
    reset();reached=proceed=false;
    pthread_create(&a,NULL,delayed_writer,NULL);
    pthread_mutex_lock(&notify_lock);while(!reached)pthread_cond_wait(&notify_cond,&notify_lock);pthread_mutex_unlock(&notify_lock);
    assert(!mtk_cpuqos_task_acquire(&task,8,&p));assert(hardware==8);
    pthread_mutex_lock(&notify_lock);proceed=true;pthread_cond_broadcast(&notify_cond);pthread_mutex_unlock(&notify_lock);
    pthread_join(a,NULL);assert(hardware==8 && mtk_cpuqos_task_is_live(p));
    mtk_cpuqos_task_release(p);assert(hardware==6);
    reset();assert(!mtk_cpuqos_task_acquire(&task,7,&p));
    struct task_struct replacement={.pid=42,.refs=1,.alive=true};
    replacement.android_vendor_data1->cpuqos_task.pd=9;
    replacement.android_vendor_data1->cpuqos_task.rank=TASK_RANK;
    task.alive=false;numeric=&replacement;
    assert(cpuqos_task_pd_set(&task,8)==-ESRCH);
    assert(!mtk_cpuqos_task_is_live(p));mtk_cpuqos_task_release(p);
    assert(cpuqos_v3_map_task_pd(&replacement)==9 && task.refs==1 && replacement.refs==1);
    reset();assert(!mtk_cpuqos_task_acquire(&task,7,&p));
    cpuqos_perf_mode=DISABLE;assert(!mtk_cpuqos_task_is_live(p));
    assert(cpuqos_task_pd_set(&task,8)==-EOPNOTSUPP);mtk_cpuqos_task_release(p);
    assert(task.android_vendor_data1->cpuqos_task.pd==5 && task.refs==1);
    reset();task.exit_state=1;assert(mtk_cpuqos_task_acquire(&task,7,&p)==-ESRCH && !p);
    reset();task.flags=PF_EXITING;assert(mtk_cpuqos_task_acquire(&task,7,&p)==-ESRCH && !p);
    task.flags=0;
#endif
    puts("PASS: actual typed CPUQoS provider, refs/unwind/preemption/reuse/delayed notification; modeled kernel primitives");
}
'''
group_provider=root/'integration/target-c/provider/mtk_perf_group_ctrl_impl.inc'
group_impl=re.sub(r'^#include[^\n]*\n', '', group_provider.read_text(), flags=re.M) if a.group else ''
group_shim=r'''
#define GROUP_ID_RECORD_MAX 4
#define FLT_GROUP_START_IDX 6
#define GP_MODE_0 0
static pthread_mutex_t related_thread_group_lock=PTHREAD_MUTEX_INITIALIZER;
static void write_lock(pthread_mutex_t *p) {assert(held==1);pthread_mutex_lock(p);held++;}
static void write_unlock(pthread_mutex_t *p) {assert(held==2);held--;pthread_mutex_unlock(p);}
#define read_lock write_lock
#define read_unlock write_unlock
static int group_mode=1,current_cgrp;
static struct grp groups[4]={{0},{1},{2},{3}};
static unsigned int group_get_mode(void) {return group_mode;}
static struct grp *lookup_grp(int id) {return id>=0 && id<4 ? &groups[id] : NULL;}
static int cgrp_to_grpid(struct task_struct *p) {assert(held==2);return current_cgrp;}
static void remove_task_from_group(struct task_struct *p) {assert(held==2);p->android_vendor_data1->gp_task.grp=NULL;}
static int add_task_to_group(struct task_struct *p,struct grp *grp) {assert(held==2 && grp);p->android_vendor_data1->gp_task.grp=grp;return 0;}
struct mtk_group_task_lease;
void mtk_group_task_release(struct mtk_group_task_lease *lease);
''' if a.group else ''
group_tests=r'''
#if IS_ENABLED(CONFIG_MTK_PERF_CTRL_LEASE)
static int gid(void) {struct grp *g=task.android_vendor_data1->gp_task.grp;return g ? g->id : -1;}
static void greset(void) {reset();group_mode=1;current_cgrp=0;task.android_vendor_data1->gp_task.grp=&groups[0];task.android_vendor_data1->gp_task.customized=false;}
static void *take_racing_group(void *unused) {
    struct mtk_group_task_lease *g=NULL;pause_notify=true;
    assert(mtk_group_task_acquire(&task,1,&g)==-ESTALE && !g);return NULL;
}
static void test_group(void) {
    struct mtk_group_task_lease *g=NULL,*h=NULL;
    greset();fail_n=alloc_n+2; /* CPUQoS allocation after group descriptor */
    assert(mtk_group_task_acquire(&task,1,&g)==-ENOMEM && !g);
    assert(task.refs==1 && gid()==0 && hardware==5);fail_n=-1;
    assert(!mtk_group_task_acquire(&task,1,&g));
    assert(task.refs==3 && gid()==1 && hardware==7 && mtk_group_task_is_live(g));
    assert(mtk_group_task_acquire(&task,1,&h)==-EBUSY && !h);
    /* A wake/cgroup callback already passed its lockless fast-path before
     * acquisition. Its delayed automatic write must not revoke this owner.
     */
    assert(!mtk_group_auto_set(&task,2));
    assert(gid()==1 && hardware==7 && mtk_group_task_is_live(g));
    current_cgrp=-1;mtk_group_task_release(g);
    assert(task.refs==1 && gid()==-1 && hardware==5 && !task.android_vendor_data1->gp_task.customized);
    assert(!mtk_group_auto_set(&task,2) && gid()==2);
    group_mode=0;assert(!mtk_group_auto_set(&task,-1) && gid()==2);
    group_mode=1;
    assert(mtk_group_auto_set(&task,GROUP_ID_RECORD_MAX)==-EINVAL);
    greset();task.android_vendor_data1->gp_task.customized=true;task.android_vendor_data1->gp_task.grp=&groups[3];
    assert(!mtk_group_task_acquire(&task,1,&g));mtk_group_task_release(g);
    assert(gid()==3 && task.android_vendor_data1->gp_task.customized);
    greset();assert(!mtk_group_task_acquire(&task,1,&g));
    assert(!mtk_group_custom_set(&task,1));assert(!cpuqos_task_pd_set(&task,7));
    assert(!mtk_group_task_is_live(g));mtk_group_task_release(g);
    assert(gid()==1 && hardware==7 && task.android_vendor_data1->gp_task.customized);
    /* ABA: an old release cannot detach or reset a newer task/group/PD owner. */
    assert(!mtk_group_task_acquire(&task,2,&g));assert(!__sched_set_grp_id(&task,-1));
    assert(!cpuqos_task_pd_set(&task,-1));assert(!mtk_group_task_acquire(&task,3,&h));
    mtk_group_task_release(g);assert(gid()==3 && hardware==9 && mtk_group_task_is_live(h));
    mtk_group_task_release(h);assert(gid()==-1 && hardware==3);
    greset();assert(!mtk_group_task_acquire(&task,1,&g));
    assert(!cpuqos_task_pd_set(&task,8));assert(!mtk_group_task_is_live(g));
    mtk_group_task_release(g);assert(gid()==0 && hardware==8);
    greset();reached=proceed=false;pthread_t thread;
    pthread_create(&thread,NULL,take_racing_group,NULL);
    pthread_mutex_lock(&notify_lock);while(!reached)pthread_cond_wait(&notify_cond,&notify_lock);pthread_mutex_unlock(&notify_lock);
    assert(!cpuqos_task_pd_set(&task,8));
    pthread_mutex_lock(&notify_lock);proceed=true;pthread_cond_broadcast(&notify_cond);pthread_mutex_unlock(&notify_lock);
    pthread_join(thread,NULL);assert(gid()==0 && hardware==8 && task.refs==1);
    greset();assert(!mtk_group_task_acquire(&task,1,&g));
    task.alive=false;assert(!mtk_group_task_is_live(g));mtk_group_task_release(g);assert(task.refs==1);
    greset();assert(!mtk_group_task_acquire(&task,1,&g));group_mode=0;
    assert(!mtk_group_task_is_live(g));mtk_group_task_release(g);assert(task.refs==1);
    puts("PASS: actual group+CPUQoS ownership/unwind/ordinary-write/ABA/current-cgroup/raced-PD; modeled rq");
}
#endif
''' if a.group else ''
if a.group:
    suffix=suffix.replace('int main(void) {',group_tests+'\nint main(void) {')
    suffix=suffix.replace('    puts("PASS: actual typed CPUQoS', '#if IS_ENABLED(CONFIG_MTK_PERF_CTRL_LEASE)\n    test_group();\n#endif\n    puts("PASS: actual typed CPUQoS')
results=[]
for enabled in [1, 0]:
    c=a.output/f'cpuqos-{enabled}.c'
    c.write_text(f'#define CONFIG_MTK_PERF_CTRL_LEASE {enabled}\n'+prefix+impl+group_shim+group_impl+suffix)
    exe=c.with_suffix('')
    subprocess.run([os.environ.get('CC','clang'),'-std=gnu11','-O1','-g','-pthread',
                    '-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
    results.append(dict(lease_config=enabled,result='PASS'))
(a.output/'RESULT.json').write_text(json.dumps(dict(scope=__doc__,
    provider_sha256=hashlib.sha256(provider.read_bytes()).hexdigest(),
    group_provider_sha256=hashlib.sha256(group_provider.read_bytes()).hexdigest() if a.group else None,checks=results,
    native_compile=False,native_irq_verification=False),indent=2)+'\n')
