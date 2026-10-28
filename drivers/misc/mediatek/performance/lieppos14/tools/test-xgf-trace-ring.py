#!/usr/bin/env python3
"""Actual production ring/callback host test; pthread/PID/IRQ primitives modeled.

No kernel compile, scheduler IRQ/rq lock-order, native ABI or whole-frame proof.
Outputs are always in an explicitly chosen NEW owned directory.
"""
import argparse
import pathlib
import re
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / 'public/drivers/misc/mediatek/performance/fpsgo_v3/fbt/src'
HEADER = SRC.parent / 'include/xgf_trace.h'
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('output', type=pathlib.Path)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
text = (SRC / 'xgf.c').read_text()
# The private module keeps its original Proprietary MODULE_LICENSE. Do not
# create GPL-only cross-module imports and do not rewrite license metadata.
ring_source = (SRC / 'xgf_trace_ring.inc').read_text()
for name in ['read', 'get', 'put']:
    assert f'EXPORT_SYMBOL(fpsgo_xgf_trace_{name});' in ring_source

def function(name):
    pattern = r'^(?:static )?(?:inline )?(?:void|int|long) ' + re.escape(name) + r'\('
    m = re.search(pattern, text, re.M)
    assert m, name
    i = text.index('{', m.start())
    depth = 1
    j = i + 1
    while depth:
        depth += (text[j] == '{') - (text[j] == '}')
        j += 1
    return text[m.start():j]

