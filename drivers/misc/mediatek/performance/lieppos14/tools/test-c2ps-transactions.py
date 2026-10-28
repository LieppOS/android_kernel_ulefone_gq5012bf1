#!/usr/bin/env python3
"""Actual C2PS queue/dispatcher with pthread host primitives; not native ABI/race proof."""
from pathlib import Path
import argparse
import os
import re
import subprocess

root = Path(__file__).resolve().parents[1]
t = (root/'public/drivers/misc/mediatek/c2ps/c2ps_main.c').read_text()
def definition(signature):
    start = t.index(signature)
    i = t.index('{', start) + 1
    depth = 1
    while depth:
        depth += (t[i] == '{') - (t[i] == '}')
        i += 1
    return t[start:i]

# Module publication must not be followed by a fallible PM registration.
init = definition('static int __init c2ps_init(void)')
assert init.index('register_pm_notifier(') < init.index('__module_get(THIS_MODULE)')
tail = init[init.index('__module_get(THIS_MODULE)'):init.index('fail_regulator:')]
assert not re.search(r'\b(register_\w+|kthread_create|kzalloc)\s*\(', tail)
assert 'c2ps_publish_callbacks()' in tail
pm = definition('static int c2ps_pm_event(')
assert pm.index('pm_blocked = true') < pm.index('c2ps_queue_work(push)')
assert 'notifier_from_errno(ret)' in pm

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('output', type=Path)
a = ap.parse_args()
a.output.mkdir(exist_ok=False)
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <errno.h>
typedef uint32_t u32; typedef uint64_t u64;
#define MAX_TASK_NAME_SIZE 10
#define MAX_NUMBER_OF_CLUSTERS 8
#define C2PS_LEASE_TIMEOUT_SEC 5
#define HZ 100
#define PIDTYPE_TGID 1
#define READ_ONCE(x) __atomic_load_n(&(x),__ATOMIC_RELAXED)
#define WRITE_ONCE(x,v) __atomic_store_n(&(x),v,__ATOMIC_RELAXED)
#define xchg(p,v) __atomic_exchange_n(p,v,__ATOMIC_SEQ_CST)
#define unlikely(x) (x)
#define max(a,b) ((a)>(b)?(a):(b))
#define msecs_to_jiffies(x) (x)
#define C2PS_LOGE(...) ((void)0)
struct list_head { struct list_head *next,*prev; };
static void list_init(struct list_head *h) {h->next=h->prev=h;}
static bool list_empty(struct list_head *h) {return h->next==h;}
static void list_add_tail(struct list_head *p,struct list_head *h) {
    p->next=h; p->prev=h->prev; h->prev->next=p; h->prev=p;
}
static void list_del(struct list_head *p) {p->prev->next=p->next;p->next->prev=p->prev;}
#define list_first_entry(h,t,m) ((t *)((char *)(h)->next - offsetof(t,m)))
struct completion {pthread_mutex_t lock;pthread_cond_t cond;bool done;};
static void init_completion(struct completion *c) {
    pthread_mutex_init(&c->lock,0);pthread_cond_init(&c->cond,0);c->done=false;
}
static void complete(struct completion *c) {
    pthread_mutex_lock(&c->lock);c->done=true;pthread_cond_broadcast(&c->cond);pthread_mutex_unlock(&c->lock);
}
static void wait_for_completion(struct completion *c) {
    pthread_mutex_lock(&c->lock);while(!c->done)pthread_cond_wait(&c->cond,&c->lock);
    pthread_mutex_unlock(&c->lock);pthread_cond_destroy(&c->cond);pthread_mutex_destroy(&c->lock);
}
struct pid {int refs;bool dead;};
/* PID/signal/RCU liveness is a modeled native boundary in this fixture. */
static bool c2ps_process_alive(struct pid *p) {return p&&!p->dead;}
struct task_struct {struct pid *owner;int refs;};
static struct pid owner_a={2},owner_b={1};
static struct task_struct worker={0},caller_a={&owner_a,1},caller_b={&owner_b,1},target={0,1};
static _Thread_local struct task_struct *current;
static struct pid *get_task_pid(struct task_struct *p,int which) {
    __atomic_add_fetch(&p->owner->refs,1,__ATOMIC_RELAXED);return p->owner;
}
static void put_pid(struct pid *p) {if(p)assert(__atomic_sub_fetch(&p->refs,1,__ATOMIC_RELAXED)>=1);}
static void put_task_struct(struct task_struct *p) {assert(__atomic_sub_fetch(&p->refs,1,__ATOMIC_RELAXED)>=1);}
static void c2ps_free(void *p,int size) {free(p);}
static pthread_mutex_t notifier_wq_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t event_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t event_cond=PTHREAD_COND_INITIALIZER;
#define mutex_lock(p) pthread_mutex_lock(p)
#define mutex_unlock(p) pthread_mutex_unlock(p)
static int notifier_wq_queue,condition_notifier_wq;
static bool background_pending,expiration_pending,stop_worker;
static bool notifier_closing,session_active=true,activation_pending,pm_blocked;
static unsigned int notifier_pending;
static struct list_head head;
static struct task_struct *c2ps_tsk=&worker;
static struct pid *session_owner=&owner_a;
static int self_uninit_timer,backgroup_info_update_timer;
static unsigned int jiffies=17,background_monitor_duration=33,refreshes;
static int operation_result=-ESTALE;
static bool kthread_should_stop(void) {return READ_ONCE(stop_worker);}
static void wake_up_interruptible(int *q) {
    pthread_mutex_lock(&event_lock);pthread_cond_broadcast(&event_cond);pthread_mutex_unlock(&event_lock);
}
#define wait_event_interruptible(q,predicate) do { \
    pthread_mutex_lock(&event_lock); \
    while(!(predicate))pthread_cond_wait(&event_cond,&event_lock); \
    pthread_mutex_unlock(&event_lock); \
} while(0)
static void mod_timer(int *p,unsigned int j) {refreshes++;}
static void c2ps_reap_dead_tasks(void) {}
static void update_cpu_idle_rate(void) {}
static bool need_update_background(void) {return false;}
struct global_info {pthread_mutex_t mlock; void *native_ctrl; int native_ctrl_error;
    int max_uclamp[8],curr_max_uclamp[8];};
