#!/usr/bin/env python3
"""Real native provider + patched setters under host locks; NOT kernel/IRQ proof."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess

root = Path(__file__).resolve().parents[1]
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('native_fixture', type=Path, help='patched committed nonlinear_opp_cap.c')
ap.add_argument('output', type=Path)
a = ap.parse_args()
a.output.mkdir(exist_ok=False)
native = a.native_fixture.read_text()
provider = root / 'integration/target-c/provider'

def leaf(name):
    m = re.search(r'^(?:void|int|bool) '+name+r'\([^\n]*\)\n\{', native, re.M)
    assert m, name
    i = m.end()
    n = 1
    while n:
        n += (native[i] == '{') - (native[i] == '}')
        i += 1
    return native[m.start():i]

def without_includes(text):
    return re.sub(r'^#include[^\n]*\n', '', text, flags=re.M)

prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <pthread.h>
#define IS_ENABLED(x) (x)
#define CONFIG_MTK_PERF_CTRL_LEASE 1
#define MAX_NR_CPUS 8
#define SCHED_CAPACITY_SCALE 1024
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define GFP_KERNEL 0
#define EXPORT_SYMBOL_GPL(x)
#define WRITE_ONCE(a,b) __atomic_store_n(&(a),(b),__ATOMIC_RELAXED)
#define READ_ONCE(a) __atomic_load_n(&(a),__ATOMIC_RELAXED)
#define DEFINE_RAW_SPINLOCK(n) pthread_mutex_t n = PTHREAD_MUTEX_INITIALIZER
static _Thread_local int held;
#define raw_spin_lock_irqsave(p,f) do { (f)=0; assert(!held); pthread_mutex_lock(p); held=1; } while (0)
#define raw_spin_unlock_irqrestore(p,f) do { (void)(f); assert(held); held=0; pthread_mutex_unlock(p); } while (0)
#define lockdep_assert_held(p) assert(held)
static bool fail_alloc;
static void *kzalloc(size_t n, int f) { assert(!held); return fail_alloc ? NULL : calloc(1,n); }
static void kfree(void *p) { assert(!held); free(p); }
static int pd_count=3, nr_wl_type=4, wl_type_manual=-1;
static bool gu_ctrl, cu_ctrl;
static int gear_uclamp_max[MAX_NR_CPUS];
static bool is_wl_support(void) { return true; }
static bool warn_once(bool v) { return v; }
#define WARN_ON_ONCE(x) warn_once(x)
'''
setters = '\n'.join(leaf(x) for x in ['set_wl_type_manual', 'set_curr_uclamp_ctrl',
    'set_gear_uclamp_ctrl', 'set_gear_uclamp_max'])
suffix = r'''
static void baseline(void) {
    assert(!mtk_perf_ctrl_owner);
    gu_ctrl=false; cu_ctrl=true; wl_type_manual=-1;
    for (int i=0;i<MAX_NR_CPUS;i++) gear_uclamp_max[i]=1000-i*10;
}
static int limits[3]={400,500,600};
static struct mtk_perf_ctrl_lease *take(void) {
    struct mtk_perf_ctrl_lease *p=NULL;
    assert(!mtk_perf_ctrl_acquire(3,limits,2,&p));
    assert(p && gu_ctrl && cu_ctrl && wl_type_manual==2);
    return p;
}
static void *old_writer(void *x) { set_gear_uclamp_max(0,450); return NULL; }
static void *client_writer(void *x) {
    for (int i=0;i<10000;i++) {
        int r=mtk_perf_ctrl_set_gear(x,0,450);
        assert(r==0 || r==-ESTALE);
    }
    return NULL;
}
int main(void) {
    struct mtk_perf_ctrl_lease *p=NULL,*q=NULL;
    baseline(); fail_alloc=true;
    assert(mtk_perf_ctrl_acquire(3,limits,2,&p)==-ENOMEM && !p);
    assert(!gu_ctrl && wl_type_manual==-1);
    fail_alloc=false;
    assert(mtk_perf_ctrl_acquire(2,limits,2,&p)==-ENODEV && !p);
    p=take(); assert(mtk_perf_ctrl_acquire(3,limits,2,&q)==-EBUSY && !q);
    assert(!mtk_perf_ctrl_set_gear(p,0,550));
    set_gear_uclamp_max(3,500); /* invalid legacy write cannot preempt */
    set_gear_uclamp_max(0,2048);
    assert(mtk_perf_ctrl_is_live(p));
    assert(mtk_perf_ctrl_set_gear(p,3,550)==-EINVAL);
    assert(mtk_perf_ctrl_set_gear(p,0,1025)==-ERANGE);
    mtk_perf_ctrl_release(p);
    assert(!gu_ctrl && cu_ctrl && wl_type_manual==-1 && gear_uclamp_max[0]==1000);
    for (int which=0;which<4;which++) {
        baseline(); p=take();
        switch (which) {
        case 0: set_gear_uclamp_max(0,450); break;
        case 1: set_curr_uclamp_ctrl(0); break;
        case 2: set_wl_type_manual(3); break;
        case 3: set_gear_uclamp_ctrl(0); break;
        }
        assert(!mtk_perf_ctrl_is_live(p));
        assert(mtk_perf_ctrl_set_gear(p,1,550)==-ESTALE);
        mtk_perf_ctrl_release(p);
        assert(!gu_ctrl);
        if (which==0) assert(gear_uclamp_max[0]==450);
        if (which==1) assert(!cu_ctrl);
        if (which==2) assert(wl_type_manual==3);
        if (which==3) assert(gear_uclamp_max[0]==1024);
    }
    baseline(); p=take();
    set_gear_uclamp_max(0,limits[0]); /* identical value still preempts */
    assert(!mtk_perf_ctrl_is_live(p)); q=take();
    mtk_perf_ctrl_release(p); assert(mtk_perf_ctrl_is_live(q));
    mtk_perf_ctrl_release(q);
    baseline(); p=take();
    pthread_t a,b; pthread_create(&a,NULL,client_writer,p); pthread_create(&b,NULL,old_writer,NULL);
    pthread_join(a,NULL); pthread_join(b,NULL);
    assert(!mtk_perf_ctrl_is_live(p) && gear_uclamp_max[0]==450);
    mtk_perf_ctrl_release(p); assert(gear_uclamp_max[0]==450 && !gu_ctrl);
    puts("PASS: real native lease acquire/unwind/preemption/ABA/stale-handle/concurrent writers");
}
'''
parts = [without_includes((provider/n).read_text()) for n in
    ['mtk_perf_ctrl.h','mtk_perf_ctrl_state.inc']]
impl = without_includes((provider/'mtk_perf_ctrl_impl.inc').read_text())
text = prefix + '\n'.join(parts) + setters + impl + suffix
c = a.output/'native-control.c'
c.write_text(text)
exe = a.output/'native-control'
subprocess.run([os.environ.get('CC','clang'),'-std=gnu11','-O1','-g','-pthread',
    '-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
(a.output/'RESULT.json').write_text(json.dumps({'scope':__doc__,
    'fixture_sha256':hashlib.sha256(native.encode()).hexdigest(),
    'provider_sha256':hashlib.sha256(impl.encode()).hexdigest(),
    'result':'PASS','native_compile':False,'native_irq_verification':False},indent=2)+'\n')