functions = '\n'.join(function(n) for n in [
    'xgf_get_task_wake_cpu', 'xgf_get_task_pid', 'xgf_get_task_state',
    'xgf_buffer_record_irq_waking_switch', 'fstb_buffer_record_waking_timer',
    'Test_fake_trace_event', 'xgf_irq_handler_entry_tracer',
    'xgf_irq_handler_exit_tracer', 'xgf_trace_sched_switch_state',
    'xgf_sched_switch_tracer', 'xgf_sched_waking_tracer',
    'xgf_hrtimer_expire_entry_tracer', 'xgf_hrtimer_expire_exit_tracer',
    'fpsgo_ktf2xgf_atomic_set', 'notify_xgf_ko_ready',
])
fixture = r'''
#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#define EXPORT_SYMBOL_GPL(x)
#define EXPORT_SYMBOL(x)
#define PIDTYPE_PID 0
#define TASK_RUNNING 0
#define TASK_STATE_MAX 256
#define XGF_BUFFER 0
#define FSTB_BUFFER 1
#define IRQ_ENTRY 0
#define IRQ_EXIT 1
#define SCHED_WAKING 2
#define SCHED_SWITCH 3
#define HRTIMER_ENTRY 4
#define HRTIMER_EXIT 5
#define DEFINE_RAW_SPINLOCK(n) pthread_mutex_t n = PTHREAD_MUTEX_INITIALIZER
static _Thread_local int lock_held, nmi_context;
#define in_nmi() nmi_context
#define raw_spin_lock_irqsave(p, f) do { (f)=0; assert(!lock_held); assert(!pthread_mutex_lock(p)); lock_held=1; } while(0)
#define raw_spin_unlock_irqrestore(p, f) do { (void)(f); assert(lock_held); lock_held=0; assert(!pthread_mutex_unlock(p)); } while(0)
typedef atomic_int atomic_t;
#define atomic_read(p) atomic_load(p)
#define atomic_set(p,v) atomic_store(p,v)
struct pid {atomic_int refs; int nr;};
struct task_struct {struct pid *identity; int pid, wake_cpu; long __state;};
struct irqaction {int unused;};
struct hrtimer {int unused;};
typedef long long ktime_t;
static _Thread_local struct task_struct *current;
static struct pid *get_pid(struct pid *p) {if(p)assert(atomic_fetch_add(&p->refs,1)>0);return p;}
static void put_pid(struct pid *p) {assert(!lock_held);if(p)assert(atomic_fetch_sub(&p->refs,1)>1);}
static struct pid *get_task_pid(struct task_struct *p,int kind) {assert(!lock_held);return get_pid(p->identity);}
static atomic_ullong clock_tick;
static unsigned long long fpsgo_get_time(void) {return atomic_fetch_add(&clock_tick,1)+1;}
static atomic_t xgf_event_buffer_idx, fstb_event_buffer_idx;
static int xgf_ko_ready;
'''
fixture += '#include "' + str(HEADER) + '"\n'
fixture += r'''
static struct fpsgo_trace_event xr[4], fr[2];
static struct fpsgo_trace_event *xgf_event_buffer, *fstb_event_buffer;
static int xgf_event_buffer_size, fstb_event_buffer_size;
'''
fixture += '#include "' + str(SRC / 'xgf_trace_ring.inc') + '"\n'
fixture += functions
fixture += r'''
static struct pid oldpid={.refs=1,.nr=71}, newpid={.refs=1,.nr=71}, callerpid={.refs=1,.nr=23};
static struct task_struct oldtask={&oldpid,71,2,1}, newtask={&newpid,71,3,0}, caller={&callerpid,23,0,0};
static void init_ring(void) {
    assert(!xgf_event_buffer && !fstb_event_buffer);
    xgf_event_buffer=xr;xgf_event_buffer_size=4;
    fstb_event_buffer=fr;fstb_event_buffer_size=2;
    xgf_ko_ready=0;assert(notify_xgf_ko_ready());
}
static void baseline(void) {assert(oldpid.refs==1 && newpid.refs==1 && callerpid.refs==1);}
static void test_callbacks(void) {
    struct fpsgo_trace_event e={0}, hold={0};
    init_ring();current=&caller;
    assert(fpsgo_xgf_trace_read(0,0,&e)==-ENOENT && !e.sequence);
    assert(fpsgo_xgf_trace_get(2,0,0,&e)==-ENOENT);
    assert(fpsgo_xgf_trace_get(0,-1,0,&e)==-ENOENT);
    assert(fpsgo_xgf_trace_get(0,4,0,&e)==-ENOENT);
    assert(fpsgo_xgf_trace_get(0,0,0,0)==-EINVAL);
    xgf_sched_waking_tracer(0,&oldtask);
    assert(xgf_event_buffer_idx==1 && fstb_event_buffer_idx==1);
    assert(!fpsgo_xgf_trace_get(0,0,0,&hold));
    assert(hold.pid_ref==&oldpid && hold.note_ref==&callerpid);
    assert(hold.pid==71 && hold.note==23 && hold.event==SCHED_WAKING);
    unsigned long long old_seq=hold.sequence;
    assert(!fpsgo_xgf_trace_read(0,0,&e));
    assert(!e.pid_ref && !e.note_ref && e.sequence==old_seq);
    assert(!fpsgo_xgf_trace_get(1,0,0,&e));
    assert(e.pid_ref==&oldpid && e.note_ref==&callerpid);fpsgo_xgf_trace_put(&e);
    /* Replacement task has the SAME numeric PID, a DIFFERENT pid object. */
    xgf_sched_switch_tracer(0,0,&oldtask,&newtask,0);
    assert(!fpsgo_xgf_trace_get(0,1,0,&e));
    assert(e.pid_ref==&newpid && e.note_ref==&oldpid && e.state==1);
    fpsgo_xgf_trace_put(&e);
    struct irqaction irq={0};struct hrtimer timer={0};
    xgf_irq_handler_entry_tracer(0,17,&irq);
    xgf_irq_handler_exit_tracer(0,17,&irq,0);
    assert(!fpsgo_xgf_trace_get(0,2,0,&e));
    assert(!e.pid_ref && e.note_ref==&callerpid && e.state==17);
    fpsgo_xgf_trace_put(&e);
    /* Full wrap replaces slot0; held snapshot must still name oldpid. */
    xgf_sched_waking_tracer(0,&newtask);
    assert(fpsgo_xgf_trace_get(0,0,old_seq,&e)==-ESTALE && !e.sequence);
    assert(hold.pid_ref==&oldpid && hold.note_ref==&callerpid);
    assert(!fpsgo_xgf_trace_get(0,0,0,&e));assert(e.pid_ref==&newpid);
    fpsgo_xgf_trace_put(&e);
    xgf_hrtimer_expire_entry_tracer(0,&timer,0);
    assert(!fpsgo_xgf_trace_get(1,0,0,&e));
    assert(e.event==HRTIMER_ENTRY && !e.pid_ref && e.note_ref==&callerpid && e.addr==(uintptr_t)&timer);
    fpsgo_xgf_trace_put(&e);
    xgf_hrtimer_expire_exit_tracer(0,&timer);
    /* Numeric-only synthetic event MUST NOT gain a fabricated identity. */
    Test_fake_trace_event(0,2,SCHED_WAKING,71,23,4,400,0);
    assert(!fpsgo_xgf_trace_get(0,1,0,&e));
    assert(e.pid==71 && !e.pid_ref && !e.note_ref);fpsgo_xgf_trace_put(&e);
    assert(fpsgo_ktf2xgf_atomic_set(1,-123));
    xgf_sched_waking_tracer(0,&newtask);
    assert(xgf_event_buffer_idx==1);
    assert(fpsgo_ktf2xgf_atomic_set(2,2000));
    xgf_hrtimer_expire_entry_tracer(0,&timer,0);
    assert(fstb_event_buffer_idx==1);
    assert(!fpsgo_ktf2xgf_atomic_set(99,0));
    nmi_context=1;
    assert(!fpsgo_ktf2xgf_atomic_set(1,0));
    unsigned long long before_nmi=xgf_trace_sequence;
    Test_fake_trace_event(0,0,2,71,23,0,10,0);
    assert(xgf_trace_sequence==before_nmi);
    assert(fpsgo_xgf_trace_get(0,0,0,&e)==-EWOULDBLOCK && !e.sequence);
    nmi_context=0;
    unsigned long long seq=xgf_trace_sequence;
    xgf_trace_reset_indices();assert(!xgf_event_buffer_idx && !fstb_event_buffer_idx);
    assert(xgf_trace_sequence==seq);
    xgf_trace_disable();assert(!notify_xgf_ko_ready());
    xgf_sched_waking_tracer(0,&newtask); /* failed publish puts newly captured refs */
    assert(fpsgo_xgf_trace_read(0,0,&e)==-ENOENT);
    xgf_trace_detach();
    assert(oldpid.refs==2 && callerpid.refs==2); /* only the held snapshot */
    fpsgo_xgf_trace_put(&hold);fpsgo_xgf_trace_put(&hold);baseline();
    xgf_trace_detach();baseline();
    init_ring();assert(fpsgo_xgf_trace_get(0,0,old_seq,&e)==-ENOENT);
    xgf_sched_waking_tracer(0,&newtask);assert(xgf_trace_sequence>seq);
    xgf_trace_detach();baseline();
}
static atomic_int finished;
static void *writer(void *arg) {
    struct task_struct *task=arg;
    for(int i=0;i<15000;i++) {
        unsigned long long n=fpsgo_get_time();
        struct fpsgo_trace_event e={.pid=task->pid,.note=23,.event=2,.cpu=(int)(n%4),.state=(int)(n%100),.ts=n,.addr=n^0xfeda};
        xgf_trace_publish((int)(n%2),&e,task,&caller);
    }
    atomic_fetch_add(&finished,1);return 0;
}
static void *reader(void *unused) {
    while(atomic_load(&finished)<4) {
        for(int b=0;b<2;b++) for(int i=0;i<(b?2:4);i++) {
            struct fpsgo_trace_event e={0};
            int rc=fpsgo_xgf_trace_get(b,i,0,&e);
            if(rc) {assert(rc==-ENOENT && !e.sequence);continue;}
            assert(e.sequence && e.pid_ref && e.note_ref==&callerpid);
            assert(e.pid==e.pid_ref->nr && e.note==23 && e.event==2);
            assert(e.cpu==(int)(e.ts%4) && e.state==(int)(e.ts%100) && e.addr==(e.ts^0xfeda));
            fpsgo_xgf_trace_put(&e);
        }
    }
    return 0;
}
static void concurrent(int detach) {
    pthread_t w[4],r[3];init_ring();finished=0;
    for(int i=0;i<3;i++)assert(!pthread_create(&r[i],0,reader,0));
    for(int i=0;i<4;i++)assert(!pthread_create(&w[i],0,writer,i%2?&oldtask:&newtask));
    if(detach)xgf_trace_detach();
    for(int i=0;i<4;i++)assert(!pthread_join(w[i],0));
    for(int i=0;i<3;i++)assert(!pthread_join(r[i],0));
    xgf_trace_detach();baseline();
}
int main(void) {
    test_callbacks();concurrent(0);concurrent(1);
    puts("PASS actual XGF/FSTB callbacks and ring: coherent publication, exact pid refs, overwrite/stale sequence, scalar reads, cursor reset, detach/unwind/concurrent writers+readers");
}
'''
(a.output / 'test.c').write_text(fixture)
cmd = ['gcc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra',
       '-Wno-unused-parameter', '-Werror=implicit-function-declaration',
       '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
       '-pthread', str(a.output / 'test.c'), '-o', str(a.output / 'test')]
subprocess.run(cmd, check=True)
subprocess.run([str(a.output / 'test')], check=True)
# Actual full private composite TU syntax/type/layout check, NOT native compile.
subprocess.run(['gcc', '-std=gnu11', '-fsyntax-only',
                '-Dnoinline=__attribute__((noinline))',
                '-Werror=implicit-function-declaration',
                str(ROOT / 'source/fpsgo-private-recon.c')], check=True)
print('PASS complete private composite TU host syntax/shared record layout; NOT native ABI/modpost/IRQ proof')
