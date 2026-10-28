#!/usr/bin/env python3
"""Actual native EAS consumer + ratio provider; modeled task/VIP/group/kernel primitives."""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess
root=Path(__file__).resolve().parents[1]
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('output',type=Path);a=ap.parse_args();a.output.mkdir(exist_ok=False)
p=root/'integration/target-c/provider'
def strip(t):return re.sub(r'^#include[^\n]*\n','',t,flags=re.M)
identity=strip((root/'public/drivers/misc/mediatek/performance/powerhal_cpu_ctrl/adpf_pid_identity.h').read_text())
ratio=strip((p/'mtk_perf_group_ratio_impl.inc').read_text());consumer=strip((p/'eas_adpf_owned_impl.inc').read_text())
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#define LIEPPOS_IOCTL_POWERHAL_H
#include "public/drivers/misc/mediatek/performance/perf_ioctl/powerhal_uapi.h"
#include "public/drivers/misc/mediatek/performance/powerhal_cpu_ctrl/powerhal_cpu_ctrl.h"
typedef uint64_t u64;
#define IS_ENABLED(x) (x)
#define READ_ONCE(x) __atomic_load_n(&(x),__ATOMIC_RELAXED)
#define WRITE_ONCE(x,v) __atomic_store_n(&(x),(v),__ATOMIC_RELAXED)
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define GFP_KERNEL 0
#define PF_EXITING 2
#define PF_KTHREAD 4
#define PIDTYPE_PID 0
#define PIDTYPE_TGID 1
#define EXPORT_SYMBOL_GPL(x)
#define ERR_PTR(x) ((void *)(intptr_t)(x))
#define PTR_ERR(x) ((long)(intptr_t)(x))
#define IS_ERR(x) ((uintptr_t)(x)>=(uintptr_t)-4095L)
#define max(a,b) ((a)>(b)?(a):(b))
#define max_t(t,a,b) ((t)(a)>(t)(b)?(t)(a):(t)(b))
#define SCHED_CAPACITY_SCALE 1024
#define GROUP_ID_RECORD_MAX 4
#define GROUP_ID_2 1
#define GP_MODE_0 0
#define NR_CPUS 2
static int nr_cpu_ids=2,grp_awr_init_finished=1;
#define for_each_possible_cpu(c) for((c)=0;(c)<NR_CPUS;(c)++)
#define cpu_possible(c) ((c)>=0 && (c)<NR_CPUS)
static int storage[2][4],*pcpu_pgrp_act_rto_cap[]={storage[0],storage[1]};
static _Thread_local int raw_held;
#define DEFINE_RAW_SPINLOCK(n) pthread_mutex_t n=PTHREAD_MUTEX_INITIALIZER
#define raw_spin_lock_irqsave(p,f) do{(f)=0;assert(!raw_held);pthread_mutex_lock(p);raw_held=1;}while(0)
#define raw_spin_unlock_irqrestore(p,f) do{(void)(f);assert(raw_held);raw_held=0;pthread_mutex_unlock(p);}while(0)
#define lockdep_assert_held(p) assert(raw_held)
#define DEFINE_MUTEX(n) pthread_mutex_t n=PTHREAD_MUTEX_INITIALIZER
#define mutex_lock(p) assert(!pthread_mutex_lock(p))
#define mutex_unlock(p) assert(!pthread_mutex_unlock(p))
static int fail_after=-1,allocations;
static void *kzalloc(size_t n,int f){assert(!raw_held);if(fail_after==0)return NULL;if(fail_after>0)fail_after--;void *p=calloc(1,n);if(p)allocations++;return p;}
static void kfree(void *p){assert(!raw_held);if(p){allocations--;free(p);}}
struct list_head {struct list_head *next,*prev;};
#define LIST_HEAD(n) struct list_head n={&(n),&(n)}
static void INIT_LIST_HEAD(struct list_head *h){h->next=h->prev=h;}
static bool list_empty(struct list_head *h){return h->next==h;}
static void list_add_tail(struct list_head *n,struct list_head *h){n->prev=h->prev;n->next=h;h->prev->next=n;h->prev=n;}
static void list_del(struct list_head *n){n->prev->next=n->next;n->next->prev=n->prev;}
static void list_del_init(struct list_head *n){list_del(n);INIT_LIST_HEAD(n);}
#define entry(n,t,m) ((t *)((char *)(n)-offsetof(t,m)))
#define list_for_each_entry(e,h,m) for(struct list_head *_n=(h)->next;_n!=(h) && ((e)=entry(_n,__typeof__(*(e)),m),true);_n=_n->next)
#define list_for_each_entry_safe(e,tmp,h,m) for(struct list_head *_n=(h)->next,*_next;_n!=(h) && ((_next=_n->next),(e)=entry(_n,__typeof__(*(e)),m),(tmp)=_next==(h)?NULL:entry(_next,__typeof__(*(e)),m),true);_n=_next)
struct mtk_group_task_lease;struct mtk_perf_task_lease;
struct task_struct {int refs,exit_state,flags,group,pd;bool vip;struct pid *tid,*tgid;struct task_struct *thread_next;struct mtk_group_task_lease *gowner;struct mtk_perf_task_lease *vowner;};
struct pid {int refs,nr;struct task_struct *task;};
static struct pid *get_pid(struct pid *p){if(p){assert(p->refs>0);p->refs++;}return p;}
static void put_pid(struct pid *p){if(p)assert(p->refs-->1);}
static void get_task_struct(struct task_struct *t){assert(t->refs>0);t->refs++;}
static void put_task_struct(struct task_struct *t){assert(t->refs-->1);}
static struct task_struct *pid_task(struct pid *p,int type){return p?p->task:NULL;}
static struct task_struct *get_pid_task(struct pid *p,int type){struct task_struct *t=pid_task(p,type);if(t)get_task_struct(t);return t;}
static bool pid_alive(struct task_struct *t){return t->tid && t->tid->task==t;}
static struct pid *task_tgid(struct task_struct *t){return t->tgid;}
#define rcu_read_lock() ((void)0)
#define rcu_read_unlock() ((void)0)
#define for_each_thread(l,t) for((t)=(l)->thread_next;(t);(t)=(t)->thread_next)
static int group_mode=1;static bool vip_enabled=true;
static int group_get_mode(void){return group_mode;}
static bool sched_vip_enable_get(void){return vip_enabled;}
struct mtk_group_task_lease {struct task_struct *task;bool live;int saved_group,saved_pd;};
struct mtk_perf_task_lease {struct task_struct *task;bool live,saved;};
static int mtk_group_task_acquire(struct task_struct *t,int id,struct mtk_group_task_lease **out){
 *out=NULL;if(t->gowner)return -EBUSY;struct mtk_group_task_lease *p=kzalloc(sizeof(*p),0);if(!p)return -ENOMEM;
 p->task=t;p->saved_group=t->group;p->saved_pd=t->pd;p->live=true;t->gowner=p;t->group=id;t->pd=7;get_task_struct(t);*out=p;return 0;
}
static bool mtk_group_task_is_live(struct mtk_group_task_lease *p){return p && p->live && pid_alive(p->task) && !p->task->exit_state && group_mode;}
static void mtk_group_task_release(struct mtk_group_task_lease *p){if(!p)return;struct task_struct *t=p->task;if(p->live){assert(t->gowner==p);t->gowner=NULL;if(pid_alive(t) && !t->exit_state){t->group=p->saved_group;t->pd=p->saved_pd;}}put_task_struct(t);kfree(p);}
static int mtk_perf_task_acquire(struct task_struct *t,bool vip,struct mtk_perf_task_lease **out){
 *out=NULL;if(t->vowner)return -EBUSY;struct mtk_perf_task_lease *p=kzalloc(sizeof(*p),0);if(!p)return -ENOMEM;
 p->task=t;p->saved=t->vip;p->live=true;t->vowner=p;t->vip=vip;get_task_struct(t);*out=p;return 0;
}
static bool mtk_perf_task_is_live(struct mtk_perf_task_lease *p){return p && p->live && pid_alive(p->task) && !p->task->exit_state;}
static void mtk_perf_task_release(struct mtk_perf_task_lease *p){if(!p)return;struct task_struct *t=p->task;if(p->live){assert(t->vowner==p);t->vowner=NULL;if(pid_alive(t) && !t->exit_state)t->vip=p->saved;}put_task_struct(t);kfree(p);}
static void ordinary_vip(struct task_struct *t,bool vip){if(t->vowner){t->vowner->live=false;t->vowner=NULL;}t->vip=vip;}
struct work_struct {void(*fn)(struct work_struct*);};
struct delayed_work {struct work_struct work;bool pending;};
#define DECLARE_DELAYED_WORK(n,f) struct delayed_work n={.work={f}}
#define HZ 100
static int system_wq;
static bool queue_delayed_work(int q,struct delayed_work *w,int delay){bool was=w->pending;w->pending=true;assert(delay==HZ);return !was;}
static int pins;
#define THIS_MODULE NULL
static void __module_get(void *module){pins++;}
static bool trace_sched_adpf_get_value_enabled(void){return false;}
#define trace_sched_adpf_get_value(...) ((void)0)
static u64 mul_u64_u64_div_u64(u64 a,u64 b,u64 c){return (unsigned __int128)a*b/c;}
'''
suffix=r'''
static struct pid owner={1,42},other_owner={1,42},app={1,200},ids[5];
static struct task_struct owner_task={.refs=1},other_task={.refs=1},app_task={.refs=1},tasks[5];
static void initialize(void){
 owner.task=&owner_task;other_owner.task=&other_task;app.task=&app_task;
 owner_task.tid=owner_task.tgid=&owner;other_task.tid=other_task.tgid=&other_owner;app_task.tid=app_task.tgid=&app;
 for(int i=0;i<5;i++){ids[i]=(struct pid){1,201+i,&tasks[i]};tasks[i]=(struct task_struct){.refs=1,.tid=&ids[i],.tgid=&app,.pd=3,.group=-1};if(i<4)tasks[i].thread_next=&tasks[i+1];}
 app_task.thread_next=&tasks[0];for(int c=0;c<2;c++)for(int g=0;g<4;g++)storage[c][g]=600+c;
}
static struct _SESSION event(unsigned sid,int first,int count){struct _SESSION s={.owner=&owner,.target=&app,.sid=sid,.tgid=200,.durationNanos=1000,.threadIds_size=count};for(int i=0;i<count;i++){s.thread_owners[i]=&ids[first+i];s.threadIds[i]=ids[first+i].nr;}return s;}
static int send(struct _SESSION *s,int cmd){s->cmd=cmd;return sched_adpf_callback(s);}
static int report(struct _SESSION *s,long duration){struct _SESSION_WORK_DURATION d={.durationNanos=duration};s->workDuration[0]=&d;s->work_duration_size=1;int rc=send(s,ADPF_REPORT_ACTUAL_WORK_DURATION);s->workDuration[0]=NULL;s->work_duration_size=0;return rc;}
static struct eas_owned_session *lookup(unsigned sid){for(int i=0;i<EAS_OWNED_MAX_SESSIONS;i++)if(eas_owned_sessions[i].used && eas_owned_sessions[i].sid==sid)return &eas_owned_sessions[i];return NULL;}
static void reap(void){eas_owned_reaper.pending=false;eas_owned_reaper.work.fn(&eas_owned_reaper.work);}
static void *reports(void *arg){struct _SESSION *s=arg;for(int i=0;i<1000;i++)assert(!report(s,500+i%400));return NULL;}
static void clear(void){set_eas_adpf_enable(0);assert(list_empty(&eas_owned_members));assert(list_empty(&mtk_group_ratio_leases));assert(allocations==0);assert(owner.refs==1 && other_owner.refs==1 && app.refs==1);for(int i=0;i<5;i++)assert(tasks[i].refs==1 && ids[i].refs==1);set_eas_adpf_enable(1);}
int main(void){
 initialize();struct _SESSION a=event(1,0,2),b=event(2,1,1),c=event(3,2,1);
 for(int n=0;n<4;n++){fail_after=n;assert(send(&c,ADPF_CREATE_HINT_SESSION)==-ENOMEM);fail_after=-1;assert(!lookup(3) && allocations==0 && !pins);}
 assert(!send(&a,ADPF_CREATE_HINT_SESSION));assert(!send(&b,ADPF_CREATE_HINT_SESSION));assert(pins==1);
 assert(lookup(1)->members[1]==lookup(2)->members[0] && lookup(2)->members[0]->users==2);
 assert(tasks[0].group==1 && tasks[1].vip==!!CONFIG_MTK_SCHED_VIP_TASK);
 pthread_t t1,t2;pthread_create(&t1,NULL,reports,&a);pthread_create(&t2,NULL,reports,&b);pthread_join(t1,NULL);pthread_join(t2,NULL);
 assert(!report(&a,500) && !report(&b,900));assert(storage[0][1]==921);
 struct _SESSION set=event(1,1,3);
 for(int n=0;n<(CONFIG_MTK_SCHED_VIP_TASK?6:4);n++){
  fail_after=n;assert(send(&set,ADPF_SET_THREADS)==-ENOMEM);fail_after=-1;
  assert(lookup(1)->threads[0]==&ids[0] && lookup(1)->count==2);assert(lookup(2)->members[0]->users==2);
  assert(tasks[2].group==-1 && !tasks[2].vip && tasks[2].refs==1 && ids[2].refs==1);assert(storage[0][1]==921);
 }
 assert(!send(&a,ADPF_PAUSE));assert(tasks[0].group==-1 && tasks[1].group==1 && storage[0][1]==921);
 assert(!send(&a,ADPF_RESUME));assert(tasks[0].group==1);
 assert(!send(&a,ADPF_CLOSE));assert(tasks[0].group==-1 && tasks[1].group==1 && storage[0][1]==921);
 /* A foreign/dead creator CLOSE cannot remove a live session's ownership. */
 struct _SESSION stale=b;stale.owner=&other_owner;assert(!send(&stale,ADPF_CLOSE));assert(lookup(2));
 assert(!send(&b,ADPF_CLOSE));assert(tasks[1].group==-1 && storage[0][1]==600 && storage[1][1]==601);
 clear();
 a=event(1,0,1);assert(!send(&a,ADPF_CREATE_HINT_SESSION));
 a.targetDurationNanos=LONG_MAX;assert(!send(&a,ADPF_UPDATE_TARGET_WORK_DURATION));
 assert(!report(&a,LONG_MAX-100));assert(storage[0][1]==1023);assert(!report(&a,LONG_MAX));assert(storage[0][1]==1024);
 /* Ordinary ratio writer revokes the old request. Reaping never restores it;
  * a NEW report can acquire against the writer's new baseline. */
 mtk_group_ratio_all_set(1,333);reap();assert(lookup(1)->quiesced && tasks[0].group==-1 && storage[0][1]==333);
 assert(!report(&a,LONG_MAX/2));assert(!lookup(1)->quiesced && storage[0][1]==511);
 if(CONFIG_MTK_SCHED_VIP_TASK){ordinary_vip(&tasks[0],false);reap();assert(lookup(1)->quiesced && !tasks[0].vip);assert(!report(&a,LONG_MAX));}
 clear();
 /* Queued old thread object cannot resolve a replacement with the same TID. */
 a=event(1,0,1);assert(!send(&a,ADPF_CREATE_HINT_SESSION));
 ids[0].task=NULL;tasks[0].exit_state=1;reap();assert(lookup(1)->quiesced);
 struct task_struct replacement={.refs=1,.group=-1,.pd=3,.tgid=&app};struct pid recycled={1,201,&replacement};replacement.tid=&recycled;
 assert(report(&a,500)==-ESRCH);assert(replacement.group==-1 && replacement.refs==1);
 a.thread_owners[0]=&recycled;assert(!send(&a,ADPF_SET_THREADS));assert(replacement.group==1);assert(!send(&a,ADPF_CLOSE));assert(replacement.group==-1 && replacement.refs==1 && recycled.refs==1);
 tasks[0].exit_state=0;ids[0].task=&tasks[0];tasks[0].group=-1;tasks[0].pd=3;tasks[0].vip=false;
 clear();
 a=event(1,0,1);assert(!send(&a,ADPF_CREATE_HINT_SESSION));
 app_task.exit_state=1;reap();assert(lookup(1)); /* live siblings keep app alive */
 for(int i=0;i<5;i++)tasks[i].exit_state=1;
 reap();assert(!lookup(1));app_task.exit_state=0;
 for(int i=0;i<5;i++){tasks[i].exit_state=0;tasks[i].group=-1;tasks[i].vip=false;tasks[i].pd=3;}
 clear();
 assert(!send(&a,ADPF_CREATE_HINT_SESSION));set_eas_adpf_enable(0);assert(!lookup(1));assert(!send(&a,ADPF_CLOSE));assert(send(&a,ADPF_CREATE_HINT_SESSION)==-EACCES);set_eas_adpf_enable(1);assert(send(&a,ADPF_RESUME)==-ENOENT);
 clear();
 a=event(1,0,1);b=event(2,0,1);b.owner=&other_owner;
 assert(!send(&a,ADPF_CREATE_HINT_SESSION) && !send(&b,ADPF_CREATE_HINT_SESSION));
 owner_task.exit_state=1;reap();assert(!lookup(1) && lookup(2));
 assert(!send(&a,ADPF_CLOSE) && lookup(2));assert(!report(&b,500));
 owner_task.exit_state=0;clear();
 group_mode=0;a=event(1,0,1);
 if(CONFIG_MTK_SCHED_VIP_TASK){assert(!send(&a,ADPF_CREATE_HINT_SESSION));assert(tasks[0].group==-1 && tasks[0].vip);assert(!report(&a,500));assert(!send(&a,ADPF_PAUSE) && !tasks[0].vip);}
 else assert(send(&a,ADPF_CREATE_HINT_SESSION)==-EOPNOTSUPP);
 clear();group_mode=1;assert(pins==1);
 a=event(1,0,1);assert(!send(&a,ADPF_CREATE_HINT_SESSION));
 eas_adpf_release_sessions();assert(get_eas_adpf_enable()==1 && !lookup(1));
 reap();assert(!eas_owned_reaper.pending);clear();
 puts("PASS actual EAS consumer+ratio: shared tasks/session close/pause/resume/replace rollback/preemption/new-report baseline/PID reuse/zombie cleanup/64-bit ratio/disable; kernel and group/VIP primitives modeled");
}
'''
for vip in [1,0]:
 # Negative capability path has fewer allocation sites, not a fictitious VIP token.
 tail=suffix if vip else suffix.replace('n<4;n++','n<3;n++')
 c=a.output/f'eas-{vip}.c';c.write_text(f'#define CONFIG_MTK_PERF_CTRL_LEASE 1\n#define CONFIG_MTK_SCHED_VIP_TASK {vip}\n'+prefix+identity+ratio+consumer+tail)
 exe=c.with_suffix('');subprocess.run([os.environ.get('CC','clang'),'-std=gnu11','-g','-O1','-pthread','-fsanitize=address,undefined','-I'+str(root),str(c),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
(a.output/'RESULT.json').write_text(json.dumps(dict(scope=__doc__,consumer_sha256=hashlib.sha256((p/'eas_adpf_owned_impl.inc').read_bytes()).hexdigest(),ratio_sha256=hashlib.sha256((p/'mtk_perf_group_ratio_impl.inc').read_bytes()).hexdigest(),result='PASS',vip_configs=[1,0],native_irq_verification=False),indent=2)+'\n')
