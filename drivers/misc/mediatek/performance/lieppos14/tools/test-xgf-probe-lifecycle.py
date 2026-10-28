#!/usr/bin/env python3
"""Exercise actual XGF probe transactions; tracepoint/grace primitives are modeled."""
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
text = (root/'public/drivers/misc/mediatek/performance/fpsgo_v3/fbt/src/xgf.c').read_text()

def function(name):
    m = re.search(r'^static void __nocfi '+name+r'\(void\)\n\{', text, re.M)
    assert m, name
    end, depth = m.end(), 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[m.start():end]

prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <errno.h>
#include <stdio.h>
#define __nocfi
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define pr_info(...) ((void)0)
struct tracepoint {int id;};
struct tracepoints_table {const char *name;void *func;struct tracepoint *tp;bool registered;};
static struct tracepoint points[6];
static struct tracepoints_table xgf_tracepoints[6];
static bool active[6];
static int xgf_nr_cpus, calls, removals, joins, resets, readers, fail_at=-1;
static int xgf_num_possible_cpus(void) {return 8;}
static int xgf_tracepoint_probe_register(struct tracepoint *tp,void *fn,void *arg) {
    int n=calls++;
    assert(tp && !active[tp->id]);
    if(n==fail_at)return -ENOMEM;
    active[tp->id]=true;return 0;
}
static int xgf_tracepoint_probe_unregister(struct tracepoint *tp,void *fn,void *arg) {
    assert(tp && active[tp->id]);active[tp->id]=false;removals++;return 0;
}
static void tracepoint_synchronize_unregister(void) {
    for(int i=0;i<6;i++)assert(!active[i]);
    joins++;readers=0;
}
static void xgf_trace_reset_indices(void) {assert(!readers);resets++;}
'''
suffix = r'''
int main(void) {
    for(int i=0;i<6;i++) {
        points[i].id=i;xgf_tracepoints[i].tp=&points[i];xgf_tracepoints[i].name="probe";
    }
    for(int fault=0;fault<6;fault++) {
        calls=removals=0;fail_at=fault;
        xgf_tracing_register();
        assert(calls==fault+1 && removals==fault);
        for(int i=0;i<6;i++)assert(!active[i] && !xgf_tracepoints[i].registered);
    }
    fail_at=-1;calls=removals=0;
    xgf_tracing_register();assert(calls==6 && !removals);
    int before=resets;
    readers=1; /* old callbacks may still hold ring access */
    xgf_tracing_register(); /* duplicate boost must be entirely inert */
    assert(calls==6 && !removals && resets==before);
    xgf_tracing_unregister();assert(removals==6 && !readers && resets==before+1);
    before=removals;xgf_tracing_unregister();assert(removals==before);
    xgf_tracepoints[3].tp=NULL;calls=removals=0;
    xgf_tracing_register();assert(calls==3 && removals==3);
    xgf_tracepoints[3].tp=&points[3];calls=0;
    xgf_tracing_register();assert(calls==6);
    readers=1;xgf_tracing_unregister();
    puts("PASS actual XGF probe functions: duplicate enable, every registration failure, missing probe, exact rollback, grace-before-reset, repeated disable and re-enable");
}
'''
c = a.output/'xgf-probes.c'
c.write_text(prefix+'\n'+function('xgf_tracing_unregister')+'\n'+function('xgf_tracing_register')+suffix)
exe = a.output/'xgf-probes'
subprocess.run([os.environ.get('CC', 'clang'), '-std=gnu11', '-O1', '-g',
    '-fsanitize=address,undefined', str(c), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True)
