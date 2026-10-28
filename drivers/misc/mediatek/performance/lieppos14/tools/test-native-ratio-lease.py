#!/usr/bin/env python3
"""Actual ratio provider and native setters; modeled lists/locks/CPUs, not IRQ proof."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess

root=Path(__file__).resolve().parents[1]
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('native_fixture',type=Path,help='reviewed patched grp_awr.c')
ap.add_argument('output',type=Path)
a=ap.parse_args();a.output.mkdir(exist_ok=False)
native=a.native_fixture.read_text()
def leaf(name):
    m=re.search(r'^void '+name+r'\([^)]*\)\n\{',native,re.M);assert m,name
    i=m.end();depth=1
    while depth:depth+=(native[i]=='{')-(native[i]=='}');i+=1
    return native[m.start():i]
setters='\n'.join(leaf(n) for n in ['set_cpu_group_active_ratio_pct',
    'set_cpu_group_active_ratio_cap','set_group_active_ratio_pct','set_group_active_ratio_cap'])
provider=root/'integration/target-c/provider/mtk_perf_group_ratio_impl.inc'
impl=re.sub(r'^#include[^\n]*\n','',provider.read_text(),flags=re.M)
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <pthread.h>
#define IS_ENABLED(x) (x)
#define GFP_KERNEL 0
#define EXPORT_SYMBOL_GPL(x)
#define NR_CPUS 4
#define GROUP_ID_RECORD_MAX 4
#define SCHED_CAPACITY_SCALE 1024
#define SCHED_CAPACITY_SHIFT 10
#define max(a,b) ((a)>(b)?(a):(b))
#define clamp_val(v,lo,hi) ((v)<(lo)?(lo):((v)>(hi)?(hi):(v)))
#define READ_ONCE(x) __atomic_load_n(&(x),__ATOMIC_RELAXED)
#define WRITE_ONCE(x,v) __atomic_store_n(&(x),(v),__ATOMIC_RELAXED)
#define DEFINE_RAW_SPINLOCK(n) pthread_mutex_t n=PTHREAD_MUTEX_INITIALIZER
static _Thread_local int held;
#define raw_spin_lock_irqsave(p,f) do{(f)=0;assert(!held);pthread_mutex_lock(p);held=1;}while(0)
#define raw_spin_unlock_irqrestore(p,f) do{(void)(f);assert(held);held=0;pthread_mutex_unlock(p);}while(0)
#define lockdep_assert_held(p) assert(held)
static bool fail_alloc;
static void *kzalloc(size_t n,int flags){assert(!held);return fail_alloc?NULL:calloc(1,n);}
static void kfree(void *p){assert(!held);free(p);}
struct list_head {struct list_head *next,*prev;};
#define LIST_HEAD(n) struct list_head n={&(n),&(n)}
static void INIT_LIST_HEAD(struct list_head *h){h->next=h->prev=h;}
static bool list_empty(struct list_head *h){return h->next==h;}
static void list_add_tail(struct list_head *n,struct list_head *h){assert(list_empty(n));n->prev=h->prev;n->next=h;h->prev->next=n;h->prev=n;}
static void list_del_init(struct list_head *n){assert(!list_empty(n));n->prev->next=n->next;n->next->prev=n->prev;INIT_LIST_HEAD(n);}
#define entry(n,type,m) ((type *)((char *)(n)-offsetof(type,m)))
#define list_for_each_entry(e,h,m) \
    for(struct list_head *_n=(h)->next;_n!=(h) && ((e)=entry(_n,__typeof__(*(e)),m),true);_n=_n->next)
#define list_for_each_entry_safe(e,next,h,m) \
    for(struct list_head *_n=(h)->next,*_next;_n!=(h) && ((_next=_n->next),(e)=entry(_n,__typeof__(*(e)),m),(next)=_next==(h)?NULL:entry(_next,__typeof__(*(e)),m),true);_n=_next)
static int nr_cpu_ids=NR_CPUS,grp_awr_init_finished=1;
#define cpu_possible(cpu) ((cpu)>=0 && (cpu)<NR_CPUS)
#define for_each_possible_cpu(cpu) for((cpu)=0;(cpu)<NR_CPUS;(cpu)++)
static int values[NR_CPUS][GROUP_ID_RECORD_MAX];
static int *pcpu_pgrp_act_rto_cap[NR_CPUS];
static void reset(void){for(int c=0;c<NR_CPUS;c++){pcpu_pgrp_act_rto_cap[c]=values[c];for(int g=0;g<4;g++)values[c][g]=600+c+g*10;}}
static void all(int group,int cap){for(int c=0;c<NR_CPUS;c++)assert(values[c][group]==cap);}
static void baseline(int group){for(int c=0;c<NR_CPUS;c++)assert(values[c][group]==600+c+group*10);}
'''
suffix=r'''
#if IS_ENABLED(CONFIG_MTK_PERF_CTRL_LEASE)
static void *ordinary_writer(void *unused){set_group_active_ratio_cap(1,777);return NULL;}
static void *client_writer(void *token){for(int i=0;i<10000;i++){int rc=mtk_group_ratio_set(token,500+i%200);assert(!rc || rc==-ESTALE);}return NULL;}
#endif
int main(void){
 reset();set_group_active_ratio_cap(1,300);all(1,300);
 set_group_active_ratio_pct(1,50);all(1,512);
 set_cpu_group_active_ratio_cap(2,1,700);assert(values[2][1]==700);
 set_cpu_group_active_ratio_pct(2,1,25);assert(values[2][1]==256);
#if IS_ENABLED(CONFIG_MTK_PERF_CTRL_LEASE)
 struct mtk_group_ratio_lease *p=NULL,*q=NULL,*r=NULL;
 reset();fail_alloc=true;assert(mtk_group_ratio_acquire(1,&p)==-ENOMEM && !p);baseline(1);fail_alloc=false;
 assert(mtk_group_ratio_acquire(4,&p)==-EINVAL && !p);
 grp_awr_init_finished=0;assert(mtk_group_ratio_acquire(1,&p)==-ENODEV && !p);grp_awr_init_finished=1;
 assert(!mtk_group_ratio_acquire(1,&p));assert(!mtk_group_ratio_set(p,400));all(1,400);
 assert(!mtk_group_ratio_acquire(1,&q));all(1,400);
 assert(!mtk_group_ratio_set(q,800));all(1,800);
 mtk_group_ratio_release(p);all(1,800);assert(mtk_group_ratio_is_live(q));
 assert(!mtk_group_ratio_set(q,0));baseline(1);
 assert(!mtk_group_ratio_set(q,500));all(1,500);
 assert(mtk_group_ratio_set(q,1025)==-ERANGE && mtk_group_ratio_is_live(q));all(1,500);
 set_group_active_ratio_cap(4,1);set_cpu_group_active_ratio_cap(-1,1,2);assert(mtk_group_ratio_is_live(q));
 set_group_active_ratio_cap(1,500);assert(!mtk_group_ratio_is_live(q));all(1,500);
 assert(!mtk_group_ratio_acquire(1,&p));assert(!mtk_group_ratio_set(p,900));
 mtk_group_ratio_release(q);all(1,900);assert(mtk_group_ratio_is_live(p));
 mtk_group_ratio_release(p);all(1,500);
 reset();assert(!mtk_group_ratio_acquire(1,&p));assert(!mtk_group_ratio_acquire(1,&q));
 assert(!mtk_group_ratio_set(p,400));assert(!mtk_group_ratio_set(q,500));
 set_cpu_group_active_ratio_cap(2,1,500);
 assert(!mtk_group_ratio_is_live(p) && !mtk_group_ratio_is_live(q));
 for(int c=0;c<NR_CPUS;c++)assert(values[c][1]==(c==2?500:610+c));
 assert(!mtk_group_ratio_acquire(1,&r));assert(!mtk_group_ratio_set(r,900));
 mtk_group_ratio_release(p);mtk_group_ratio_release(q);all(1,900);
 mtk_group_ratio_release(r);for(int c=0;c<NR_CPUS;c++)assert(values[c][1]==(c==2?500:610+c));
 reset();assert(!mtk_group_ratio_acquire(0,&p));assert(!mtk_group_ratio_set(p,500));
 assert(!mtk_group_ratio_acquire(1,&q));assert(!mtk_group_ratio_set(q,700));
 mtk_group_ratio_cpu_set(0,0,600);assert(!mtk_group_ratio_is_live(p));assert(mtk_group_ratio_is_live(q));all(1,700);
 mtk_group_ratio_release(p);mtk_group_ratio_release(q);baseline(1);
 reset();assert(!mtk_group_ratio_acquire(1,&p));assert(!mtk_group_ratio_set(p,500));
 pthread_t a,b;pthread_create(&a,NULL,client_writer,p);pthread_create(&b,NULL,ordinary_writer,NULL);pthread_join(a,NULL);pthread_join(b,NULL);
 assert(!mtk_group_ratio_is_live(p));mtk_group_ratio_release(p);all(1,777);
 assert(list_empty(&mtk_group_ratio_leases));for(int g=0;g<4;g++)assert(!mtk_group_ratio_count[g]);
#endif
 puts("PASS: actual ratio aggregate/withdraw/baseline/same-value writer/per-CPU preemption/ABA/concurrency; modeled CPU/IRQ primitives");
}
'''
for enabled in [1,0]:
    c=a.output/f'ratio-{enabled}.c';c.write_text(f'#define CONFIG_MTK_PERF_CTRL_LEASE {enabled}\n'+prefix+impl+setters+suffix)
    exe=c.with_suffix('')
    subprocess.run([os.environ.get('CC','clang'),'-std=gnu11','-O1','-g','-pthread',
                    '-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
(a.output/'RESULT.json').write_text(json.dumps(dict(scope=__doc__,provider_sha256=hashlib.sha256(provider.read_bytes()).hexdigest(),
    native_setters_sha256=hashlib.sha256(setters.encode()).hexdigest(),result='PASS',lease_configs=[1,0],native_irq_validation=False),indent=2)+'\n')
