#!/usr/bin/env python3
"""Execute actual ADPF session methods with host services; NOT native race/ABI proof."""
from pathlib import Path
import argparse
import os
import re
import subprocess

root = Path(__file__).resolve().parents[1]
source = root / 'public/drivers/misc/mediatek/performance/powerhal_cpu_ctrl/powerhal_service.c'
t = source.read_text()

def definition(signature):
    start = t.index(signature)
    i = t.index('{', start) + 1
    depth = 1
    while depth:
        depth += (t[i] == '{') - (t[i] == '}')
        i += 1
    return t[start:i]

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('output', type=Path)
a = ap.parse_args()
a.output.mkdir(exist_ok=False)
# Lifetime ordering is checked in the real init/teardown as well as executing
# the teardown below. Queue/SRCU/RCU primitives are host boundaries, not proofs.
init = definition('static int __init powerhal_cpu_ctrl_init(void)')
assert init.index('adpf_stopping = false') < init.index('proc_mkdir(')
destroy = definition('static void powerhal_destroy_events(void)')
assert destroy.index('adpf_stopping = true') < destroy.index('cancel_delayed_work_sync(')
assert destroy.index('cancel_delayed_work_sync(') < destroy.index('destroy_workqueue(')
assert destroy.index('destroy_workqueue(') < destroy.index('put_pid(')

prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#define LIEPPOS_IOCTL_POWERHAL_H
#include "public/drivers/misc/mediatek/performance/perf_ioctl/powerhal_uapi.h"
#include "public/drivers/misc/mediatek/performance/powerhal_cpu_ctrl/adpf_session_snapshot.h"
#define GFP_KERNEL 0
#define HZ 100
#define PIDTYPE_TGID 1
#define PIDTYPE_PID 0
#define PF_EXITING 2
#define PF_KTHREAD 4
#define DEFINE_MUTEX(n) pthread_mutex_t n = PTHREAD_MUTEX_INITIALIZER
#define mutex_lock(p) assert(pthread_mutex_lock(p)==0)
#define mutex_unlock(p) assert(pthread_mutex_unlock(p)==0)
#define lockdep_assert_held(p) ((void)0)
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x,v) ((x)=(v))
#define ERR_PTR(x) ((void *)(intptr_t)(x))
#define PTR_ERR(x) ((long)(intptr_t)(x))
#define IS_ERR(x) ((uintptr_t)(x)>=(uintptr_t)-4095L)
#define EXPORT_SYMBOL(x)
#define pr_warn_ratelimited(...) ((void)0)
#define WARN_ON(x) assert(!(x))
#define rcu_read_lock() ((void)0)
#define rcu_read_unlock() ((void)0)
#define DEFINE_STATIC_SRCU(n) static int n
static int srcu_read_lock(int *p) {return 0;}
static void srcu_read_unlock(int *p,int i) {}
static void synchronize_srcu(int *p) {}
typedef struct {int counter;} atomic_t;
#define ATOMIC_INIT(n) {n}
static int atomic_read(atomic_t *a) {return a->counter;}
static void atomic_inc(atomic_t *a) {a->counter++;}
static void atomic_dec(atomic_t *a) {assert(a->counter>0);a->counter--;}
struct pid {int refs, number; bool alive,zombie,other_live;};
struct task_struct {struct pid *tgid,*tid;int exit_state,flags;struct task_struct *sibling;};
static _Thread_local struct task_struct *current;
static struct pid *task_tgid(struct task_struct *p) {return p->tgid;}
static struct pid *get_task_pid(struct task_struct *p,int type) {
    struct pid *id=type==PIDTYPE_TGID?p->tgid:p->tid;assert(id);id->refs++;return id;
}
static struct pid target_app={1,200,true};
static struct pid *get_pid(struct pid *p) {if(p)p->refs++;return p;}
static struct pid *find_get_pid(int n) {return n==200?get_pid(&target_app):NULL;}
static void put_pid(struct pid *p) {if(p)assert(--p->refs>=1);}
static bool pid_has_task(struct pid *p,int type) {assert(type==PIDTYPE_TGID);return p->alive;}
static int pid_nr(struct pid *p){return p?p->number:0;}
static struct pid t201={1,201,true},t201_reused={1,201,true},t202={1,202,true};
static struct pid t300={1,300,true},t301={1,301,true},t302={1,302,true},foreign={1,999,true},t303={1,303,true};
static struct task_struct thread_tasks[]={ {&target_app,&t201},{&target_app,&t202},{&target_app,&t300},{&target_app,&t301},{&target_app,&t302},{&foreign,&t303} };
static struct task_struct reused_thread={&target_app,&t201_reused};
static bool reuse_thread201;
static struct task_struct *find_task_by_vpid(int id){
    if(id==201 && reuse_thread201)return &reused_thread;
    for(unsigned i=0;i<sizeof(thread_tasks)/sizeof(thread_tasks[0]);i++)if(thread_tasks[i].tid->number==id)return &thread_tasks[i];
    return NULL;
}
static bool pid_alive(struct task_struct *p){return p->tid && p->tid->alive;}
static _Thread_local struct task_struct leader_view,other_view;
static struct task_struct *pid_task(struct pid *p,int type){
    assert(type==PIDTYPE_TGID);if(!p || !p->alive)return NULL;
    leader_view=(struct task_struct){.tgid=p,.exit_state=p->zombie};
    other_view=(struct task_struct){.tgid=p};
    leader_view.sibling=p->other_live?&other_view:NULL;return &leader_view;
}
#define for_each_thread(leader,thread) for((thread)=(leader)->sibling;(thread);(thread)=(thread)->sibling)
struct work_struct {void (*fn)(struct work_struct *);struct work_struct *next;};
struct delayed_work {struct work_struct work;bool pending;};
struct workqueue_struct {struct work_struct *head,*tail;};
static struct workqueue_struct queue;
static struct workqueue_struct *system_wq=&queue;
#define DECLARE_DELAYED_WORK(n,f) struct delayed_work n = {.work={.fn=f}}
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define INIT_WORK(p,f) do {(p)->fn=f;(p)->next=NULL;}while(0)
static bool queue_work(struct workqueue_struct *q,struct work_struct *w) {
    w->next=NULL;if(q->tail)q->tail->next=w;else q->head=w;q->tail=w;return true;
}
static void drain(struct workqueue_struct *q) {
    while(q->head) {struct work_struct *w=q->head;q->head=w->next;if(!q->head)q->tail=NULL;w->fn(w);}
}
static bool queue_delayed_work(struct workqueue_struct *q,struct delayed_work *w,int delay) {
    bool was=w->pending;assert(delay==HZ);w->pending=true;return !was;
}
static void cancel_delayed_work_sync(struct delayed_work *w) {w->pending=false;}
static void destroy_workqueue(struct workqueue_struct *q) {drain(q);}
static int fail_alloc, allocs, frees;
static void *kzalloc(size_t n,int flags) {
    if(fail_alloc && !--fail_alloc)return NULL;
    void *p=calloc(1,n);assert(p);allocs++;return p;
}
static void kfree(void *p) {if(p){frees++;free(p);}}
static void _adpf_systrace(int tgid,long val,const char *fmt,...) {}
struct file {int unused;};
static int copy_from_user(void *d,const void *s,size_t n) {memcpy(d,s,n);return 0;}
static int kstrtoint(char *p,int base,int *v) {
    char *end;long n=strtol(p,&end,base);if(end==p)return -EINVAL;*v=n;return 0;
}
'''
prefix += re.sub(r'^#include[^\n]*\n', '', (source.parent/'adpf_pid_identity.h').read_text(), flags=re.M)
# Whole production global/event declarations, real dispatch body, real session
# helper/method region and actual disable entry point. No algorithm reimplementation.
globals_ = t[t.index('// ADPF\n'):t.index('void send_boost_cmd(')]
methods = t[t.index('/* Prepare all fallible storage'):t.index('int dsu_sport_mode(')]
bodies = [globals_, definition('int adpf_register_callback('),
          definition('int adpf_unregister_callback('), methods,
          definition('static ssize_t perfmgr_adpf_enable_proc_write('), destroy]
(a.output/'adpf_production.h').write_text('\n'.join(bodies)+'\n')
argv = ['cc', '-std=gnu11', '-g', '-O1', '-fno-omit-frame-pointer', '-no-pie',
        '-fsanitize=address,undefined', '-pthread', '-I'+str(root), '-I'+str(a.output),
        str(root/'tests/adpf_sessions_test.c'), '-include', str(a.output/'adpf_shim.h'),
        '-o', str(a.output/'adpf-sessions')]
(a.output/'adpf_shim.h').write_text(prefix)
subprocess.run(argv, check=True)
env = dict(os.environ, ASAN_OPTIONS='detect_leaks=1:abort_on_error=1')
subprocess.run([str(a.output/'adpf-sessions')], check=True, env=env)
print('PASS: actual ADPF transaction rollback, creator PID identity, atomic disable, death/reuse cleanup and teardown')
