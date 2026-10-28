#!/usr/bin/env python3
"""Run the real FBT capacity callback with concurrent modeled policy producers."""
import argparse
import os
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('output', type=Path)
a = p.parse_args()
a.output.mkdir(exist_ok=False)
text = (root/'public/drivers/misc/mediatek/performance/fpsgo_v3/fbt/src/fbt_cpu.c').read_text()
m = re.search(r'^void fbt_cpufreq_cb_cap\([^)]*\)\n\{',text,re.M)
assert m
end, depth = m.end(), 1
while depth:
    depth += (text[end] == '{') - (text[end] == '}')
    end += 1
body = text[m.start():end]
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#define LOADING_CNT 8
#define SCHED_CAPACITY_SCALE 1024
#define READ_ONCE(x) (x)
#define max(a,b) ((a)>(b)?(a):(b))
#define xgf_trace(...) ((void)0)
#define fpsgo_systrace_c_fbt_debug(...) ((void)0)
static pthread_mutex_t freq_slock=PTHREAD_MUTEX_INITIALIZER,loading_slock=PTHREAD_MUTEX_INITIALIZER;
static _Thread_local int lock_depth;
#define spin_lock_irqsave(p,f) do {(f)=0;assert(!pthread_mutex_lock(p));lock_depth++;} while(0)
#define spin_unlock_irqrestore(p,f) do {(void)(f);lock_depth--;assert(!pthread_mutex_unlock(p));} while(0)
static int fbt_enable=1,cluster_num=2;
static unsigned long long fpsgo_get_time(void) {assert(lock_depth==1);return 1000000;}
static unsigned long long nsec_to_100usec_ull(unsigned long long n) {return n/100000;}
static unsigned int fbt_is_cl_isolated(int n) {assert(lock_depth==1);return 0;}
static unsigned long long stamps[8],previous[8],last_stamp;
static unsigned int obv[8],bycluster[8][2],isolated[8][2],*bp[8],*ip[8],index_,clus[2],iso[2],last_obv;
'''
suffix = r'''
static void update(int cid,int cap,unsigned long long t) {
 fbt_cpufreq_cb_cap(cid,cap,stamps,previous,obv,bp,ip,&index_,&last_stamp,clus,iso,&last_obv,t);
}
static void *producer(void *arg) {
 int cid=(int)(intptr_t)arg;
 for(int i=0;i<10000;i++)update(cid,100+(i%800),(unsigned long long)(1000+((i*13)%100))*100000);
 return NULL;
}
int main(void) {
 for(int i=0;i<8;i++){bp[i]=bycluster[i];ip[i]=isolated[i];}
 update(0,512,20000000);assert(last_stamp==200 && last_obv==50);
 update(1,256,10000000);assert(last_stamp==200 && previous[index_]==200 && stamps[index_]==200);
 unsigned int before=index_;
 update(-1,512,30000000);update(2,512,30000000);update(0,-1,30000000);update(0,INT_MAX,30000000);
 assert(index_==before && last_stamp==200);
 index_=UINT_MAX;update(0,513,21000000); /* same rounded percentage is inert */
 update(0,700,21000000);assert(index_==0 && last_stamp==210);
 fbt_enable=0;update(1,900,999999999);assert(last_stamp==210);fbt_enable=1;
 pthread_t a,b;assert(!pthread_create(&a,NULL,producer,(void *)(intptr_t)0));
 assert(!pthread_create(&b,NULL,producer,(void *)(intptr_t)1));
 assert(!pthread_join(a,NULL) && !pthread_join(b,NULL));
 for(int i=0;i<8;i++)assert(previous[i]<=stamps[i]);
 assert(last_stamp==1099 && index_<8 && !lock_depth);
 puts("PASS actual FBT cpufreq callback: stale/cross-policy timestamps, concurrent updates, invalid domains/caps, disabled admission and ring wrap");
}
'''
c = a.output/'cpufreq-history.c'
c.write_text(prefix+body+suffix)
exe = a.output/'cpufreq-history'
subprocess.run([os.environ.get('CC','clang'),'-std=gnu11','-O1','-g','-pthread',
    '-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