static struct global_info global={.mlock=PTHREAD_MUTEX_INITIALIZER};
struct regulator_req {struct global_info *glb_info;};
static struct regulator_req *get_regulator_req(void) {return NULL;}
static struct global_info *get_glb_info(void) {return &global;}
/* Native provider behavior is tested separately with its real C. These queue
 * tests intentionally keep the backend live at their algorithm boundary. */
static bool mtk_perf_ctrl_is_live(void *p) {return true;}
static int common_error,acquire_error,common_exits,ctrl_token;
static unsigned int c2ps_nr_clusters=3;
static int picked_wl_table=2,last_wl;
static int get_nr_wl_type(void) {return 4;}
static int init_c2ps_common(void) {return common_error;}
static void exit_c2ps_common(void) {common_exits++;global.native_ctrl=NULL;}
static int mtk_perf_ctrl_acquire(unsigned int n,const int *v,int w,void **out) {
    *out=NULL;last_wl=w;if(acquire_error)return acquire_error;*out=&ctrl_token;return 0;
}
static void set_config_camfps(int fps) {}
static struct pid *get_pid(struct pid *p) {if(p)__atomic_add_fetch(&p->refs,1,__ATOMIC_RELAXED);return p;}
static void send_regulator_req(struct regulator_req *r) {}
static void reset_need_update_status(void) {}
'''
# Real event definitions and production ownership/dispatch functions, not a
# second queue implementation. Algorithm callbacks are deliberate boundaries.
event = definition('enum C2PS_NOTIFIER_PUSH_TYPE') + ';\n' + definition('struct C2PS_NOTIFIER_PUSH_TAG {') + ';\n'
stubs = r'''
static int c2ps_notifier_wq_cb_uninit(void) {return operation_result;}
static int c2ps_notifier_wq_cb_add_task(u32 a,u32 b,u32 c,int d,u32 e,bool f,bool g,const char*h) {return operation_result;}
static int c2ps_notifier_wq_cb_task_start(struct task_struct*t,int id) {return operation_result;}
static int c2ps_notifier_wq_cb_task_end(struct task_struct*t,int id) {return operation_result;}
static int c2ps_notifier_wq_cb_scene_change(int id,int mode) {return operation_result;}
static int c2ps_notifier_wq_cb_camfps(int fps) {return operation_result;}
static int c2ps_notifier_wq_cb_vsync(u64 ts) {return operation_result;}
static int c2ps_notifier_wq_cb_task_single_shot(int*a,int b,int c,int*d,int*e,int*f,bool g) {return operation_result;}
'''
functions = '\n'.join(definition(x) for x in ['static int c2ps_notifier_wq_cb_init(', 'static void c2ps_free_push(', 'static void c2ps_complete_push(', 'static int c2ps_queue_work(', 'static void c2ps_notifier_wq_cb(void)'])
suffix = r'''
static void *run_worker(void *p) {
    current=&worker;
    while(!kthread_should_stop())c2ps_notifier_wq_cb();
    return NULL;
}
static struct C2PS_NOTIFIER_PUSH_TAG *request(void) {
    struct C2PS_NOTIFIER_PUSH_TAG *p=calloc(1,sizeof(*p));assert(p);
    p->ePushType=C2PS_NOTIFIER_TASK_START;p->task=&target;
    __atomic_add_fetch(&target.refs,1,__ATOMIC_RELAXED);return p;
}
int main(void) {
    pthread_t thread;list_init(&head);current=&caller_a;
    assert(pthread_create(&thread,0,run_worker,0)==0);
    assert(c2ps_queue_work(request())==-ESTALE);
    assert(owner_a.refs==2 && target.refs==1 && !notifier_pending && !refreshes);
    current=&caller_b;
    assert(c2ps_queue_work(request())==-EPERM);
    assert(owner_b.refs==1 && target.refs==1 && !refreshes);
    current=&caller_a;notifier_pending=256;
    assert(c2ps_queue_work(request())==-EAGAIN && target.refs==1);
    notifier_pending=0;operation_result=-ENOMEM;
    assert(c2ps_queue_work(request())==-ENOMEM && !refreshes);
    operation_result=0;
    assert(c2ps_queue_work(request())==0 && refreshes==1 && target.refs==1);
    pm_blocked=true;
    assert(c2ps_queue_work(request())==-EBUSY && target.refs==1);
    pm_blocked=false;
    notifier_closing=true;
    assert(c2ps_queue_work(request())==-ESHUTDOWN && target.refs==1);
    notifier_closing=false;session_active=false;
    assert(c2ps_queue_work(request())==-ENODEV && target.refs==1);
    current=&worker;
    assert(c2ps_queue_work(request())==-EDEADLK && target.refs==1);
    WRITE_ONCE(stop_worker,true);wake_up_interruptible(&notifier_wq_queue);
    pthread_join(thread,0);
    assert(owner_a.refs==2 && owner_b.refs==1 && !notifier_pending);
    /* Real activation body: no session is published on either failure. */
    struct C2PS_NOTIFIER_PUSH_TAG init={.actor=&owner_a,.camfps=30000,
        .overwrite_uclamp_max={400,500,600}};
    activation_pending=true;common_error=-ENOMEM;
    assert(c2ps_notifier_wq_cb_init(&init)==-ENOMEM && !activation_pending && !session_active);
    common_error=0;activation_pending=true;acquire_error=-EBUSY;
    assert(c2ps_notifier_wq_cb_init(&init)==-EBUSY && common_exits==1);
    assert(!session_active && !activation_pending && !global.native_ctrl && owner_a.refs==2);
    activation_pending=true;acquire_error=0;picked_wl_table=999;
    assert(!c2ps_notifier_wq_cb_init(&init) && session_active && !activation_pending);
    assert(last_wl==0);
    assert(session_owner==&owner_a && owner_a.refs==3 && global.native_ctrl==&ctrl_token);
    for(int i=0;i<3;i++)assert(global.max_uclamp[i]==init.overwrite_uclamp_max[i] &&
        global.curr_max_uclamp[i]==init.overwrite_uclamp_max[i]);
    put_pid(session_owner);exit_c2ps_common();session_active=false;session_owner=NULL;
    puts("PASS: actual C2PS activation rollback plus queue/dispatcher completion, TGID ownership, bounds and release-before-reply");
}
'''
c=a.output/'transactions.c';c.write_text(prefix+event+stubs+functions+suffix)
out=a.output/'transactions'
subprocess.run([os.environ.get('CC','clang'),'-std=gnu11','-O1','-g','-pthread',
    '-fsanitize=address,undefined',str(c),'-o',str(out)],check=True)
subprocess.run([str(out)],check=True,timeout=30)
