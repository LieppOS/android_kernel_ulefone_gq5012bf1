#!/usr/bin/env python3
"""Actual ADPF synchronous ingress/dispatch; provider/identity primitives modeled."""
import argparse, hashlib, os, re, subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
src=root/'public/drivers/misc/mediatek/performance/fpsgo_v3/fpsgo_main.c'
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('output',type=Path);a=ap.parse_args();a.output.mkdir(exist_ok=False)
def leaf(text,name):
 m=re.search(r'^(?:static )?(?:void|int)\s+'+name+r'\([^)]*\)\n\{',text,re.M);assert m,name
 i=m.end();d=1
 while d:d+=(text[i]=='{')-(text[i]=='}');i+=1
 return text[m.start():i]
t=src.read_text();bodies='\n'.join(leaf(t,n) for n in ['fpsgo_notifier_wq_cb_adpf_hint','fpsgo_notify_adpf_hint'])
prefix=r'''
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define ADPF_MAX_THREAD 8
#define ADPF_CREATE_HINT_SESSION 1
#define ADPF_UPDATE_TARGET_WORK_DURATION 2
#define ADPF_REPORT_ACTUAL_WORK_DURATION 3
#define ADPF_PAUSE 4
#define ADPF_RESUME 5
#define ADPF_CLOSE 6
#define ADPF_SENT_HINT 7
#define ADPF_SET_THREADS 8
#define FPSGO_LOGE(...) ((void)0)
struct pid {int nr;};
struct fpsgo_task_identity {struct pid *tid,*tgid;bool process;};
struct fpsgo_adpf_session {struct fpsgo_task_identity identity;int cmd,sid,tgid,uid;int dep_task_arr[ADPF_MAX_THREAD],dep_task_num;struct pid *dep_ids[ADPF_MAX_THREAD];unsigned long long workload_tcpu_arr[ADPF_MAX_THREAD],workload_ts_arr[ADPF_MAX_THREAD];int workload_num,used;unsigned long long target_time;};
struct _WORK_DURATION {long durationNanos,timeStampNanos;};
struct _SESSION {struct pid *owner,*target,*thread_owners[ADPF_MAX_THREAD];unsigned int cmd,sid,tgid,uid;int threadIds[ADPF_MAX_THREAD],threadIds_size;struct _WORK_DURATION *workDuration[ADPF_MAX_THREAD];int work_duration_size,used;long durationNanos;};
#define FPSGO_READY 1
#define smp_load_acquire(p) (*(p))
static int fpsgo_runtime=FPSGO_READY;
static bool identity_alive=true,enabled=true;
static int pid_nr(struct pid *p){return p?p->nr:0;}
static bool fpsgo_task_identity_alive(const struct fpsgo_task_identity *id){return id->tid&&identity_alive;}
static bool fpsgo_is_enable(void){return enabled;}
static unsigned long long seen_key;static int create_calls,update_calls,close_calls,create_error;static struct pid *seen_owner;
static int fpsgo_ctrl2comp_adpf_create_session(int a,int b,unsigned long long k,int*d,int n,struct pid * const *pids,unsigned long long t,const struct fpsgo_task_identity *id){create_calls++;seen_key=k;seen_owner=id->tgid;assert(d[1]==102 && pids[1] && pids[1]->nr==102);return create_error;}
static int fpsgo_ctrl2comp_set_target_time(int a,int b,unsigned long long k,unsigned long long t,const struct fpsgo_task_identity *id){update_calls++;seen_key=k;return 0;}
static int fpsgo_ctrl2comp_report_workload(int a,int b,unsigned long long k,unsigned long long*x,unsigned long long*y,int n,const struct fpsgo_task_identity *id){seen_key=k;return 0;}
static void fpsgo_ctrl2comp_adpf_pause(int a,unsigned long long k,const struct fpsgo_task_identity *id){seen_key=k;}
static void fpsgo_ctrl2comp_adpf_resume(int a,unsigned long long k,const struct fpsgo_task_identity *id){seen_key=k;}
static void fpsgo_ctrl2comp_adpf_close(int a,int b,unsigned long long k,const struct fpsgo_task_identity *id){close_calls++;seen_owner=id->tgid;seen_key=k;}
static int fpsgo_ctrl2comp_adpf_set_dep_list(int a,int b,unsigned long long k,int*d,int n,struct pid * const *pids,const struct fpsgo_task_identity *id){seen_key=k;return 0;}
'''
suffix=r'''
int main(void){
 struct pid daemon={99},owner={0x1234},t101={101},t102={102};struct _WORK_DURATION w={11,21};
 struct _SESSION s={.owner=&daemon,.target=&owner,.cmd=ADPF_CREATE_HINT_SESSION,.sid=0x12345678,.tgid=0x1234,.threadIds_size=2,.threadIds={101,102},.thread_owners={&t101,&t102},.work_duration_size=1,.workDuration={&w},.durationNanos=16666666};
 assert(!fpsgo_notify_adpf_hint(&s));assert(create_calls==1&&seen_key==0x0000123412345678ULL&&seen_owner==&owner);
 create_error=-ENOMEM;assert(fpsgo_notify_adpf_hint(&s)==-ENOMEM);create_error=0;
 fpsgo_runtime=0;s.cmd=ADPF_UPDATE_TARGET_WORK_DURATION;assert(fpsgo_notify_adpf_hint(&s)==-ESHUTDOWN && !update_calls);fpsgo_runtime=FPSGO_READY;
 identity_alive=false;s.cmd=ADPF_UPDATE_TARGET_WORK_DURATION;assert(!fpsgo_notify_adpf_hint(&s)&&!update_calls);
 enabled=false;s.cmd=ADPF_CLOSE;assert(!fpsgo_notify_adpf_hint(&s)&&close_calls==1&&seen_owner==&owner);
 s.workDuration[0]=NULL;assert(fpsgo_notify_adpf_hint(&s)==-EFAULT);s.work_duration_size=ADPF_MAX_THREAD+1;assert(fpsgo_notify_adpf_hint(&s)==-EINVAL);s.work_duration_size=0;
 s.owner=NULL;assert(fpsgo_notify_adpf_hint(&s)==-EINVAL);
 puts("PASS actual ADPF ingress: synchronous committed callback, producer identity borrowed without relookup/allocation; full-width keys; stale updates rejected; dead/disabled CLOSE delivered. Kernel/SRCU primitives modeled.");
}
'''
c=a.output/'adpf-event.c';c.write_text(prefix+bodies+suffix);exe=a.output/'adpf-event'
subprocess.run([os.environ.get('CC','clang'),'-std=gnu11','-g','-O1','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True,env=dict(os.environ,UBSAN_OPTIONS='halt_on_error=1'))
(a.output/'SOURCE.sha256').write_text(hashlib.sha256(bodies.encode()).hexdigest()+'\n')
