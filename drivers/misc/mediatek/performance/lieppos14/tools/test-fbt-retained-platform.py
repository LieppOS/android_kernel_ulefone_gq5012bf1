#!/usr/bin/env python3
"""Exercise actual retained-task base/platform FBT helper bodies.

Kernel scheduler, cpumask and vendor soft-affinity primitives are modeled.
This is a host ownership/target-selection witness, not native scheduler proof.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess

root = Path(__file__).resolve().parents[1]
base = root/'public/drivers/misc/mediatek/performance/fpsgo_v3/base/src/fpsgo_base.c'
plat = root/'public/drivers/misc/mediatek/performance/fpsgo_v3/fbt/platform/fbt_cpu_platform.c'
header = root/'public/drivers/misc/mediatek/performance/fpsgo_v3/fbt/include/fbt_cpu_platform.h'
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('output', type=Path)
a = ap.parse_args()
a.output.mkdir(exist_ok=False)

def leaf(text, name):
    m = re.search(r'^(?:static )?(?:long|void|int)\s+'+name+r'\([^)]*\)\n\{', text, re.M)
    assert m, name
    i = m.end(); depth = 1
    while depth:
        depth += (text[i] == '{') - (text[i] == '}')
        i += 1
    return text[m.start():i]

bt = base.read_text()
pt = plat.read_text()
bodies = '\n'.join([
    leaf(bt, 'fpsgo_task_setaffinity'),
    leaf(pt, 'fbt_build_task_cap_attr'),
    leaf(pt, 'fbt_trace_task_cap'),
    leaf(pt, 'fbt_set_per_task_cap_task'),
    leaf(pt, 'fbt_set_affinity_task'),
    leaf(pt, 'fbt_set_soft_affinity_task'),
    leaf(pt, 'fbt_set_vip_task'),
    leaf(pt, 'fbt_set_ls_task'),
])
enum = re.search(r'enum FPSGO_CPU_PREFER \{.*?\};', header.read_text(), re.S).group(0)

prefix = r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdbool.h>
#define SCHED_FLAG_KEEP_ALL 1U
#define SCHED_FLAG_UTIL_CLAMP 2U
#define SCHED_FLAG_RESET_ON_FORK 4U
#define PF_NO_SETAFFINITY 8U
#define READ_ONCE(x) (x)
#define min(a,b) ((a)<(b)?(a):(b))
#define clamp(v,lo,hi) ((v)<(lo)?(lo):((v)>(hi)?(hi):(v)))
#define fpsgo_systrace_c_fbt(...) ((void)0)
#define fpsgo_systrace_c_fbt_debug(...) ((void)0)
#define likely(x) (x)
struct cpumask {int value;};
struct sched_attr {int sched_policy;unsigned int sched_flags;int sched_util_min,sched_util_max;};
#define IS_ENABLED(x) (x)
#define CONFIG_MTK_SCHEDULER 1
#define CONFIG_MTK_SCHED_VIP_TASK 1
#define CONFIG_MTK_PERF_CTRL_LEASE 1
#define WRITE_ONCE(x,v) ((x)=(v))
struct vip_task_struct {bool basic_vip,vvip;};
struct soft_affinity_task {bool latency_sensitive;struct cpumask soft_cpumask;};
struct mtk_task {struct vip_task_struct vip_task;struct soft_affinity_task sa_task;};
struct task_struct {int pid,refs;unsigned long flags;struct mtk_task android_vendor_data1[1];};
static bool pid_alive(struct task_struct *p) {return p->refs>0;}
static void cpumask_copy(struct cpumask *a,const struct cpumask *b) {*a=*b;}
static struct cpumask fbt_generate_user_cpu_mask(int n) {return (struct cpumask){n};}
static void mtk_perf_task_legacy_vip(struct task_struct *p,bool v) {p->android_vendor_data1[0].vip_task.basic_vip=v;}
static int task_pid_nr(struct task_struct *p) {return p->pid;}
static struct task_struct *affinity_target,*sched_target;
static struct cpumask affinity_mask;
static struct sched_attr sched_seen;
static int affinity_ret,sched_ret;
static int set_cpus_allowed_ptr(struct task_struct *p,const struct cpumask *m) {
    affinity_target=p;affinity_mask=*m;return affinity_ret;
}
static int sched_setattr_nocheck(struct task_struct *p,const struct sched_attr *a) {
    sched_target=p;sched_seen=*a;return sched_ret;
}
static struct cpumask mask[8];
static int mask_done=1;
static int soft_pid,soft_set,soft_prefer;
static int fbt_set_soft_affinity(int pid,int set,unsigned int prefer) {
    soft_pid=pid;soft_set=set;soft_prefer=prefer;return 0;
}
'''
suffix = r'''
int main(void) {
    struct task_struct admitted={.pid=41,.refs=2}, recycled={.pid=41,.refs=1};
    (void)recycled; /* A same-number task exists, but direct helpers ignore it. */
    mask[FPSGO_PREFER_BIG].value=0x30;
    mask[FPSGO_PREFER_LITTLE].value=0x0f;

    assert(!fbt_set_affinity_task(&admitted,FPSGO_PREFER_BIG));
    assert(affinity_target==&admitted && affinity_mask.value==0x30);
    admitted.flags=PF_NO_SETAFFINITY;affinity_target=NULL;
    assert(fbt_set_affinity_task(&admitted,FPSGO_PREFER_BIG)==-EINVAL);
    assert(!affinity_target);
    admitted.flags=0;
    assert(fbt_set_affinity_task(NULL,FPSGO_PREFER_BIG)==-ESRCH);
    assert(fbt_set_affinity_task(&admitted,FPSGO_PREFER_TOTAL)==-EINVAL);
    mask_done=0;
    assert(fbt_set_affinity_task(&admitted,FPSGO_PREFER_BIG)==-ENODEV);
    mask_done=1;

    assert(!fbt_set_soft_affinity_task(&admitted,1,FPSGO_PREFER_LITTLE));
    assert(admitted.android_vendor_data1[0].sa_task.soft_cpumask.value==0x0f);
    assert(admitted.android_vendor_data1[0].sa_task.latency_sensitive);
    assert(!recycled.android_vendor_data1[0].sa_task.latency_sensitive);
    assert(!fbt_set_soft_affinity_task(&admitted,0,FPSGO_PREFER_LITTLE));
    assert(admitted.android_vendor_data1[0].sa_task.soft_cpumask.value==0xff);
    fbt_set_vip_task(&admitted,true,false);
    assert(admitted.android_vendor_data1[0].vip_task.basic_vip&&!recycled.android_vendor_data1[0].vip_task.basic_vip);
    fbt_set_ls_task(&admitted,true);
    assert(admitted.android_vendor_data1[0].sa_task.latency_sensitive&&!recycled.android_vendor_data1[0].sa_task.latency_sensitive);
    assert(fbt_set_soft_affinity_task(NULL,1,FPSGO_PREFER_LITTLE)==-ESRCH);

    sched_target=NULL;sched_ret=0;
    fbt_set_per_task_cap_task(&admitted,25,80,1024);
    assert(sched_target==&admitted);
    assert(sched_seen.sched_policy==-1);
    assert(sched_seen.sched_flags==(SCHED_FLAG_KEEP_ALL|SCHED_FLAG_UTIL_CLAMP|SCHED_FLAG_RESET_ON_FORK));
    assert(sched_seen.sched_util_min==204 && sched_seen.sched_util_max==655);
    sched_target=NULL;
    fbt_set_per_task_cap_task(NULL,25,80,1024);
    assert(!sched_target);
    fbt_set_per_task_cap_task(&admitted,0,100,1024);
    assert(sched_seen.sched_util_min==-1 && sched_seen.sched_util_max==-1);
    fbt_set_per_task_cap_task(&admitted,200,200,4096);
    assert(sched_seen.sched_util_min==819 && sched_seen.sched_util_max==819);

    puts("PASS actual retained-task platform helpers: hard affinity and uclamp target the admitted task directly; VIP/LS/soft affinity mutate that same task without numeric relookup; validation/reset/clamp semantics preserved.");
}
'''
source = prefix + enum + '\n' + bodies + suffix
c = a.output/'retained-platform.c'
c.write_text(source)
exe = a.output/'retained-platform'
subprocess.run([os.environ.get('CC','clang'), '-std=gnu11', '-g', '-O1',
    '-fsanitize=address,undefined', str(c), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True)
(a.output/'SOURCE.sha256').write_text(hashlib.sha256(bodies.encode()).hexdigest()+'\n')
