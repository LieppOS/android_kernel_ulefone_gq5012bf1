#!/usr/bin/env python3
"""Exercise actual FBT task helper/error/encoding and XGF first insertion bodies.
Host primitives, NOT complete PID-identity pipeline or native scheduler proof.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[1]
b = root/'public/drivers/misc/mediatek/performance/fpsgo_v3/fbt/src'
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('output', type=Path)
a = ap.parse_args()
a.output.mkdir(exist_ok=False)

def leaf(text, name):
    m = re.search(r'^(?:static )?(?:bool|int|void|struct (?:xgf_dep|task_struct) \*)\s*'+name+r'\([^)]*\)\n\{',text,re.M)
    assert m,name
    i=m.end();depth=1
    while depth:
        depth+=(text[i]=='{')-(text[i]=='}');i+=1
    return text[m.start():i]

fbt = (b/'fbt_cpu.c').read_text()
xgf = (b/'xgf.c').read_text()
bodies = '\n'.join(leaf(fbt,n) for n in ['fbt_set_nice','fbt_nice_task',
    'fbt_set_task_sched_policy','fbt_change_task_policy','fbt_affinity_task',
    'fbt_set_task_ls'])
bodies += '\n'+'\n'.join(leaf(fbt,n) for n in ['fbt_put_dep_identities',
    'fbt_copy_dep_view','fbt_dep_task_current','fbt_dep_task_get',
    'fbt_reset_task_setting'])
bodies += '\n'+'\n'.join(leaf(xgf,n) for n in ['xgf_admit_dependency_task',
    'xgf_free_dependency','xgf_reset_render_dep_list','xgf_add_dep_task_identity','xgf_add_dep_task',
    'xgf_del_pid2prev_dep','xgf_non_normal_dep_task',
    'fpsgo_comp2xgf_get_dep_list','gbe2xgf_get_dep_list','xgff_get_dep_list',
    'xgf_filter_dep_task','fpsgo_fbt2xgf_get_dep_list'])
bodies += '\n'+'\n'.join(leaf(fbt,n) for n in ['pid_sorted','dep_a_except_b',
    '__cmp1','fbt_get_dep_list','fbt_clear_min_cap'])
prefix = r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#define ERR_PTR(x) ((void *)(intptr_t)(x))
#define MIN_NICE -20
#define MAX_NICE 19
#define SCHED_RR 2
#define SCHED_NORMAL 0
#define SCHED_RESET_ON_FORK 0x40000000
#define FPSGO_TASK_NORMAL 0
#define FPSGO_TASK_RT 1
#define FPSGO_TASK_VIP 2
#define FPSGO_GROUP_HEAVY 2
#define FPSGO_GROUP_SECOND 1
#define FPSGO_GROUP_OTHERS 0
#define XGF_ADD_DEP_GROUPING_VIP 8
#define IS_ENABLED(x) (x)
#define CONFIG_MTK_SCHED_VIP_TASK 1
#define fpsgo_systrace_c_fbt(...) ((void)0)
#define fpsgo_systrace_c_fbt_debug(...) ((void)0)
#define FPSGO_LOGE(...) ((void)0)
#define WARN_ON(x) assert(!(x))
#define GFP_KERNEL 0
struct pid {int refs,nr;};
struct task_struct {int refs,pid,nice,rt_priority,tgid,exit_state;bool vip,vvip,alive,deadline;unsigned long flags;struct pid *identity;};
#define PIDTYPE_PID 0
struct fpsgo_loading {struct pid *identity;int pid,loading,prefer_type,ori_ls,ori_rtprio,policy,ori_nice,ori_vip,action,rmidx,heavyidx,reset_taskmask;};
#define MAX_DEP_NUM 100
struct render_info {int pid,dep_valid_size;unsigned long long buffer_id,raw_runtime;struct {int gcc_enable_by_pid;} attr;struct fpsgo_loading *dep_arr;};
static int max_blc_pid,max_blc_dep_num;
static unsigned long long max_blc_buffer_id;
static struct fpsgo_loading max_blc_dep[MAX_DEP_NUM];
static int dep_alloc_fail_after=-1;
static void *dep_alloc(size_t n,size_t s) {if(dep_alloc_fail_after==0)return NULL;if(dep_alloc_fail_after>0)dep_alloc_fail_after--;return calloc(n,s);}
#define kcalloc(n,s,g) dep_alloc(n,s)
#define fpsgo_alloc_atomic(n) dep_alloc(1,n)
#define kfree(p) free(p)
static void sort(void *p,size_t n,size_t s,int(*cmp)(const void*,const void*),void *swap) {qsort(p,n,s,cmp);}
static void print_dep(const char*f,const char*t,int p,unsigned long long b,struct fpsgo_loading *a,int n) {assert(!n||a);}
static struct pid *get_pid(struct pid *p) {if(p){assert(p->refs>0);p->refs++;}return p;}
static void put_pid(struct pid *p) {if(p)assert(p->refs-->1);}
static int pid_nr(struct pid *p) {return p?p->nr:0;}
static struct task_struct *pid_registry[4];
static int pid_registry_num;
static struct task_struct *pid_task(struct pid *p,int type) {
    for(int i=0;i<pid_registry_num;i++)
        if(pid_registry[i]->identity==p && pid_registry[i]->alive &&
           !pid_registry[i]->exit_state)return pid_registry[i];
    return NULL;
}
#define READ_ONCE(x) (x)
#define PF_KTHREAD 0x200000
#define PF_EXITING 4
static struct pid group_identity={1,7};
static struct pid *task_tgid(struct task_struct *p) {return &group_identity;}
static struct pid *task_pid(struct task_struct *p) {return p->identity;}
static bool pid_alive(struct task_struct *p) {return p->alive;}
static int task_pid_nr(struct task_struct *p) {return p->pid;}
static int task_tgid_nr(struct task_struct *p) {return p->tgid;}
static bool dl_task(struct task_struct *p) {return p->deadline;}
struct rq {int unused;};struct rq_flags {int unused;};
struct sched_param {int sched_priority;};
static struct task_struct *lookup,*expected,*swap_lookup;
static int rcu,rq_locked,fail_sched,sched_calls;
static void rcu_read_lock(void) {assert(!rcu);rcu++;}
static void rcu_read_unlock(void) {assert(rcu);rcu--;if(swap_lookup){lookup=swap_lookup;swap_lookup=NULL;}}
static struct task_struct *find_task_by_vpid(int p) {assert(rcu);return lookup;}
static struct pid *get_task_pid(struct task_struct *t,int type) {return get_pid(t->identity);}
static void get_task_struct(struct task_struct *p) {assert(p->refs);p->refs++;}
static void put_task_struct(struct task_struct *p) {assert(p->refs>1);p->refs--;}
static struct task_struct *get_pid_task(struct pid *id,int type) {struct task_struct *p=pid_task(id,type);if(p)get_task_struct(p);return p;}
static struct rq *task_rq_lock(struct task_struct *p,struct rq_flags *f) {
    static struct rq r;assert(!rcu && !rq_locked && p==expected && p->refs>1);rq_locked++;return &r;
}
static void task_rq_unlock(struct rq *r,struct task_struct *p,struct rq_flags *f) {assert(rq_locked);rq_locked--;}
static int task_nice(struct task_struct *p) {assert(rq_locked);return p->nice;}
static void set_user_nice(struct task_struct *p,int v) {assert(!rcu && !rq_locked && p==expected && p->refs>1);p->nice=v;}
static bool rt_task(struct task_struct *p) {return p->rt_priority>0;}
static int sched_setscheduler(struct task_struct *p,int policy,struct sched_param *s) {
    assert(!rcu && !rq_locked && p==expected && p->refs>1);sched_calls++;
    if(fail_sched)return fail_sched;
    if(s->sched_priority<0 || s->sched_priority>99)return -EINVAL;
    p->rt_priority=s->sched_priority;return 0;
}
/* Native VIP mutation primitives are modeled; caller bookkeeping is real C. */
static int fbt_check_vip_task(struct task_struct *p) {return p->vip;}
static int fbt_check_vvip_task(struct task_struct *p) {return p->vvip;}
static void fbt_set_vip_task(struct task_struct *p,bool vip,bool vvip) {assert(p==expected);p->vip=vip;p->vvip=vvip;}
#define FPSGO_TYPE 0
#define ADPF_TYPE 1
#define KTF_TYPE 2
#define MAGT_TYPE 3
#define XGF_ADD_DEP 0
struct rb_node {struct rb_node *rb_left,*rb_right,*parent;};
struct rb_root {struct rb_node *rb_node;};
struct xgf_dep {struct rb_node rb_node;int tid,action;struct task_struct *task;};
struct xgf_render_if {struct rb_root dep_list,magt_dep_list;int dep_list_size,magt_dep_list_size,tgid,spid,filter_dep_task_enable;};
static void xgf_add_pid2prev_dep(struct xgf_render_if *r,int type,int tid,int action) {}
static void xgf_wspid_list_add2prev(struct xgf_render_if *r) {}

#define RB_EMPTY_ROOT(r) (!(r)->rb_node)
#define rb_entry(n,t,m) ((t *)((char *)(n)-offsetof(t,m)))
static bool xgf_main_lock;
#define mutex_lock(p) do {assert(!*(p));*(p)=true;} while(0)
#define mutex_unlock(p) do {assert(*(p));*(p)=false;} while(0)
static struct xgf_render_if *render_fixture;
static struct xgf_render_if *xgf_get_render_if(int p,unsigned long long b,int c,int t,int f) {return render_fixture;}
struct gbe_runtime {int pid;};
static struct rb_node *rb_first(struct rb_root *r) {struct rb_node *n=r->rb_node;if(n)while(n->rb_left)n=n->rb_left;return n;}
static struct rb_node *rb_next(struct rb_node *n) {
    if(n->rb_right){n=n->rb_right;while(n->rb_left)n=n->rb_left;return n;}
    while(n->parent && n==n->parent->rb_right)n=n->parent;
    return n->parent;
}
static bool fail_alloc;
static void *xgf_alloc(size_t n) {return fail_alloc ? NULL : calloc(1,n);}
static void xgf_free(void *p) {free(p);}
static void rb_erase(struct rb_node *n,struct rb_root *r) {
    assert(!n->rb_left || !n->rb_right);
    struct rb_node *child=n->rb_left ? n->rb_left : n->rb_right;
    if(!n->parent)r->rb_node=child;
    else if(n->parent->rb_left==n)n->parent->rb_left=child;
    else {assert(n->parent->rb_right==n);n->parent->rb_right=child;}
    if(child)child->parent=n->parent;
}
static void rb_link_node(struct rb_node *n,struct rb_node *p,struct rb_node **link) {
    if(!p)assert(!*link);n->parent=p;*link=n;
}
static void rb_insert_color(struct rb_node *n,struct rb_root *r) {assert(r->rb_node && !r->rb_node->parent);}
static int per_task_cap_writes;
static struct task_struct *last_cap_task;
static void fbt_set_per_task_cap_task(struct task_struct *p,int a,int b,int c) {
    assert(p && p->refs>1);last_cap_task=p;per_task_cap_writes++;
}
static bool fbt_dep_task_current(const struct fpsgo_loading *fl);
static struct task_struct *fbt_dep_task_get(const struct fpsgo_loading *fl);
static void fbt_set_task_ls(int set,int mask,struct task_struct *p,int group,int *ori);
#define CONFIG_MTK_SCHEDULER 1
static int ls_writes;
static void fbt_set_ls_task(struct task_struct *p,bool set) {assert(p==expected);ls_writes++;}
static int cam_hal_pid,cam_server_pid;
static bool xgf_global_var_lock;
'''
# Keep native enum/macro values, rather than another model's numbering.
prefix += '\n'.join(re.findall(r'^#define FPSGO_(?:B|SOFT)AFFINITY[^\n]*',fbt,re.M))+'\n'
header=(b.parent/'include/fbt_cpu_platform.h').read_text()
prefix += re.search(r'enum FPSGO_CPU_PREFER \{.*?\};',header,re.S).group(0)+'\n'
prefix += r'''
static int bhr_opp,nr_freq_cpu=8;
#define XGF_ADD_DEP_FORCE_LLF 99
struct cpumask {int value;};
static int last_mask,affinity_writes;
static struct task_struct *last_affinity_task;
static int fbt_set_affinity_task(struct task_struct *p,int pref) {
    assert(p && p->refs>1);last_affinity_task=p;affinity_writes++;return 0;
}
static int fbt_set_soft_affinity_task(struct task_struct *p,int set,int pref) {
    assert(p && p->refs>1);last_affinity_task=p;affinity_writes++;return 0;
}
static int fbt_check_ls_task(struct task_struct *p) {assert(p && p->refs>1);return 0;}
static struct cpumask fbt_generate_user_cpu_mask(int v) {return (struct cpumask){v};}
static int fpsgo_task_setaffinity(struct task_struct *p,struct cpumask *m) {
    assert(p && p->refs>1);last_affinity_task=p;last_mask=m->value;return 0;
}
'''
suffix = r'''
int main(void) {
    struct task_struct old={.refs=1,.pid=7,.alive=true},newer={.refs=1,.pid=7,.nice=12,.alive=true};
    lookup=expected=&old;
    get_task_struct(&old); /* model the caller's retained dependency task */
    for(int n=MIN_NICE;n<=MAX_NICE;n++) {
        int cache=0;old.nice=n;
        fbt_nice_task(&old,1,&cache);
        assert(old.nice==MIN_NICE && cache==n-MIN_NICE+1 && old.refs==2);
        fbt_nice_task(&old,1,&cache);
        assert(cache==n-MIN_NICE+1);
        fbt_nice_task(&old,0,&cache);
        assert(old.nice==n && !cache && old.refs==2);
    }
    int out=987,cache=0;
    assert(fbt_set_nice(NULL,0,&out)==-ESRCH && out==987);
    fbt_nice_task(NULL,1,&cache);assert(!cache);
    cache=20;fbt_nice_task(NULL,0,&cache);assert(cache==20);
    assert(fbt_set_task_sched_policy(NULL,10,&out)==-ESRCH && out==987);
    old.nice=-1;lookup=&newer;
    assert(!fbt_set_nice(&old,-20,&out) && out==-1 && newer.nice==12 && old.refs==2);
    lookup=&old;int owner=0,vip=0;
    fail_sched=-EPERM;
    for(int group=0;group<3;group++) {
        fbt_change_task_policy(FPSGO_TASK_RT,&old,group,XGF_ADD_DEP_GROUPING_VIP,10,10,10,&owner,&vip,0,0);
        assert(!owner && old.rt_priority==0 && old.refs==2);
    }
    fail_sched=0;
    fbt_change_task_policy(FPSGO_TASK_RT,&old,FPSGO_GROUP_HEAVY,0,100,10,10,&owner,&vip,0,0);
    assert(!owner);
    fbt_change_task_policy(FPSGO_TASK_RT,&old,FPSGO_GROUP_HEAVY,0,10,10,10,&owner,&vip,0,0);
    assert(owner==-1 && old.rt_priority==10);
    fail_sched=-EPERM;
    fbt_change_task_policy(FPSGO_TASK_NORMAL,&old,0,0,0,0,0,&owner,&vip,0,0);
    assert(owner==-1 && old.rt_priority==10);
    fail_sched=0;
    fbt_change_task_policy(FPSGO_TASK_NORMAL,&old,0,0,0,0,0,&owner,&vip,0,0);
    assert(!owner && !old.rt_priority && old.refs==2);
    int calls=sched_calls;
    assert(!fbt_set_task_sched_policy(&old,0,&out) && out==0 && sched_calls==calls);
    for(int original=0;original<4;original++) {
        old.vip=original&1;old.vvip=original&2;vip=0;
        fbt_change_task_policy(FPSGO_TASK_VIP,&old,FPSGO_GROUP_HEAVY,0,0,0,0,&owner,&vip,4,0);
        assert(vip==(original ? original : -1) && old.vip && !old.vvip);
        fbt_change_task_policy(FPSGO_TASK_NORMAL,&old,FPSGO_GROUP_HEAVY,0,0,0,0,&owner,&vip,4,0);
        assert(!vip && old.vip==!!(original&1) && old.vvip==!!(original&2));
    }
    int mask[1]={7},policy=FPSGO_SOFTAFFINITY,prefer=FPSGO_PREFER_BIG,ls=0;
    int writes=affinity_writes;
    assert(fbt_affinity_task(FPSGO_BAFFINITY_USERDEFINE,&old,1,0,0,0,0,&policy,&prefer,&ls,mask,1)==-EINVAL);
    assert(affinity_writes==writes && policy==FPSGO_SOFTAFFINITY && prefer==FPSGO_PREFER_BIG);
    assert(fbt_affinity_task(FPSGO_BAFFINITY_USERDEFINE,&old,0,0,0,0,XGF_ADD_DEP_GROUPING_VIP,&policy,&prefer,&ls,mask,1)==-EINVAL);
    assert(!fbt_affinity_task(FPSGO_BAFFINITY_USERDEFINE,&old,0,0,0,0,0,&policy,&prefer,&ls,mask,1) && last_mask==7 && last_affinity_task==&old);
    assert(!fbt_affinity_task(FPSGO_BAFFINITY_USERDEFINE,&old,0,1,1,0,XGF_ADD_DEP,&policy,&prefer,&ls,NULL,0));
    assert(prefer==FPSGO_PREFER_LITTLE && last_affinity_task==&old);
    put_task_struct(&old);
    struct xgf_render_if r={0};
    struct xgf_dep *a=xgf_add_dep_task(8,&r,FPSGO_TYPE,1);
    assert(a && r.dep_list_size==1 && !a->rb_node.parent && a->task==&old && old.refs==2);
    assert(xgf_add_dep_task(8,&r,FPSGO_TYPE,1)==a && r.dep_list_size==1);
    assert(!xgf_add_dep_task(9,&r,FPSGO_TYPE,0));
    struct xgf_dep *b=xgf_add_dep_task(9,&r,MAGT_TYPE,1);
    assert(b && r.magt_dep_list_size==1 && !b->rb_node.parent);
    fail_alloc=true;
    assert(!xgf_add_dep_task(10,&r,FPSGO_TYPE,1) && r.dep_list_size==1);
    assert(old.refs==3);fail_alloc=false;
    old.alive=false;lookup=&newer;
    assert(xgf_add_dep_task(8,&r,FPSGO_TYPE,1)==a && a->task==&old && newer.refs==1);
    assert(xgf_non_normal_dep_task(a->task,&r)); /* cached tid must not become newer */
    render_fixture=&r;int ids[2]={987,987};unsigned int uid[2]={987,987};
    struct gbe_runtime gbe[2]={{987},{987}};
    assert(!fpsgo_comp2xgf_get_dep_list(7,2,ids,0) && ids[0]==987);
    assert(!gbe2xgf_get_dep_list(7,2,gbe,0) && gbe[0].pid==987);
    assert(!xgff_get_dep_list(7,2,uid,&r) && uid[0]==987);
    xgf_del_pid2prev_dep(&r,FPSGO_TYPE,8);
    assert(old.refs==2 && !r.dep_list_size);
    xgf_reset_render_dep_list(&r,MAGT_TYPE);
    assert(old.refs==1 && !r.magt_dep_list_size);
    a=xgf_add_dep_task(8,&r,FPSGO_TYPE,1);assert(a->task==&newer && newer.refs==2);
    assert(!xgf_non_normal_dep_task(a->task,&r));
    b=xgf_add_dep_task(9,&r,FPSGO_TYPE,1);assert(b && newer.refs==3);
    assert(fpsgo_comp2xgf_get_dep_list(7,2,ids,0)==2 && ids[0]==8 && ids[1]==9);
    assert(gbe2xgf_get_dep_list(7,2,gbe,0)==2 && gbe[0].pid==8);
    assert(xgff_get_dep_list(7,2,uid,&r)==2 && uid[0]==8);
    /* Downstream dependency-array ownership and identity-guarded mutation. */
    struct pid live={1,8},stale={1,8};
    old.identity=&stale;newer.identity=&live;
    pid_registry[0]=&old;pid_registry[1]=&newer;pid_registry_num=2;
    /* Ordered ADPF event captured the old PID object: a new numeric lookup
     * would hit newer, but actual typed admission must reject the old one.
     */
    struct xgf_render_if typed={0};
    assert(xgf_add_dep_task_identity(8,&typed,ADPF_TYPE,1,&stale,&group_identity)==ERR_PTR(-ESRCH));
    assert(!typed.dep_list_size && newer.refs==3);
    assert(xgf_add_dep_task_identity(8,&typed,ADPF_TYPE,1,&live,&group_identity)->task==&newer);
    assert(newer.refs==4);xgf_reset_render_dep_list(&typed,ADPF_TYPE);assert(newer.refs==3);
    struct pid foreign_group={1,99};
    assert(xgf_add_dep_task_identity(8,&typed,ADPF_TYPE,1,&live,&foreign_group)==ERR_PTR(-ESRCH));
    assert(!typed.dep_list_size && newer.refs==3);
    struct fpsgo_loading owning[2]={0},view[2]={0};
    assert(fpsgo_fbt2xgf_get_dep_list(7,2,owning,0)==2);
    assert(owning[0].identity==&live && live.refs==3 && owning[0].pid==8);
    fbt_copy_dep_view(view,owning,2);
    assert(!view[0].identity && view[0].pid==8 && live.refs==3); /* snapshot borrows nothing */
    assert(fbt_dep_task_current(&owning[0]));
    assert(fbt_dep_task_current(&view[0])); /* legacy numeric entry keeps old behaviour */
    expected=&newer;int before=per_task_cap_writes;
    fbt_reset_task_setting(&owning[0],1);
    assert(per_task_cap_writes==before+1 && last_cap_task==&newer && newer.refs==3);
    struct fpsgo_loading dead={.identity=&stale,.pid=8};
    assert(!fbt_dep_task_current(&dead)); /* exited task: nothing of ours to restore */
    before=per_task_cap_writes;fbt_reset_task_setting(&dead,1);
    assert(per_task_cap_writes==before && old.refs==1); /* recycled tid untouched */
    struct fpsgo_loading moved={.identity=&live,.pid=9};
    assert(!fbt_dep_task_current(&moved)); /* identity no longer names this tid */
    /* Run the COMPLETE actual frame transfer, not only leaf helpers. */
    struct render_info thr={.pid=7,.buffer_id=9,.raw_runtime=1};
    dep_alloc_fail_after=3;assert(fbt_get_dep_list(&thr)==5);
    assert(!thr.dep_arr&&!thr.dep_valid_size&&live.refs==3);
    dep_alloc_fail_after=-1;assert(!fbt_get_dep_list(&thr));
    assert(thr.dep_valid_size==2&&live.refs==5);
    thr.dep_arr[0].ori_nice=12;
    assert(!fbt_get_dep_list(&thr));
    assert(thr.dep_arr[0].identity==&live&&thr.dep_arr[0].ori_nice==12&&live.refs==5);
    dep_alloc_fail_after=0;assert(fbt_get_dep_list(&thr)==6);
    assert(thr.dep_arr[0].identity==&live&&live.refs==5);dep_alloc_fail_after=-1;
    struct fpsgo_loading prior={.identity=&stale,.pid=8,.ori_nice=25};
    struct fpsgo_loading candidate={.identity=&live,.pid=8},removed={0};int removed_count=-1;
    dep_a_except_b(&prior,1,&candidate,1,&removed,&removed_count,1);
    assert(removed_count==1&&removed.identity==&stale&&!candidate.ori_nice&&candidate.identity==&live);
    /* Cleanup must run with ALL scratch allocation denied. The live PID
     * remains owned by thr, and another renderer's shared dependency is
     * still excluded by the exact production intersection helper.
     */
    dep_alloc_fail_after=0;max_blc_pid=thr.pid;max_blc_buffer_id=thr.buffer_id;
    before=per_task_cap_writes;fbt_clear_min_cap(&thr);
    assert(per_task_cap_writes==before+1 && live.refs==5 && last_cap_task==&newer);
    max_blc_pid=0;max_blc_dep_num=1;fbt_copy_dep_view(max_blc_dep,thr.dep_arr,1);
    before=per_task_cap_writes;fbt_clear_min_cap(&thr);
    assert(per_task_cap_writes==before && live.refs==5);
    max_blc_dep_num=0;dep_alloc_fail_after=-1;
    fbt_put_dep_identities(thr.dep_arr,MAX_DEP_NUM);free(thr.dep_arr);
    assert(live.refs==3);
    fbt_put_dep_identities(owning,2);
    assert(!owning[0].identity && live.refs==1);
    fbt_put_dep_identities(owning,2);
    old.identity=newer.identity=NULL;
    xgf_reset_render_dep_list(&r,ADPF_TYPE);
    assert(newer.refs==1 && !r.dep_list_size);
    puts("PASS actual FBT helpers: retained-task nice/RT/affinity/VIP/LS/cap mutation; XGF identity cache and allocation unwind; typed owning export, full repeated frame transfer/allocation rollback, non-owning snapshots, identity-guarded reset and recycled-tid bookkeeping rejection.");
}
'''
c=a.output/'helpers.c';c.write_text(prefix+bodies+suffix)
exe=a.output/'helpers'
subprocess.run([os.environ.get('CC','clang'),'-std=gnu11','-g','-O1',
    '-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
(a.output/'SOURCE.sha256').write_text(hashlib.sha256(bodies.encode()).hexdigest()+'\n')
