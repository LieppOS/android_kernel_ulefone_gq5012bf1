#!/usr/bin/env python3
"""Focused actual-production renderer/FBT admission test. Kernel primitives modeled.
Not a native ABI/IRQ proof or completion of event/BQ/downstream boost ownership.
"""
import argparse
from pathlib import Path
import re
import subprocess
root=Path(__file__).resolve().parents[1]
p=root/'public/drivers/misc/mediatek/performance/fpsgo_v3'
a=argparse.ArgumentParser(description=__doc__);a.add_argument('output',type=Path);opt=a.parse_args();opt.output.mkdir(parents=True,exist_ok=False)
base=(p/'base/src/fpsgo_base.c').read_text();fbt=(p/'fbt/src/fbt_cpu.c').read_text()
def leaf(text,name):
    m=re.search(r'^(?:static )?(?:int|bool|void|struct (?:render_info|fbt_thread_blc) \*|const struct fpsgo_task_identity \*)\s*'+name+r'\([^)]*\)\n\{',text,re.M);assert m,name
    i=m.end();depth=1
    while depth:depth+=(text[i]=='{')-(text[i]=='}');i+=1
    return text[m.start():i]
c=r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#define READ_ONCE(x) (x)
#define PIDTYPE_PID 0
#define PIDTYPE_TGID 1
#define GFP_KERNEL 0
#define FPSGO_MAX_RENDER_INFO_SIZE 20
#define FPSGO_TYPE 0
#define ADPF_TYPE 1
#define NATIVE_WINDOW_API_EGL 1
#define NON_VSYNC_ALIGNED_TYPE 1
#define BY_PASS_TYPE 0
#define ACQUIRE_UNKNOWN_TYPE 0
#define RESCUE_TIMER_NUM 5
#define LOADING_WEIGHT 50
#define DEFAULT_ST2WNT_ADJ 3
#define FPSGO_LOGE(...) ((void)0)
#define fpsgo_main_trace(...) ((void)0)
#define kmemleak_not_leak(x) ((void)0)
#define WARN_ON(x) (!!(x))
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
struct pid {int refs,nr;};
struct signal_struct {int live;};
struct task_struct {bool alive;int exit_state;struct pid *tid,*group;struct signal_struct *signal;};
struct fpsgo_task_identity {struct pid *tid,*tgid;bool process;};
#define atomic_read(p) (*(p))
static struct pid oldpid={1,71},newpid={1,71},group={1,70},foreign={1,90};
static struct signal_struct oldsig={1},newsig={1};
static struct task_struct oldtask={true,0,&oldpid,&group,&oldsig},newtask={true,0,&newpid,&group,&newsig};
static struct pid *task_pid(struct task_struct *t){return t->tid;}
static struct task_struct *lookup=&oldtask;
static int init_pid_ns,rcu_depth;
static void rcu_read_lock(void){rcu_depth++;}
static void rcu_read_unlock(void){assert(rcu_depth>0);rcu_depth--;}
static struct pid *find_pid_ns(int n,void *ns){assert(rcu_depth);return lookup && lookup->alive && lookup->tid->nr==n?lookup->tid:NULL;}
static struct pid *get_pid(struct pid *p){if(p){assert(p->refs>0);p->refs++;}return p;}
static void put_pid(struct pid *p){if(p)assert(p->refs-->1);}
static struct pid *get_task_pid(struct task_struct *t,int type){assert(rcu_depth);return get_pid(type==PIDTYPE_PID?t->tid:t->group);}
static struct task_struct *pid_task(struct pid *p,int type){assert(rcu_depth);if(oldtask.alive && p==oldtask.tid)return &oldtask;if(newtask.alive && p==newtask.tid)return &newtask;return NULL;}
static bool pid_alive(struct task_struct *t){return t->alive;}
static struct pid *task_tgid(struct task_struct *t){return t->group;}
static int pid_nr(struct pid *p){return p?p->nr:0;}
struct list_head {struct list_head *next,*prev;};
#define INIT_LIST_HEAD(p) do{(p)->next=(p);(p)->prev=(p);}while(0)
static bool list_empty(struct list_head *p){return p->next==p;}
static void list_add(struct list_head *p,struct list_head *h){p->next=h->next;p->prev=h;h->next->prev=p;h->next=p;}
static void list_del(struct list_head *p){p->next->prev=p->prev;p->prev->next=p->next;INIT_LIST_HEAD(p);}
struct rb_node {struct rb_node *rb_left,*rb_right;};
struct rb_root {struct rb_node *rb_node;};
#define RB_ROOT (struct rb_root){NULL}
#define rb_entry(p,t,m) container_of(p,t,m)
static void rb_link_node(struct rb_node *n,struct rb_node *parent,struct rb_node **link){n->rb_left=n->rb_right=NULL;*link=n;}
static void rb_insert_color(struct rb_node *n,struct rb_root *r){}
struct fbt_render_key {unsigned long long key1,key2;};
struct work_struct {int initialized;};
struct jerk {int timer;struct work_struct work;};
struct fbt_boost_info {long *cl_loading;int loading_weight,t_duration;struct {struct jerk jerks[RESCUE_TIMER_NUM];} proc;};
struct fbt_thread_blc {struct list_head entry;int blc,blc_b,blc_m,pid,dep_num;unsigned long long buffer_id;};
struct render_info {
 struct rb_node render_key_node;struct fbt_render_key render_key;
 struct list_head bufferid_list;struct work_struct reclaim_work;
 int thr_mlock,ux_mlock;bool jerks_initialized;struct fpsgo_task_identity task_identity;
 int pid,tgid,api,frame_type,bq_type,sbe_enhance,frame_count;
 unsigned long long identifier,buffer_id,t_last_start,master_type;
 struct rb_root ux_frame_info_tree;struct fbt_boost_info boost_info;struct fbt_thread_blc *p_blc;
};
static struct rb_root render_pid_tree;
static struct list_head blc_list;
static int blc_mlock,cluster_num=3,total_render_info_num,total_linger_num;
static int heap_live,fail_after=-1,allocations,kill_after=-1,timer_inits,cancel_count;
static void *allocate(size_t n){allocations++;if(kill_after==allocations)oldtask.alive=false;if(fail_after==0)return NULL;if(fail_after>0)fail_after--;void *p=calloc(1,n);if(p)heap_live++;return p;}
#define vzalloc(n) allocate(n)
#define kzalloc(n,f) allocate(n)
#define kcalloc(n,s,f) allocate((n)*(s))
static void release(void *p){if(p){assert(heap_live>0);heap_live--;free(p);}}
#define kfree(p) release(p)
#define vfree(p) release(p)
#define INIT_WORK(p,fn) do{(p)->initialized=1;}while(0)
#define mutex_init(p) (*(p)=0)
#define mutex_lock(p) do{assert(!*(p));*(p)=1;}while(0)
#define mutex_unlock(p) do{assert(*(p));*(p)=0;}while(0)
static pthread_mutex_t tree_lock=PTHREAD_MUTEX_INITIALIZER;
static _Thread_local int tree_held;
static void fpsgo_lockprove(const char *tag){assert(tree_held);}
static void lock_tree(void){assert(!pthread_mutex_lock(&tree_lock));tree_held=1;}
static void unlock_tree(void){tree_held=0;assert(!pthread_mutex_unlock(&tree_lock));}
static void set_bit(int n,unsigned long *p){*p|=1UL<<n;}
static void fbt_set_render_boost_attr(struct render_info *r){}
static void fbt_init_jerk(struct jerk *j,int n){j->timer=1;j->work.initialized=1;timer_inits++;}
static void hrtimer_cancel(int *p){assert(*p);cancel_count++;}
static void cancel_work_sync(struct work_struct *p){assert(p->initialized);cancel_count++;}
static void fpsgo_ux_free_frame_info(struct render_info *r){}
void fpsgo_task_identity_put(struct fpsgo_task_identity *id);
struct BQ_id {struct fpsgo_task_identity queue_identity;int queue_pid,queue_SF;unsigned long long buffer_id;};
static struct BQ_id *pair_fixture;
#define ACTION_FIND 0
static struct BQ_id *fpsgo_find_BQ_id(int pid,int tgid,long long identifier,int action){assert(tree_held);return pair_fixture;}
static struct render_info *fpsgo_find_render_key(int pid, unsigned long long identifier);
'''
c+='\n'.join(leaf(base,n) for n in ['fpsgo_task_identity_capture','fpsgo_task_identity_get','fpsgo_task_identity_get_group','fpsgo_task_identity_put','fpsgo_task_identity_alive','render_key_compare','fpsgo_is_exceed_render_info_limit'])
c+='\n'+ '\n'.join(leaf(fbt,n) for n in ['fbt_list_blc_prepare','fpsgo_base2fbt_node_prepare','fpsgo_base2fbt_node_publish','fpsgo_base2fbt_node_abort'])
c+='\n'+ '\n'.join(leaf(base,n) for n in ['fpsgo_release_render_work','fpsgo_search_and_add_render_info_identity','fpsgo_search_and_add_render_info','fpsgo_find_render_key','fpsgo_get_BQ_owner','fpsgo_get_BQid_pair_identity','fpsgo_get_BQid_pair','fpsgo_free_BQ_id'])
c+=r'''
static void baseline(void){assert(!heap_live && oldpid.refs==1 && newpid.refs==1 && group.refs==1 && foreign.refs==1 && list_empty(&blc_list) && !render_pid_tree.rb_node);}
static void reset(void){baseline();lookup=&oldtask;oldtask.alive=newtask.alive=true;oldtask.exit_state=0;newtask.exit_state=0;oldsig.live=newsig.live=1;fail_after=-1;allocations=0;kill_after=-1;total_render_info_num=total_linger_num=0;timer_inits=cancel_count=0;}
/* External removal primitives are modeled here; the final reclaim body above
 * is production. This is NOT a full item-del/QoS/linger/timer-race test. */
