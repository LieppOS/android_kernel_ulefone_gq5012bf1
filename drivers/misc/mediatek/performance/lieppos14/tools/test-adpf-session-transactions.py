#!/usr/bin/env python3
"""Exercise actual XGF and composer ADPF create transaction bodies.

RB trees, identities, locks, FSTB and renderer admission are modeled. This is a
host rollback/ownership witness, not native locking or scheduler proof.
"""
import argparse,hashlib,os,re,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1];v3=root/'public/drivers/misc/mediatek/performance/fpsgo_v3'
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('output',type=Path);a=ap.parse_args();a.output.mkdir(exist_ok=False)

def leaf(text,name):
 m=re.search(r'^(?:static )?(?:int|void)\s+'+name+r'\([^)]*\)\n\{',text,re.M);assert m,name
 i=m.end();d=1
 while d:d+=(text[i]=='{')-(text[i]=='}');i+=1
 return text[m.start():i]
x=(v3/'fbt/src/xgf.c').read_text();c=(v3/'composer/src/fps_composer.c').read_text()
bodies=leaf(x,'fpsgo_comp2xgf_adpf_set_dep_list')+'\n'+leaf(c,'fpsgo_ctrl2comp_adpf_create_session')
prefix=r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#define IS_ERR_OR_NULL(p) (!(p) || (uintptr_t)(p)>=(uintptr_t)-4095L)
#define PTR_ERR(p) ((long)(intptr_t)(p))
#define ADPF_TYPE 1
#define NATIVE_WINDOW_API_EGL 2
#define NON_VSYNC_ALIGNED_TYPE 3
#define set_bit(n,p) (*(p)|=(1UL<<(n)))
#define test_bit(n,p) (!!(*(p)&(1UL<<(n))))
struct rb_root {void *rb_node;};
#define RB_ROOT (struct rb_root){0}
#define fpsgo_systrace_c_fbt(...) ((void)0)
#define xgf_trace(...) ((void)0)
struct pid {int nr;};static struct pid owner={7};
struct fpsgo_task_identity {struct pid *tid,*tgid;bool process;};
static int pid_nr(struct pid *p){return p?p->nr:0;}
struct xgf_dep {int unused;};
struct xgf_render_if {int tgid;unsigned long master_type;struct rb_root dep_list;int dep_list_size;};
struct render_info {int tgid,api,frame_type;unsigned long long buffer_id;unsigned long master_type;int thr_mlock;};
static int xgf_main_lock,adpf_hint_lock,render_lock;
static void mutex_lock(int *m){assert(!*m);*m=1;}static void mutex_unlock(int *m){assert(*m);*m=0;}
static unsigned long long fpsgo_get_time(void){return 10;}
static struct xgf_render_if xi;static bool xgf_live;static int add_index,fail_at=-1,xgf_resets,dep_resets;
static struct xgf_render_if *xgf_get_render_if(int rtid,unsigned long long id,unsigned long type,unsigned long long ts,int create){if(!xgf_live&&create){xgf_live=true;xi.master_type=type;}return xgf_live?&xi:NULL;}
static void xgf_reset_render_dep_list(struct xgf_render_if*r,int type){dep_resets++;r->dep_list=RB_ROOT;r->dep_list_size=0;}
static struct xgf_dep deps[8];
static struct xgf_dep *xgf_add_dep_task_identity(int tid,struct xgf_render_if*r,int type,int create,struct pid *expected,struct pid *target){if(add_index==fail_at)return NULL;r->dep_list.rb_node=&deps[add_index];r->dep_list_size++;return &deps[add_index++];}
static void xgf_reset_render(struct xgf_render_if*r){assert(xgf_live&&r==&xi);xgf_live=false;xgf_resets++;add_index=0;}
static int fstb_ret,fstb_create,fstb_delete;
static int fpsgo_comp2fstb_adpf_set_target_time(int tgid,int tid,unsigned long long id,unsigned long long target,int op){if(op==-1){fstb_delete++;return 0;}fstb_create++;return fstb_ret;}
static int identity_fail;static struct fpsgo_task_identity identity={&owner,&owner,true};
static bool fpsgo_task_identity_alive(const struct fpsgo_task_identity *id){return !identity_fail;}
static struct render_info ri;static bool admit_ok,existing;
static struct render_info *fpsgo_find_render_key(int tid,unsigned long long id){assert(render_lock);return existing?&ri:NULL;}
static void fpsgo_render_tree_lock(const char*f){assert(!render_lock);render_lock=1;}static void fpsgo_render_tree_unlock(const char*f){assert(render_lock);render_lock=0;}
static struct render_info *fpsgo_search_and_add_render_info_identity(int tid,unsigned long long id,int force,const struct fpsgo_task_identity *identity){assert(render_lock);if(admit_ok){existing=true;return &ri;}return NULL;}
static void fpsgo_thread_lock(int*m){mutex_lock(m);}static void fpsgo_thread_unlock(int*m){mutex_unlock(m);}
static int publishes;static void fpsgo_base2fbt_node_publish(struct render_info*r){publishes++;}
'''
suffix=r'''
static void reset_fixture(void){xi=(struct xgf_render_if){0};xgf_live=false;add_index=0;fail_at=-1;xgf_resets=dep_resets=0;fstb_ret=0;fstb_create=fstb_delete=0;identity_fail=0;existing=admit_ok=false;ri=(struct render_info){0};publishes=0;}
#define create() fpsgo_ctrl2comp_adpf_create_session(7,7,9,dep,3,pids,100,&identity)
int main(void){int dep[3]={11,12,13};struct pid *pids[3]={&owner,&owner,&owner};
 reset_fixture();fail_at=1;assert(fpsgo_comp2xgf_adpf_set_dep_list(7,7,9,dep,3,1,pids,&owner)==-ENOMEM);assert(!xgf_live&&!xgf_resets);
 reset_fixture();fail_at=0;assert(create()==-ENOMEM);assert(!xgf_live&&!fstb_create);
 reset_fixture();fstb_ret=-ENOSPC;assert(create()==-ENOSPC);assert(!xgf_live&&xgf_resets==1&&fstb_create==1&&!fstb_delete);
 reset_fixture();assert(create()==-ENOMEM);assert(!xgf_live&&xgf_resets==1&&fstb_create==1&&fstb_delete==1);
 reset_fixture();admit_ok=true;assert(!create());assert(xgf_live&&xi.dep_list_size==3&&fstb_create==1&&!fstb_delete);assert(create()==-EEXIST&&xgf_live&&xi.dep_list_size==3&&fstb_create==1);
 add_index=0;fail_at=1;void *old=xi.dep_list.rb_node;assert(fpsgo_comp2xgf_adpf_set_dep_list(7,7,9,dep,3,0,pids,&owner)==-ENOMEM);assert(xi.dep_list.rb_node==old&&xi.dep_list_size==3);
 xi.master_type=1;assert(fpsgo_comp2xgf_adpf_set_dep_list(7,7,9,NULL,0,-1,NULL,NULL)==-EEXIST&&xgf_live);
 reset_fixture();identity_fail=1;assert(create()==-ESRCH);assert(!xgf_live&&!fstb_create);
 puts("PASS actual ADPF transaction: collision/rejected CREATE preserves other state; candidate dependency tree commits by move; SET_THREADS failure preserves old tree; provider/renderer failure unwinds owned state. Kernel primitives modeled.");}
'''
cfile=a.output/'adpf-session.c';cfile.write_text(prefix+bodies+suffix);exe=a.output/'adpf-session'
subprocess.run([os.environ.get('CC','clang'),'-std=gnu11','-g','-O1','-fsanitize=address,undefined',str(cfile),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
(a.output/'SOURCE.sha256').write_text(hashlib.sha256(bodies.encode()).hexdigest()+'\n')