static void reclaim(struct render_info *r){render_pid_tree.rb_node=NULL;total_render_info_num--;list_del(&r->p_blc->entry);kfree(r->p_blc);kfree(r->boost_info.cl_loading);fpsgo_release_render_work(&r->reclaim_work);}
static void failures(void){
 for(int i=0;i<3;i++){reset();lock_tree();fail_after=i;assert(!fpsgo_search_and_add_render_info(71,9,1));assert(!total_render_info_num && list_empty(&blc_list));unlock_tree();baseline();}
 for(int i=1;i<=3;i++){reset();lock_tree();kill_after=i;assert(!fpsgo_search_and_add_render_info(71,9,1));unlock_tree();baseline();}
 reset();lock_tree();cluster_num=0;assert(!fpsgo_search_and_add_render_info(71,9,1));cluster_num=3;unlock_tree();baseline();
 reset();lock_tree();total_render_info_num=20;assert(!fpsgo_search_and_add_render_info(71,9,1));assert(!allocations);total_render_info_num=0;unlock_tree();baseline();
}
static void identities(void){
 reset();struct fpsgo_task_identity event={0};assert(!fpsgo_task_identity_get(71,&event));
 lock_tree();struct render_info *r=fpsgo_search_and_add_render_info_identity(71,9,1,&event);assert(r && total_render_info_num==1 && !list_empty(&blc_list));
 assert(r==fpsgo_search_and_add_render_info_identity(71,9,1,&event));assert(total_render_info_num==1 && heap_live==3 && timer_inits==5);
 struct fpsgo_task_identity bad={&oldpid,&foreign};assert(!fpsgo_search_and_add_render_info_identity(71,9,1,&bad));
 oldtask.alive=false;lookup=&newtask;
 assert(!fpsgo_search_and_add_render_info_identity(71,9,1,&event)); /* queued OLD event */
 assert(!fpsgo_search_and_add_render_info(71,9,1)); /* NEW identity must not inherit OLD node */
 assert(fpsgo_find_render_key(71,9)==r); /* reaper can find DEAD owner */
 fpsgo_task_identity_put(&event);reclaim(r);assert(cancel_count==10);unlock_tree();baseline();
 lock_tree();r=fpsgo_search_and_add_render_info(71,9,1);assert(r && r->task_identity.tid==&newpid);reclaim(r);unlock_tree();baseline();
}
static struct render_info *admitted[2];
static void *admit(void *arg){long i=(long)arg;lock_tree();admitted[i]=fpsgo_search_and_add_render_info(71,9,1);unlock_tree();return NULL;}
static void duplicates(void){reset();pthread_t a,b;assert(!pthread_create(&a,NULL,admit,(void*)0));assert(!pthread_create(&b,NULL,admit,(void*)1));pthread_join(a,NULL);pthread_join(b,NULL);assert(admitted[0] && admitted[0]==admitted[1] && total_render_info_num==1);lock_tree();reclaim(admitted[0]);unlock_tree();baseline();}
static void bq_ownership(void){
 reset();lock_tree();struct fpsgo_task_identity a={0},b={0};unsigned long long buffer=0;int sf=0;
 assert(!fpsgo_task_identity_get(71,&a));pair_fixture=kzalloc(sizeof(*pair_fixture),0);pair_fixture->buffer_id=99;
 assert(fpsgo_get_BQid_pair_identity(71,70,9,&buffer,&sf,1,&a));assert(buffer==99);
 assert(fpsgo_get_BQ_owner(&a,9)==&pair_fixture->queue_identity);
 assert(fpsgo_get_BQid_pair_identity(71,70,9,&buffer,&sf,1,&pair_fixture->queue_identity)); /* self-rebind */
 oldtask.alive=false;lookup=&newtask;assert(!fpsgo_task_identity_get(71,&b));
 assert(!fpsgo_get_BQ_owner(&b,9)); /* same number cannot make dead queue owner live */
 newtask.group=&foreign;fpsgo_task_identity_put(&b);assert(!fpsgo_task_identity_get(71,&b));
 assert(!fpsgo_get_BQid_pair_identity(71,90,9,&buffer,&sf,1,&b)); /* foreign process cannot rebind */
 newtask.group=&group;fpsgo_task_identity_put(&b);assert(!fpsgo_task_identity_get(71,&b));
 assert(fpsgo_get_BQid_pair_identity(71,70,9,&buffer,&sf,1,&b));
 assert(fpsgo_get_BQ_owner(&b,9)->tid==&newpid);
 fpsgo_task_identity_put(&a);fpsgo_task_identity_put(&b);fpsgo_free_BQ_id(pair_fixture);pair_fixture=NULL;
 unlock_tree();baseline();
}
static void adpf_group_identity(void){
 reset();lock_tree();struct fpsgo_task_identity proc={0};
 /* Leader PID 71 is the ADPF session owner; tgid object is the group. */
 group.nr=71;oldpid.nr=71;oldtask.group=&oldpid; /* leader: task_pid==task_tgid */
 assert(!fpsgo_task_identity_get_group(71,&proc) && proc.process);
 struct render_info *r=fpsgo_search_and_add_render_info_identity(71,255,1,&proc);
 assert(r && r->task_identity.process);
 /* A per-thread identity must NOT match a process-owned session. */
 struct fpsgo_task_identity thread={0};assert(!fpsgo_task_identity_get(71,&thread));
 assert(!fpsgo_search_and_add_render_info_identity(71,255,0,&thread));
 /* Leader thread exits, thread group still live: session stays valid. */
 oldtask.exit_state=1;assert(fpsgo_task_identity_alive(&proc));
 assert(fpsgo_search_and_add_render_info_identity(71,255,0,&proc)==r);
 /* Whole process exits: session must stop matching. */
 oldsig.live=0;assert(!fpsgo_task_identity_alive(&proc));
 assert(!fpsgo_search_and_add_render_info_identity(71,255,0,&proc));
 assert(fpsgo_find_render_key(71,255)==r); /* close/reaper can still remove it */
 fpsgo_task_identity_put(&proc);fpsgo_task_identity_put(&thread);reclaim(r);unlock_tree();
 oldsig.live=1;oldtask.exit_state=0;oldtask.group=&group;group.nr=70;baseline();
}
int main(void){INIT_LIST_HEAD(&blc_list);failures();identities();duplicates();bq_ownership();adpf_group_identity();puts("PASS actual renderer identity/admission + FBT preparation: allocation/exit rollback, unpublished abort, duplicate serialization, stale queued identity/recycled TID rejection, BQ owner rebinding, ADPF process-vs-thread identity and final reclaim reference puts");puts("Kernel task/locks/RB/alloc/timer primitives and removal setup MODELED; NOT native proof or complete downstream FBT/QoS ownership");}
'''
(opt.output/'test.c').write_text(c)
subprocess.run(['gcc','-std=gnu11','-O1','-g','-Wall','-Wno-unused-function','-Wno-unused-parameter','-Werror=implicit-function-declaration','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie','-pthread',str(opt.output/'test.c'),'-o',str(opt.output/'test')],check=True)
subprocess.run([str(opt.output/'test')],check=True)
